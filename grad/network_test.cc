#include "network.h"

#include <set>
#include <string>
#include <cmath>
#include <memory>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "network-test-util.h"
#include "arcfour.h"

using TestNet = NetworkTestUtil::TestNet;
using TestExample = NetworkTestUtil::TestExample;

static constexpr bool VERBOSE = true;

static std::vector<half> FloatsToHalves(const std::vector<float> &fs) {
  std::vector<half> hs(fs.size());
  for (int i = 0; i < fs.size(); i++) hs[i] = fs[i];
  return hs;
}

static std::vector<float> HalvesToFloats(const std::vector<half> &hs) {
  std::vector<float> fs(hs.size());
  for (int i = 0; i < hs.size(); i++) fs[i] = hs[i];
  return fs;
}

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
        CHECK(example.input.size() == n.layers[0].num_nodes);

        const std::vector<half> input_half =
          FloatsToHalves(example.input);
        stim.values[0] = input_half;
        n.RunForward(&stim);

        if (VERBOSE) {
          printf("Stim with %d layers:\n", (int)stim.values.size());
          for (int i = 0; i < stim.values.size(); i++) {
            printf("Stim Layer %d:\n  ", i);
            for (half f : stim.values[i]) {
              printf("%.3f ", (float)f);
            }
            printf("\n");
          }
        }

        stim.NaNCheck(example.name);

        // No change to input
        CHECK(stim.values[0] == input_half);

        const std::vector<half> &out = stim.values.back();
        CHECK(out.size() == n.layers.back().num_nodes);

        const std::vector<float> out_float = HalvesToFloats(out);
        CHECK_FEQV(out_float, example.output);
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
  CHECK(net.rounds == net2->rounds);
  CHECK(net.examples == net2->examples);
  CHECK(net.layers.size() == net2->layers.size());
  for (int i = 0; i < net.layers.size(); i++) {
    CHECK(net.layers[i].num_nodes == net2->layers[i].num_nodes);
    CHECK(net.layers[i].chunks.size() ==
          net2->layers[i].chunks.size());
    for (int c = 0; c < net.layers[i].chunks.size(); c++) {
      const Chunk &chunk1 = net.layers[i].chunks[c];
      const Chunk &chunk2 = net2->layers[i].chunks[c];
      // XXX test more stuff here...
      CHECK(chunk1.fixed == chunk2.fixed);
      CHECK_FEQV(chunk1.weights, chunk2.weights);
      CHECK_FEQV(chunk1.biases, chunk2.biases);
    }
  }

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

static void TestSparseChunk() {
  const string seed = StringPrintf("x.%lld", time(nullptr));
  ArcFour rc(seed);

  {
    Chunk sparse = Network::MakeRandomSparseChunk(
        &rc,
        3,
        {
          Network::SparseSpan{.span_start = 1,
                              .span_size = 3,
                              .ipn = 2},
          Network::SparseSpan{.span_start = 4,
                              .span_size = 2,
                              .ipn = 2},
        },
        IDENTITY, SGD);
    CHECK(sparse.num_nodes == 3);
    CHECK(sparse.transfer_function == IDENTITY);
    CHECK(sparse.weight_update == SGD);
    CHECK(sparse.type == CHUNK_SPARSE);
    CHECK(sparse.indices_per_node == 4);
    CHECK(sparse.biases.size() == 3);
    CHECK(sparse.weights.size() == 3 * 4);
    CHECK(sparse.span_start == 1);
    CHECK(sparse.span_size == 5);
    CHECK(sparse.width == 3);

    for (int i = 0; i < 3; i++) {
      std::set<int> ids;
      auto Has = [&ids](int d) {
          return ids.find(d) != ids.end();
        };
      for (int j = 0; j < 4; j++) {
        int id = sparse.indices[i * 4 + j];
        CHECK(!Has(id)) << id;
        ids.insert(id);
      }

      // Exactly two of the three in this span.
      CHECK((Has(1) && Has(2) && !Has(3)) ||
            (Has(1) && !Has(2) && Has(3)) ||
            (!Has(1) && Has(2) && Has(3)));

      // Sampled two from a span of size two, so
      // we must have both.
      CHECK(Has(4));
      CHECK(Has(5));
    }
  }
}

int main(int argc, char **argv) {

  SimpleTests(NetworkTestUtil::SingleSparse());
  SimpleTests(NetworkTestUtil::SingleDense());
  SimpleTests(NetworkTestUtil::SingleConvolution());
  SimpleTests(NetworkTestUtil::TwoInputSparse());
  SimpleTests(NetworkTestUtil::TwoDenseChunks());
  SimpleTests(NetworkTestUtil::SimpleConv());
  SimpleTests(NetworkTestUtil::Net1());
  SimpleTests(NetworkTestUtil::Copy());
  SimpleTests(NetworkTestUtil::TwoDenseLayers());
  SimpleTests(NetworkTestUtil::FixedSingle());
  SimpleTests(NetworkTestUtil::CountInternalEdges());

  TestSparseChunk();

  printf("OK\n");
  return 0;
}
