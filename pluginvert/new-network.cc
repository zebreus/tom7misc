
// Generate a new Network object and write it to disk. You do this
// once at the start of training, or probably ad nauseum as you
// start over and tweak parameters in hopes of the Miracle occurring.
//
// This code is generally pretty problem-specific, so hack and slash!
// But it's probably good to keep some clean-ish utilities.

#include "network.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_set>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "util.h"

#include "problem.h"

using namespace std;
using int64 = int64_t;
using uint32 = uint32_t;

// When generating the initial network, the number of nodes that
// are guaranteed to be sampled from the corresponding spatial
// location in the input layer. The square is actually sized
// NEIGHBORHOOD + 1 + NEIGHBORHOOD along both dimensions.
static constexpr int NEIGHBORHOOD = 2;

// .. utils
template<class C>
static void DeleteElements(C *cont) {
  for (auto &elt : *cont) {
    delete elt;
  }
  cont->clear();
}

// Make indices. This assumes that nodes are 2D "pixel" data, where on
// each layer we have width[l] * height[l] pixels, with channels[l]
// nodes per pixel. Row-major order.
//
// We mostly sample from a Gaussian near each pixel, but:
//  - we reject duplicates (inefficient),
//  - we reject pixels off the image (doesn't make sense; wrapping around
//      would work but an image is not a torus)
//  - we require that a small neighborhood around the pixel is mapped
//      directly (especially the pixel itself; this preserves spatial
//      locality and makes sure we don't have any statically dead nodes).
//
// TODO: Allow specifying a strategy for index assignment. For chess,
// taking full rows, columns, and diagonals is probably better than
// random gaussians!
//
// TODO: If the number of indices requested is close to the number
// available (or equal to it), then this approach is super inefficient.
// Could instead randomly delete indices. But at least we only do it
// once.

// Fill the indices on the sparse layer randomly.
static void FillSparseIndices(ArcFour *rc,
                              int src_width, int src_height, int src_channels,
                              int dst_width, int dst_height, int dst_channels,
                              Network::Layer *layer) {

  static_assert(NEIGHBORHOOD >= 0, "must include the pixel itself.");
  int64 rejected = 0LL, duplicate = 0LL;
  // Generate the indices for a single node at position idx.
  // Indices are sorted for locality of access.
  auto OneNode = [rc,
                  src_width, src_height, src_channels,
                  dst_width, dst_height, dst_channels,
                  &rejected, &duplicate](
                      RandomGaussian *gauss,
                      int indices_per_node,
                      int idx) -> vector<uint32> {

    CHECK(indices_per_node <= src_width * src_height * src_channels) <<
    "Can't get " << indices_per_node
    << " distinct indices from a layer with " <<
    src_width << " x " << src_height << " x " << src_channels <<
    " = " << (src_width * src_height * src_channels) << " sources";

    // Whenever we read the neighborhood, we include all source channels.
    CHECK((NEIGHBORHOOD * 2 + 1) * (NEIGHBORHOOD * 2 + 1) *
          src_channels <= indices_per_node) <<
    "neighborhood doesn't fit in indices!";
    // Which pixel is this?
    const int dst_nodes_per_row = dst_width * dst_channels;
    [[maybe_unused]]
    const int c = idx % dst_channels;
    const double x = (idx % dst_nodes_per_row) / (double)dst_channels;
    const double y = idx / (double)dst_nodes_per_row;

    const double xf = x / (double)dst_width;
    const double yf = y / (double)dst_height;

    // Use hash set for deduplication; we re-sort for locality of access later.
    unordered_set<int> indices;
    // clips xx,yy if they are out of the image (and does nothing).
    // cc must be a valid channel index.
    auto AddNodeByCoordinates =
      [src_width, src_height, src_channels, &indices,
       &rejected, &duplicate](int xx, int yy, int cc) {
      CHECK_GE(cc, 0);
      CHECK_LT(cc, src_channels);
      if (xx < 0 || yy < 0 || xx >= src_width || yy >= src_height) {
        rejected++;
        return;
      }
      int idx = (yy * src_width * src_channels) + xx * src_channels + cc;
      CHECK_GE(idx, 0);
      CHECK_LT(idx, src_width * src_height * src_channels);
      auto p = indices.insert(idx);
      if (!p.second) duplicate++;
    };

    // Find the closest corresponding pixel in the src layer; add all
    // its channels.
    const int cx = round(xf * src_width);
    const int cy = round(yf * src_height);
    for (int ny = -NEIGHBORHOOD; ny <= NEIGHBORHOOD; ny++) {
      for (int nx = -NEIGHBORHOOD; nx <= NEIGHBORHOOD; nx++) {
        // Note that the pixel may be clipped.
        for (int nc = 0; nc < src_channels; nc++) {
          AddNodeByCoordinates(cx + nx, cy + ny, nc);
        }
      }
    }

    CHECK_LE(indices.size(), indices_per_node);

    // XXX Select this dynamically based on how many unused nodes
    // are even left?
    #if 0
    static constexpr double stddev = 1 / 16.0;

    // Sample gaussian pixels.
    while (indices.size() < indices_per_node) {
      double dx = gauss->Next() * stddev;
      double dy = gauss->Next() * stddev;

      AddNodeByCoordinates((int)round((xf + dx) * src_width),
                           (int)round((yf + dy) * src_height));
    }
    #else

    // XXXXX
    int hood = NEIGHBORHOOD;
    while (indices.size() < indices_per_node) {
      hood++;
      const int cx = round(xf * src_width);
      const int cy = round(yf * src_height);
      for (int ny = -hood; ny <= hood; ny++) {
        for (int nx = -hood; nx <= hood; nx++) {
          // In the interests of getting more spatial
          // dispersion, only add one channel at random. As
          // we expand we can try these multiple times, so
          // pixels closer to the center are more likely to
          // have all channels used.
          int nc = RandTo(rc, src_channels);
          AddNodeByCoordinates(cx + nx, cy + ny, nc);
          if (indices.size() == indices_per_node) goto done;
        }
      }
    }
  done:;
    #endif

    CHECK_EQ(indices_per_node, indices.size());
    vector<uint32> ret;
    ret.reserve(indices_per_node);
    for (int idx : indices) {
      CHECK_GE(idx, 0);
      CHECK_LT(idx, src_width * src_height * src_channels);
      ret.push_back(idx);
    }
    std::sort(ret.begin(), ret.end());
    return ret;
  };

  // Assign sparse layers randomly.
  CHECK(layer->type == LAYER_SPARSE);
  const int indices_per_node = layer->indices_per_node;
  const int num_nodes = dst_width * dst_height * dst_channels;
  CHECK(layer->indices.size() == num_nodes * indices_per_node);
  RandomGaussian gauss{rc};
  for (int node_idx = 0;
       node_idx < num_nodes;
       node_idx++) {
    vector<uint32> indices =
      OneNode(&gauss, indices_per_node, node_idx);
    CHECK_EQ(indices_per_node, indices.size());
    const int start_idx = node_idx * indices_per_node;
    for (int i = 0; i < indices_per_node; i++) {
      layer->indices[start_idx + i] = indices[i];
    }
    if (node_idx % 1000 == 0) {
      printf("  %d. [%d/%d] %.1f%% (%lld rejected %lld dupe)\n",
             layer,
             node_idx, num_nodes,
             (100.0 * node_idx) / num_nodes,
             rejected, duplicate);
    }
  }
}

