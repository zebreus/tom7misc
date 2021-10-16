
#ifndef _NETWORK_TEST_UTIL_H
#define _NETWORK_TEST_UTIL_H

#include <vector>
#include <functional>
#include <string>

#include "network.h"

// Note: Evaluates a and b a second time if check fails!
#define CHECK_FEQ(a, b) CHECK(fabs((a) - (b)) < 0.00001) \
  << #a " = " << (a) << " vs " #b " = " << (b)

// Same, but for vectors of floats.
#define CHECK_FEQV(aav, bbv) do {                           \
    auto av = (aav), bv = (bbv);                            \
    CHECK_EQ(av.size(), bv.size()) <<                       \
      av.size() << " vs " << bv.size();                     \
    for (size_t i = 0; i < av.size(); i++)                  \
      CHECK_FEQ(av[i], bv[i]) <<                            \
        "\n" #aav "[" << i << "] vs " #bbv "[" << i << "]"; \
  } while (0)

struct NetworkTestUtil {

  struct TestExample {
    std::string name;
    std::vector<float> input;
    std::vector<float> output;
  };

  struct TestNet {
    std::string name;
    Network net;
    std::vector<TestExample> examples;
  };

  // A network structure that should be able to learn the given
  // function. (And its input/output shape should match the function.)
  struct TrainNet {
    std::string name;
    // Weights and biases initialized to zero.
    Network net;
    std::function<std::vector<float>(const std::vector<float>&)> f;
    // If true, inputs should be only 0.0f or 1.0f.
    bool boolean_problem = false;

    int NumInputs() const;
    int NumOutputs() const;
  };

  // Trivial network with just one node, sparse chunk.
  static TestNet SingleSparse();
  // Trivial network with just one node, dense chunk.
  static TestNet SingleDense();
  // Trivial network with one 1x1 "convolutional" chunk.
  static TestNet SingleConvolution();

  // Input size 2; output is a single node 2a + 3b + 1.
  static TestNet TwoInputSparse();

  // One input, two outputs in separate dense chunks.
  // computes 5a - 7a.
  static TestNet TwoDenseChunks();

  static TestNet Net1();

  // Nontrivial network with two dense layers (one chunk each).
  // input  a0, b0
  // hidden a1 = leaky(.5 + 2a0 - 3b0)   b1 = leaky(-1 - 1a0 + .25b0)
  // out    a2 = leaky(0 + 1.5a1 + 1b0)  b2 = leaky(1 - 0.25a1 + 3b1)
  static TestNet TwoDenseLayers();

  // TODO: Need some tests with convolutions!

  // F(x) = x. One sparse node.
  static TrainNet LearnTrivialIdentitySparse();
  static TrainNet LearnTrivialIdentityDense();
  static TrainNet LearnTrivialIdentityConvolution();

  // All Boolean functions of three variables (2^(2^3) many), with two
  // dense layers.
  static TrainNet LearnBoolean();

};


#endif
