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


int main(int argc, char **argv) {
  SimpleTests(NetworkTestUtil::SingleSparse());
  SimpleTests(NetworkTestUtil::SingleDense());
  SimpleTests(NetworkTestUtil::SingleConvolution());
  SimpleTests(NetworkTestUtil::TwoInputSparse());
  SimpleTests(NetworkTestUtil::TwoDenseChunks());
  SimpleTests(NetworkTestUtil::Net1());

  printf("OK\n");
  return 0;
}