// Randomize the weights in a network. Doesn't do anything to indices.
static void RandomizeNetwork(ArcFour *rc, Network *net) {
  [[maybe_unused]]
  auto RandomizeFloatsGaussian =
    [](float mag, ArcFour *rc, vector<float> *vec) {
      RandomGaussian gauss{rc};
      for (int i = 0; i < vec->size(); i++) {
        (*vec)[i] = mag * gauss.Next();
      }
    };

  [[maybe_unused]]
  auto RandomizeFloatsUniform =
    [](float mag, ArcFour *rc, vector<float> *vec) {
      // Uniform from -mag to mag.
      const float width = 2.0f * mag;
      for (int i = 0; i < vec->size(); i++) {
        // Uniform in [0,1]
        double d = (double)Rand32(rc) / (double)0xFFFFFFFF;
        float f = (width * d) - mag;
        (*vec)[i] = f;
      }
    };

  // This must access rc serially.
  vector<ArcFour *> rcs;
  for (int i = 0; i < net->num_layers; i++) rcs.push_back(Substream(rc, i));

  // But now we can do all layers in parallel.
  CHECK_EQ(net->num_layers, net->layers.size());
  ParallelComp(
      net->num_layers,
      [rcs, &RandomizeFloatsUniform, &net](int layer) {
        // XXX such hacks. How to best initialize?

        for (float &f : net->layers[layer].biases) f = 0.0f;
        // RandomizeFloats(0.000025f, rcs[layer], &net->layers[layer].biases);
        // RandomizeFloats(0.025f, rcs[layer], &net->layers[layer].weights);

        // The more indices we have, the smaller initial weights we should
        // use.
        // "Xavier initialization"
        const float mag = 1.0f / sqrtf(net->layers[layer].indices_per_node);
        // "He initialization"
        // const float mag = sqrtf(2.0 / net->layers[layer].indices_per_node);
        // Tom initialization
        // const float mag = (0.0125f / net->layers[layer].indices_per_node);
        RandomizeFloatsUniform(mag, rcs[layer], &net->layers[layer].weights);
      }, 12);

  DeleteElements(&rcs);
}

