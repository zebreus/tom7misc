
#ifndef _NETWORK_TEST_UTIL_H
#define _NETWORK_TEST_UTIL_H

#include "network.h"

// Note: Evaluates a and b a second time if check fails!
#define CHECK_FEQ(a, b) CHECK(fabs((a) - (b)) < 0.00001) \
  << #a " = " << (a) << " vs " #b " = " << (b)

// Same, but for vectors of floats.
#define CHECK_FEQV(aav, bbv) do {                           \
    auto av = (aav), bv = (bbv);                            \
    CHECK_EQ(av.size(), bv.size());                         \
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

  // TODO: Need some tests with convolutions!
};


#endif
