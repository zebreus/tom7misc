#include "network.h"

#include <cmath>
#include <memory>

#include "base/logging.h"
#include "network-test-util.h"

using TestNet = NetworkTestUtil::TestNet;
using TestExample = NetworkTestUtil::TestExample;

static void SimpleTests(TestNet test_net) {
  printf("======\n"
         "Testing %s:\n", test_net.name.c_str());
  const Network &net = test_net.net;

  auto StimTests = [&](const Network &n) {
      Stimulation stim(n);

      for (const TestExample &example : test_net.examples) {
        if (test_net.examples.size() != 1) {
          printf("Example: %s\n", example.name.c_str());
        }

        CHECK(stim.values[0].size() == n.layers[0].num_nodes);
        stim.values[0] = example.input;
        n.RunForward(&stim);

        stim.NaNCheck(example.name);

        // No change to input
        CHECK(stim.values[0] == example.input);

        const std::vector<float> &out = stim.values.back();
        CHECK(out.size() == n.layers.back().num_nodes);

        CHECK_FEQV(out, example.output);
      }
    };

  StimTests(net);

  std::vector<uint8_t> bytes1 = net.Serialize();

  // warning for "large" weights and biases expected here; the values
  // in some test networks exceed the threshold values
  std::unique_ptr<Network> net2(Network::ParseSerialized(bytes1, false));
  CHECK(net2.get() != nullptr);
  std::vector<uint8_t> bytes2 = net2->Serialize();
  CHECK(bytes1 == bytes2) << "Serialization should be deterministic, "
    "and Serialize and ParseSerialize should be inverses.";

  // Check that the deserialized network also works!
  StimTests(*net2);
}

// TODO: Test inverted indices computation, referring to this old
// comment (pre-chunk):

// The value here gives the index into the indices/weights vectors
// for the next layer. If for each index i within the span (defined
// by inverted_indices[layer].start[z]) for node id z
// let gidx = inverted_indices[layer].output_indices[i]
// and then layers[layer].indices[gidx] == z. (The same for the weight
// vector gives us the weight, which is the point, and dividing
// by INDICES_PER_NODE gives us the output node.) As such, this is
// a permutation of 0..(num_nodes[ii] * layers[ii].indices_per_node - 1).

#if 0
// XXX to tests
void Network::CheckInvertedIndices() const {
  for (int layer = 0; layer < num_layers; layer++) {
    const vector<uint32> &indices = layers[layer].indices;
    const LayerType dst_layer_type = layers[layer].type;
    const Network::InvertedIndices &inv = inverted_indices[layer];

    CHECK_EQ(inv.output_indices.size(), indices.size());
    // Need one start/length pair for every node in the source layer.
    CHECK_EQ(num_nodes[layer], inv.start.size());
    CHECK_EQ(num_nodes[layer], inv.length.size());
    // But the output size is determined by the next layer.
    if (dst_layer_type == CHUNK_CONVOLUTION_ARRAY) {
      const int dst_layer_nodes = num_nodes[layer + 1];
      CHECK(dst_layer_nodes % layers[layer].num_features == 0);
      CHECK_EQ((dst_layer_nodes / layers[layer].num_features) *
               layers[layer].indices_per_node,
               inv.output_indices.size());
    } else {
      CHECK_EQ(num_nodes[layer + 1] * layers[layer].indices_per_node,
               inv.output_indices.size());
    }
    // z is a node id from the src layer.
    for (int z = 0; z < inv.start.size(); z++) {
      // i is the index within the compacted inverted index.
      for (int i = inv.start[z]; i < inv.start[z] + inv.length[z]; i++) {
        // Global index into 'indices'.
        CHECK(i >= 0);
        CHECK(i < inv.output_indices.size());

        const int gidx = inv.output_indices[i];
        CHECK(gidx >= 0);
        CHECK(gidx < indices.size());
        // This should map back to our current node id.
        // (Should be true for sparse, dense, and convolution layers.)
        CHECK_EQ(indices[gidx], z);
      }
    }
  }
}

#endif


int main(int argc, char **argv) {
  SimpleTests(NetworkTestUtil::SingleSparse());
  SimpleTests(NetworkTestUtil::SingleDense());
  SimpleTests(NetworkTestUtil::SingleConvolution());
  SimpleTests(NetworkTestUtil::TwoInputSparse());
  SimpleTests(NetworkTestUtil::TwoDenseChunks());
  SimpleTests(NetworkTestUtil::Net1());
  SimpleTests(NetworkTestUtil::TwoDenseLayers());

  printf("OK\n");
  return 0;
}