static std::unique_ptr<Network> CreateInitialNetwork(ArcFour *rc) {

  const vector<int> width_config =
    { NES_WIDTH,
      NES_WIDTH / 4,
      NES_WIDTH / 12,
      NES_WIDTH / 4,
      NES_WIDTH, };

  // If zero, automatically factor to make square-ish.
  const vector<int> height_config =
    { NES_HEIGHT,
      NES_HEIGHT / 4,
      NES_HEIGHT / 12,
      NES_HEIGHT / 4,
      // output layer
      NES_HEIGHT, };

  // When zero, create a dense layer.
  const vector<int> indices_per_node_config = {
    (int)(NES_WIDTH * NES_HEIGHT * 0.01),
    (int)((NES_WIDTH * NES_HEIGHT / 4) * 0.01),
    (int)((NES_WIDTH * NES_HEIGHT / 12) * 0.02),
    (int)((NES_WIDTH * NES_HEIGHT / 4) * 0.01),
  };

  const int num_layers = indices_per_node_config.size();

  // Everything is RGB.
  const vector<int> channels(num_layers + 1, 3);

  // All use leaky relu
  const vector<TransferFunction> transfer_functions = {
    LEAKY_RELU,
    LEAKY_RELU,
    LEAKY_RELU,
    LEAKY_RELU,
  };

  const vector<uint32_t> renderstyle = {
    RENDERSTYLE_RGB,
    RENDERSTYLE_RGB,
    RENDERSTYLE_RGB,
    RENDERSTYLE_RGB,
    RENDERSTYLE_RGB,
  };


  vector<int> height, width;
  CHECK(height_config.size() == width_config.size());
  for (int i = 0; i < height_config.size(); i++) {
    int w = width_config[i];
    int h = height_config[i];
    CHECK(w > 0);
    CHECK(h >= 0);
    if (h == 0) {
      if (w == 1) {
        width.push_back(1);
        height.push_back(1);
      } else {
        // Try to make a rectangle that's squareish.
        vector<int> factors = Util::Factorize(w);

        CHECK(!factors.empty()) << w << " has no factors??";

        // XXX Does this greedy approach produce good results?
        int ww = factors.back(), hh = 1;
        factors.pop_back();

        for (int f : factors) {
          if (ww < hh)
            ww *= f;
          else
            hh *= f;
        }

        CHECK(ww * hh == w);
        width.push_back(ww);
        height.push_back(hh);
      }
    } else {
      width.push_back(w);
      height.push_back(h);
    }
  }

  // num_nodes = width * height * channels
  // //  indices_per_node = indices_per_channel * channels
  // //  vector<int> indices_per_node;
  vector<int> num_nodes;
  CHECK_EQ(width.size(), height.size());
  CHECK_EQ(width.size(), channels.size());
  CHECK_EQ(width.size(), renderstyle.size());
  CHECK_EQ(num_layers + 1, height.size());
  CHECK_EQ(num_layers, indices_per_node_config.size());
  CHECK_EQ(num_layers, transfer_functions.size());
  for (int i = 0; i < num_layers + 1; i++) {
    CHECK(width[i] >= 1);
    CHECK(height[i] >= 1);
    CHECK(channels[i] >= 1);
    num_nodes.push_back(width[i] * height[i] * channels[i]);
  }

  vector<Network::Layer> layers(num_layers);

  // In parallel?
  for (int i = 0; i < indices_per_node_config.size(); i++) {
    Network::Layer &layer = layers[i];
    int ipc = indices_per_node_config[i];
    // TODO: Support convolution
    if (ipc == 0) {
      const int indices_per_node = width[i] * height[i] * channels[i];
      layer = Network::MakeDenseLayer(num_nodes[i + 1],
                                      indices_per_node,
                                      transfer_functions[i]);
    } else {
      layer.type = LAYER_SPARSE;
      CHECK(ipc > 0);
      CHECK(ipc <= width[i] * height[i] * channels[i]);
      int num_nodes = width[i + 1] * height[i + 1] * channels[i + 1];
      layer.indices_per_node = ipc;
      layer.indices.resize(num_nodes * layer.indices_per_node, 0);
      layer.weights.resize(num_nodes * layer.indices_per_node, 0.0f);
      layer.biases.resize(num_nodes, 0.0f);
      FillSparseIndices(rc,
                        width[i], height[i], channels[i],
                        width[i + 1], height[i + 1], channels[i + 1],
                        &layer);
    }
  }

  std::unique_ptr<Network> net =
    std::make_unique<Network>(num_nodes, layers);

  net->width = width;
  net->height = height;
  net->channels = channels;
  net->renderstyle = renderstyle;

  printf("Randomize weights:\n");
  RandomizeNetwork(rc, net.get());

  // Should still be well-formed.
  net->StructuralCheck();
  net->NaNCheck("initial network");

  return net;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage:\n"
           "./new-network.exe network-out.val\n");
    return -1;
  }

  ArcFour rc(StringPrintf("new-network %lld", time(nullptr)));
  std::unique_ptr<Network> net = CreateInitialNetwork(&rc);

  CHECK(net.get() != nullptr);
  net->SaveNetworkBinary(argv[1]);

  return 0;
}
