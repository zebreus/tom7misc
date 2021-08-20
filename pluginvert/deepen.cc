
#include <string>
#include <vector>
#include <shared_mutex>
#include <cstdint>
#include <unordered_set>

#include "threadutil.h"
#include "randutil.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"

#include "timer.h"

#include "network.h"
#include "network-util.h"

using namespace std;

using int64 = int64_t;

// Take some layer (as an index into Layers; i.e. a hidden layer
// index) and create another layer right after it, with the same
// dimensions (this means that any layers that follow can just keep
// their existing node references). The new layer has 0 bias, weight 1
// "on the diagonal", and other random connections with zero weight.
//
// TODO: It would work fine to add a layer at the front, so perhaps
// this index should be based on the num_nodes array, not the layer
// array?
static constexpr int LAYER_TO_COPY = 3;

// Indices per node for the newly added layer. One of them will
// be used to copy the corresponding existing node. Others can
// be claimed by custom code below; the rest will be random.
static constexpr int NEW_IPN = 256;


// Create a new network with a copy of the indicated layer. The new
// layer is just the sparse identity (ipn = 1).
static Network *DeepenNetwork(ArcFour *rc,
                              const Network &old_net, int layer_idx) {

  // Specs for the new layer.
  int prev_width = old_net.width[layer_idx + 1];
  int prev_height = old_net.height[layer_idx + 1];
  int prev_channels = old_net.channels[layer_idx + 1];
  int prev_num_nodes = old_net.num_nodes[layer_idx + 1];
  CHECK(prev_num_nodes == prev_width * prev_height * prev_channels);

  vector<int> num_nodes = old_net.num_nodes;
  vector<int> width = old_net.width;
  vector<int> height = old_net.height;
  vector<int> channels = old_net.channels;
  vector<uint32_t> renderstyle = old_net.renderstyle;

  printf("Input network num_nodes:");
  for (int nn : num_nodes) {
    printf(" %d", nn);
  }
  printf("\n");
  fflush(stdout);

  auto MakeCopy = [layer_idx](auto &v) {
      printf("size %d   %d + 1 = %d\n", v.size(), layer_idx, layer_idx + 1);
      const auto prev = v[layer_idx + 1];
      v.insert(v.begin() + layer_idx + 1 + 1, prev);
    };

  MakeCopy(num_nodes);
  MakeCopy(width);
  MakeCopy(height);
  MakeCopy(channels);
  MakeCopy(renderstyle);

  // Create a new layer with defaults.
  std::vector<Network::Layer> layers = old_net.layers;
  layers.insert(layers.begin() + layer_idx + 1, Network::Layer());
  Network::Layer *layer = &layers[layer_idx + 1];
  CHECK(layer->indices.empty()) << "expecting default";

  layer->type = LAYER_SPARSE;
  layer->transfer_function = LEAKY_RELU;
  layer->indices_per_node = 1;

  // Initialize to the identity.
  for (int i = 0; i < prev_num_nodes; i++) {
    layer->indices.push_back(i);
    layer->weights.push_back(1.0f);
    layer->biases.push_back(0.0f);
  }

  // Create new network; this computes the inverted indices and
  // all that.
  Network *net = new Network(num_nodes, layers);
  CHECK(net->num_layers == old_net.num_layers + 1);
  CHECK(layer_idx + 1 < net->layers.size());
  // Update presentational parameters
  net->width = width;
  net->height = height;
  net->channels = channels;
  net->renderstyle = renderstyle;

  // Copy history
  net->rounds = old_net.rounds;
  net->examples = old_net.examples;

  return net;
}

