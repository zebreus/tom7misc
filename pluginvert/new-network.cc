
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
//
// neighborhood: The number of adjacent nodes that are guaranteed to
// be sampled from the corresponding spatial location in the input
// layer. The square is actually sized neighborhood + 1 + neighborhood
// along both dimensions.
static void FillSparseIndices(ArcFour *rc,
                              int neighborhood,
                              bool use_gaussian,
                              int src_width, int src_height, int src_channels,
                              int dst_width, int dst_height, int dst_channels,
                              Network::Layer *layer) {

  CHECK(neighborhood >= 0) << "must include the pixel itself.";
  int64 rejected = 0LL, duplicate = 0LL;
  // Generate the indices for a single node at position idx.
  // Indices are sorted for locality of access.
  auto OneNode = [rc,
                  neighborhood, use_gaussian,
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
    CHECK((neighborhood * 2 + 1) * (neighborhood * 2 + 1) *
          src_channels <= indices_per_node) <<
    "neighborhood doesn't fit in indices (hood: " <<
    ((neighborhood * 2 + 1) * (neighborhood * 2 + 1) * src_channels) <<
    ", ipc: " << indices_per_node << ")";

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
    for (int ny = -neighborhood; ny <= neighborhood; ny++) {
      for (int nx = -neighborhood; nx <= neighborhood; nx++) {
        // Note that the pixel may be clipped.
        for (int nc = 0; nc < src_channels; nc++) {
          AddNodeByCoordinates(cx + nx, cy + ny, nc);
        }
      }
    }

    CHECK_LE(indices.size(), indices_per_node);

    // XXX Select this dynamically based on how many unused nodes
    // are even left?
    if (use_gaussian) {
      static constexpr double stddev = 1 / 16.0;

      // Sample gaussian pixels.
      while (indices.size() < indices_per_node) {
        double dx = gauss->Next() * stddev;
        double dy = gauss->Next() * stddev;

        int nc = RandTo(rc, src_channels);
        AddNodeByCoordinates((int)round((xf + dx) * src_width),
                             (int)round((yf + dy) * src_height),
                             nc);
      }
    } else {

      // XXXXX
      int hood = neighborhood;
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
    }

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
        // XXX this should be indicated somehow else.
        if (net->layers[layer].transfer_function == IDENTITY)
          return;

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

  vector<int> num_nodes, widths, heights, channelses;
  vector<uint32_t> renderstyles;
  vector<Network::Layer> layers;

  [[maybe_unused]]
  auto AddConvolutional = [&](int num_features,
                              int pat_width, int pat_height,
                              int x_stride, int y_stride) {
      int prev_width = widths.back();
      int prev_height = heights.back();
      int prev_channels = channelses.back();
      int prev_num_nodes = prev_width * prev_height * prev_channels;

      int src_width = prev_width * prev_channels;
      int src_height = prev_height;

      int indices_per_node = pat_width * pat_height;

      Network::Layer layer;
      layer.indices_per_node = indices_per_node;
      layer.type = LAYER_CONVOLUTION_ARRAY;
      // configurable?
      layer.transfer_function = LEAKY_RELU;
      layer.num_features = num_features;
      layer.pattern_width = pat_width;
      layer.pattern_height = pat_height;
      layer.src_width = src_width;
      layer.src_height = src_height;
      layer.occurrence_x_stride = x_stride;
      layer.occurrence_y_stride = y_stride;

      auto [indices, this_num_nodes, occ_across, occ_down] =
        Network::MakeConvolutionArrayIndices(
            prev_num_nodes,
            num_features,
            pat_width,
            pat_height,
            src_width,
            src_height,
            x_stride,
            y_stride);

      layer.num_occurrences_across = occ_across;
      layer.num_occurrences_down = occ_down;
      layer.indices = std::move(indices);
      layer.weights.resize(num_features * indices_per_node, 0.0f);
      layer.biases.resize(num_features, 0.0f);

      layers.push_back(std::move(layer));

      num_nodes.push_back(occ_across * occ_down * num_features);
      widths.push_back(occ_across);
      heights.push_back(occ_down);
      channelses.push_back(num_features);
      renderstyles.push_back(RENDERSTYLE_MULTIRGB);

      printf("Now %d x %d x %d\n",
             widths.back(), heights.back(), channelses.back());
    };

  [[maybe_unused]]
  auto AddSparse = [&](int width, int height, int channels,
                       int indices_per_node,
                       int neighborhood) {

      const int prev_width = widths.back();
      const int prev_height = heights.back();
      const int prev_channels = channelses.back();
      // const int prev_num_nodes = prev_width * prev_height * prev_channels;

      const int this_num_nodes = width * height * channels;

      Network::Layer layer;
      layer.type = LAYER_SPARSE;
      layer.transfer_function = LEAKY_RELU;
      layer.indices_per_node = indices_per_node;

      layer.indices.resize(this_num_nodes * indices_per_node, 0);
      layer.weights.resize(this_num_nodes * indices_per_node, 0.0f);
      layer.biases.resize(this_num_nodes, 0.0f);
      FillSparseIndices(rc,
                        neighborhood,
                        /* use_gaussian */ false,
                        prev_width, prev_height, prev_channels,
                        width, height, channels,
                        &layer);

      layers.push_back(std::move(layer));

      num_nodes.push_back(this_num_nodes);
      widths.push_back(width);
      heights.push_back(height);
      channelses.push_back(channels);
      renderstyles.push_back(RENDERSTYLE_RGB);
      printf("Now %d x %d x %d\n",
             widths.back(), heights.back(), channelses.back());
    };

  [[maybe_unused]]
    auto AddNES8x8Permutation = [&]() {
        const int prev_width = widths.back();
        const int prev_height = heights.back();
        const int prev_channels = channelses.back();

        // Each an 8x8 block of UV coords.
        CHECK(prev_channels == 64 * 2);
        CHECK(prev_width == 32);
        CHECK(prev_height == 30);

        const int this_num_nodes = 256 * 240 * 2;

        Network::Layer layer;
        // TODO: LAYER_PERMUTATION or LAYER_FIXED or whatever.
        // A layer with IPN=1 is cheap to include, but can
        // be nearly free if implemented directly, and we
        // might prefer to leave the weight and bias fixed?
        layer.type = LAYER_SPARSE;
        layer.transfer_function = IDENTITY;
        layer.indices_per_node = 1;

        // 0 bias, 1x weight.
        layer.biases.resize(this_num_nodes, 0.0f);
        layer.weights.resize(this_num_nodes * 1, 1.0f);

        layer.indices.reserve(this_num_nodes);

        // Generate as pixels, 2 channels at a time.
        const int UVCH = 2;
        for (int y = 0; y < 240; y++) {
          for (int x = 0; x < 256; x++) {
            // Which 8x8 block am I in?
            int bx = x / 8;
            int by = y / 8;
            int bidx = by * 32 + bx;
            // And what's my flat index in that block?
            int ox = x % 8;
            int oy = y % 8;
            int oidx = oy * 8 + ox;

            // ... giving my flat pixel index
            int pidx = bidx * (8 * 8) + oidx;
            // and expanded to each channel
            for (int c = 0; c < UVCH; c++) {
              layer.indices.push_back(pidx * UVCH + c);
            }
          }
        }

        layers.push_back(std::move(layer));

        num_nodes.push_back(this_num_nodes);
        widths.push_back(256);
        heights.push_back(240);
        channelses.push_back(2);
        renderstyles.push_back(RENDERSTYLE_NESUV);
        printf("Now %d x %d x %d\n",
               widths.back(), heights.back(), channelses.back());
      };


  // input layer
  widths.push_back(NES_WIDTH);
  heights.push_back(NES_HEIGHT);
  // uv
  channelses.push_back(2);
  renderstyles.push_back(RENDERSTYLE_NESUV);
  num_nodes.push_back(NES_WIDTH * NES_HEIGHT * 2);

  const int EXPAND_FEATURES = 128;
  // Without overlap, expand 8x8(x2) blocks into some features.
  const int UVCH = 2;
  AddConvolutional(EXPAND_FEATURES, 8 * UVCH, 8, 8 * UVCH, 8);

  // Now work on strips of EXPAND_FEATURES size, since the
  // features are linearized.
  AddConvolutional(EXPAND_FEATURES,
                   EXPAND_FEATURES, 1,
                   EXPAND_FEATURES, 1);

  // Contract the cells to 1/4 size.
  AddConvolutional(EXPAND_FEATURES / 4,
                   EXPAND_FEATURES, 1,
                   EXPAND_FEATURES, 1);

  // Only 7k nodes.. maybe a bit extreme. Consider
  // expanding the number of channels here!
  const int BOTTLENECK_CHANNELS = 8;
  AddSparse(32, 30, BOTTLENECK_CHANNELS, 144, 0);

  // Now expand to render with another convolution.
  // This one expands each "pixel" in the input into an 8x8x2 block.
  // (But the 8 * 8 * 2 is arranged as a 64 * 2 row.)
  AddConvolutional(8 * 8 * 2,
                   BOTTLENECK_CHANNELS, 1,
                   BOTTLENECK_CHANNELS, 1);

  // Arbitrary in-place transformation of the 64 cells.
  AddConvolutional(8 * 8 * 2,
                   8 * 8 * 2, 1,
                   8 * 8 * 2, 1);

  // And then permute it into the screen we actually want.
  AddNES8x8Permutation();

  CHECK(widths.back() == 256);
  CHECK(heights.back() == 240);
  CHECK(channelses.back() == 2);

  std::unique_ptr<Network> net =
    std::make_unique<Network>(num_nodes, layers);

  printf("Set net presentational attributes:\n");
  net->width = widths;
  net->height = heights;
  net->channels = channelses;
  net->renderstyle = renderstyles;

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