// Just modifies the indices on the given layer, so this is pretty easy.
static void DensifyLayer(ArcFour *rc, Network *net, int layer_idx) {
  EZLayer ez(*net, layer_idx, /* allow_channels = */ true);

  // This code is written expecting the layer to be the same dimensions,
  // and to already contain one identity node.
  const int width = net->width[layer_idx];
  const int height = net->height[layer_idx];
  const int channels = net->channels[layer_idx];
  const int previous_layer_size = net->num_nodes[layer_idx];
  CHECK(ez.ipn == 1);
  CHECK(width == net->width[layer_idx + 1]);
  CHECK(height == net->height[layer_idx + 1]);
  CHECK(channels == net->channels[layer_idx + 1]);
  CHECK(previous_layer_size == net->num_nodes[layer_idx + 1]);

  printf("Densify layer with dimensions %dx%dx%d\n",
         width, height, channels);

  for (int src_idx = 0; src_idx < ez.nodes.size(); src_idx++) {
    EZLayer::Node &node = ez.nodes[src_idx];
    node.inputs.reserve(NEW_IPN);
    CHECK(node.inputs.size() == 1);
    std::unordered_set<uint32_t> used;
    used.insert(node.inputs[0].index);

    auto MaybeAdd = [&node, &used, previous_layer_size](int idx) {
        if (idx < 0) return false;
        if (idx >= previous_layer_size) return false;
        if (used.find(idx) == used.end()) {
          EZLayer::OneIndex oi;
          oi.index = idx;
          oi.weight = 0.0f;
          node.inputs.push_back(oi);
          used.insert(idx);
          return true;
        } else {
          return false;
        }
      };

    RandomGaussian gauss{rc};

    // Should probably add nearby "pixels" in some neighborhood first?

    static constexpr double stddev = 1 / 16.0;

    int row = (src_idx / channels) / width;
    int col = (src_idx / channels) % width;

    double xf = col / (double)width;
    double yf = row / (double)height;

    // Sample gaussian pixels.
    while (node.inputs.size() < NEW_IPN) {
      double dx = gauss.Next() * stddev;
      double dy = gauss.Next() * stddev;

      int nx = round((xf + dx) * width);
      int ny = round((yf + dy) * height);
      int nc = RandTo(rc, channels);

      MaybeAdd(ny * width * channels +
               nx * channels +
               nc);
    }

    #if 0
    static constexpr int NEIGHBORHOOD = 4;

    // In the SDF region, add nodes from the immediate neighborhood.
    if (src_idx < SDF_SIZE * SDF_SIZE) {
      int y = src_idx / SDF_SIZE;
      int x = src_idx % SDF_SIZE;
      for (int dy = -NEIGHBORHOOD; dy <= NEIGHBORHOOD; dy++) {
        for (int dx = -NEIGHBORHOOD; dx <= NEIGHBORHOOD; dx++) {
          int ny = y + dy;
          int nx = x + dx;
          if (ny >= 0 && nx >= 0 &&
              ny < SDF_SIZE && nx <= SDF_SIZE) {
            int nidx = ny * SDF_SIZE + nx;
            MaybeAdd(nidx);
          }
        }
      }
    }
    #endif

    CHECK(node.inputs.size() <= NEW_IPN);

    // The rest, randomly assign.
    while (node.inputs.size() < NEW_IPN) {
      (void)MaybeAdd(RandTo(rc, previous_layer_size));
    }
  }

  ez.ipn = NEW_IPN;
  ez.Repack(net, layer_idx);
  net->ReallocateInvertedIndices();
  net->ComputeInvertedIndices(6);
}

static void DoDeepen(const string &input_file, const string &output_file) {
  std::unique_ptr<Network> input_net{Network::ReadNetworkBinary(input_file)};

  ArcFour rc(StringPrintf("%lld,%lld,%lld",
                          (int64)time(nullptr),
                          input_net->rounds,
                          input_net->Bytes()));

  input_net->StructuralCheck();

  CHECK(LAYER_TO_COPY < input_net->layers.size()) << "LAYER_TO_COPY out of bounds?";

  std::unique_ptr<Network> output_net{DeepenNetwork(&rc, *input_net, LAYER_TO_COPY)};
  CHECK(output_net.get() != nullptr);

  DensifyLayer(&rc, output_net.get(), LAYER_TO_COPY + 1);

  printf("Structural check...\n");
  output_net->StructuralCheck();
  output_net->SaveNetworkBinary(output_file);
}

int main(int argc, char **argv) {
  CHECK(argc >= 2) << "\n\nUsage:\ndeepen.exe net.val [output.val]";

  const string infile = argv[1];
  const string outfile = argc > 2 ? argv[2] : "net-deepened.val";

  DoDeepen(infile, outfile);
  return 0;
}

