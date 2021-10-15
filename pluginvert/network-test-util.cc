
#include "network-test-util.h"

#include <functional>
#include <vector>
#include <string>

#include "network.h"
#include "base/logging.h"

static inline constexpr float Leaky(float f) {
  if (f < 0.0f) return 0.01f * f;
  return f;
}

int NetworkTestUtil::TrainNet::NumInputs() const {
  CHECK(!net.layers.empty());
  return net.layers[0].num_nodes;
}

int NetworkTestUtil::TrainNet::NumOutputs() const {
  CHECK(!net.layers.empty());
  return net.layers.back().num_nodes;
}

NetworkTestUtil::TestNet NetworkTestUtil::SingleSparse() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 1;
  input_chunk.width = 1;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk sparse_chunk;
  sparse_chunk.type = CHUNK_SPARSE;
  sparse_chunk.num_nodes = 1;
  sparse_chunk.transfer_function = IDENTITY;
  sparse_chunk.width = 1;
  sparse_chunk.height = 1;
  sparse_chunk.channels = 1;
  sparse_chunk.span_start = 0;
  sparse_chunk.span_size = 1;
  sparse_chunk.indices_per_node = 1;
  sparse_chunk.indices = {0};
  sparse_chunk.weights = {1.0};
  sparse_chunk.biases = {0.0};

  Layer input_layer;
  input_layer.num_nodes = 1;
  input_layer.chunks = {input_chunk};

  Layer real_layer;
  real_layer.num_nodes = 1;
  real_layer.chunks = {sparse_chunk};

  Network net({input_layer, real_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 2);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);

  TestExample example1{
    .name = "five",
    .input = {5.0},
    .output = {5.0},
  };

  return TestNet{
    .name = "one real layer with one sparse node, computes identity",
    .net = net,
    .examples = {example1},
  };
}

NetworkTestUtil::TestNet NetworkTestUtil::SingleDense() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 1;
  input_chunk.width = 1;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk dense_chunk;
  dense_chunk.type = CHUNK_DENSE;
  dense_chunk.num_nodes = 1;
  dense_chunk.transfer_function = IDENTITY;
  dense_chunk.width = 1;
  dense_chunk.height = 1;
  dense_chunk.channels = 1;
  dense_chunk.span_start = 0;
  dense_chunk.span_size = 1;
  dense_chunk.indices_per_node = 1;
  // indices not stored for dense chunks
  dense_chunk.indices = {};
  dense_chunk.weights = {1.0};
  dense_chunk.biases = {0.0};

  Layer input_layer;
  input_layer.num_nodes = 1;
  input_layer.chunks = {input_chunk};

  Layer real_layer;
  real_layer.num_nodes = 1;
  real_layer.chunks = {dense_chunk};

  Network net({input_layer, real_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 2);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);

  TestExample example1{
    .name = "seven",
    .input = {7.0},
    .output = {7.0},
  };

  return TestNet{
    .name = "one real layer with one dense node, computes identity",
    .net = net,
    .examples = {example1},
  };
}

NetworkTestUtil::TestNet NetworkTestUtil::SingleConvolution() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 1;
  input_chunk.width = 1;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk conv_chunk;
  conv_chunk.type = CHUNK_CONVOLUTION_ARRAY;
  conv_chunk.num_features = 1;
  conv_chunk.num_nodes = 1;
  conv_chunk.transfer_function = IDENTITY;
  conv_chunk.width = 1;
  conv_chunk.height = 1;
  conv_chunk.channels = 1;
  conv_chunk.span_start = 0;
  conv_chunk.span_size = 1;
  conv_chunk.indices_per_node = 1;
  // A trivial 1x1 convolution over a 1x1 rectangle.
  conv_chunk.pattern_width = 1;
  conv_chunk.pattern_height = 1;
  conv_chunk.src_width = 1;
  conv_chunk.src_height = 1;
  conv_chunk.occurrence_x_stride = 1;
  conv_chunk.occurrence_y_stride = 1;
  conv_chunk.num_occurrences_across = 1;
  conv_chunk.num_occurrences_down = 1;

  conv_chunk.indices = {0};
  conv_chunk.weights = {1.0};
  conv_chunk.biases = {0.0};

  Layer input_layer;
  input_layer.num_nodes = 1;
  input_layer.chunks = {input_chunk};

  Layer real_layer;
  real_layer.num_nodes = 1;
  real_layer.chunks = {conv_chunk};

  Network net({input_layer, real_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 2);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);

  TestExample example1{
    .name = "nine",
    .input = {9.0},
    .output = {9.0},
  };

  return TestNet{
    .name = "one real layer with trivial 1x1 convolution, computes identity",
    .net = net,
    .examples = {example1},
  };
}


NetworkTestUtil::TestNet NetworkTestUtil::TwoInputSparse() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 2;
  input_chunk.width = 2;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk sparse_chunk;
  sparse_chunk.type = CHUNK_SPARSE;
  sparse_chunk.num_nodes = 1;
  sparse_chunk.transfer_function = IDENTITY;
  sparse_chunk.width = 1;
  sparse_chunk.height = 1;
  sparse_chunk.channels = 1;
  sparse_chunk.span_start = 0;
  sparse_chunk.span_size = 2;
  sparse_chunk.indices_per_node = 2;
  sparse_chunk.indices = {0, 1};
  sparse_chunk.weights = {2.0, 3.0};
  sparse_chunk.biases = {1.0};

  Layer input_layer;
  input_layer.num_nodes = 2;
  input_layer.chunks = {input_chunk};

  Layer real_layer;
  real_layer.num_nodes = 1;
  real_layer.chunks = {sparse_chunk};

  Network net({input_layer, real_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 2);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);

  TestExample example1{
    .name = "eight-nine",
    .input = {8.0f, 9.0f},
    .output = {2.0f * 8.0f + 3.0f * 9.0f + 1.0f},
  };

  return TestNet{
    .name = "one node computing 2a + 3b + 1",
    .net = net,
    .examples = {example1},
  };
}


NetworkTestUtil::TestNet NetworkTestUtil::TwoDenseChunks() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 1;
  input_chunk.width = 1;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk dense_chunk1;
  dense_chunk1.type = CHUNK_DENSE;
  dense_chunk1.num_nodes = 1;
  dense_chunk1.transfer_function = IDENTITY;
  dense_chunk1.width = 1;
  dense_chunk1.height = 1;
  dense_chunk1.channels = 1;
  dense_chunk1.span_start = 0;
  dense_chunk1.span_size = 1;
  dense_chunk1.indices_per_node = 1;
  // indices not stored for dense chunks
  dense_chunk1.indices = {};
  dense_chunk1.weights = {5.0f};
  dense_chunk1.biases = {1.0f};

  Chunk dense_chunk2 = dense_chunk1;
  dense_chunk2.weights = {-7.0f};
  dense_chunk2.biases = {2.0f};

  Layer input_layer;
  input_layer.num_nodes = 1;
  input_layer.chunks = {input_chunk};

  Layer real_layer;
  real_layer.num_nodes = 2;
  real_layer.chunks = {dense_chunk1, dense_chunk2};

  Network net({input_layer, real_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 2);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 2);

  TestExample example1{
    .name = "three",
    .input = {3.0f},
    .output = {5.0f * 3.0f + 1.0f, -7.0f * 3.0f + 2.0f},
  };

  return TestNet{
    .name = "two dense chunks, computing (5a + 1, -7a + 2)",
    .net = net,
    .examples = {example1},
  };
}


NetworkTestUtil::TestNet NetworkTestUtil::Net1() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 3;
  input_chunk.width = 3;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk dense_chunk =
    Network::MakeDenseChunk(2,
                            // Span
                            0, 2,
                            //
                            IDENTITY);
  dense_chunk.weights = {2.0, 3.0, 4.0, 5.0};
  dense_chunk.biases = {-100.0, -200.0};

  Chunk sparse_chunk;
  sparse_chunk.type = CHUNK_SPARSE;
  sparse_chunk.num_nodes = 2;
  sparse_chunk.transfer_function = LEAKY_RELU;
  sparse_chunk.width = 2;
  sparse_chunk.height = 1;
  sparse_chunk.channels = 1;
  sparse_chunk.span_start = 1;
  sparse_chunk.span_size = 2;
  sparse_chunk.indices_per_node = 1;
  sparse_chunk.indices = {2, 1};
  sparse_chunk.weights = {10.0, 70.0};
  sparse_chunk.biases = {-1000.0, -2000.0};

  Layer input_layer;
  input_layer.num_nodes = 3;
  input_layer.chunks = {input_chunk};

  Layer real_layer;
  real_layer.num_nodes = 4;
  real_layer.chunks = {dense_chunk, sparse_chunk};

  Network net({input_layer, real_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 2);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 2);

  TestExample example1{
    .name = "one",
    .input = {3.0, 5.0, 7.0},
    .output = {
      // dense chunk (identity transfer function)
      -100.0 + 2.0 * 3.0 + 3.0 * 5.0,
      -200.0 + 4.0 * 3.0 + 5.0 * 5.0,
      // sparse chunk (leaky relu)
      0.01f * (-1000.0 + 10.0 * 7.0),
      0.01f * (-2000.0 + 70.0 * 5.0),
    },
  };

  return TestNet{
    .name = "one real layer, dense id and sparse leaky relu chunks",
    .net = net,
    .examples = {example1},
  };
}

NetworkTestUtil::TestNet NetworkTestUtil::TwoDenseLayers() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 2;
  input_chunk.width = 2;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  // hidden a1 = leaky_relu(.5 + 2a0 - 3b0) b1 = leaky_relu(-1 - 1a0 + .25b0)
  Chunk dense_chunk1;
  dense_chunk1.type = CHUNK_DENSE;
  dense_chunk1.num_nodes = 2;
  dense_chunk1.transfer_function = LEAKY_RELU;
  dense_chunk1.width = 2;
  dense_chunk1.height = 1;
  dense_chunk1.channels = 1;
  dense_chunk1.span_start = 0;
  dense_chunk1.span_size = 2;
  dense_chunk1.indices_per_node = 2;
  dense_chunk1.indices = {};
  dense_chunk1.weights = {2.0f, -3.0f, -1.0f, 0.25f};
  dense_chunk1.biases = {0.5f, -1.0f};

  // hidden a2 = leaky(0 + 1.5a1 + b1)  b2 = leaky(1 - 0.25a1 + 3b1)
  Chunk dense_chunk2;
  dense_chunk2.type = CHUNK_DENSE;
  dense_chunk2.num_nodes = 2;
  dense_chunk2.transfer_function = LEAKY_RELU;
  dense_chunk2.width = 2;
  dense_chunk2.height = 1;
  dense_chunk2.channels = 1;
  dense_chunk2.span_start = 0;
  dense_chunk2.span_size = 2;
  dense_chunk2.indices_per_node = 2;
  dense_chunk2.indices = {};
  dense_chunk2.weights = {1.5f, 1.0f, -0.25f, 3.0f};
  dense_chunk2.biases = {0.0f, 1.0f};

  Layer input_layer;
  input_layer.num_nodes = 2;
  input_layer.chunks = {input_chunk};

  Layer real_layer1;
  real_layer1.num_nodes = 2;
  real_layer1.chunks = {dense_chunk1};

  Layer real_layer2;
  real_layer2.num_nodes = 2;
  real_layer2.chunks = {dense_chunk2};

  Network net({input_layer, real_layer1, real_layer2});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 3);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);
  CHECK(net.layers[2].chunks.size() == 1);

  constexpr auto F = [](float a0, float b0) {
      const float a1 = Leaky(0.5f + 2.0f * a0 - 3.0f * b0);
      const float b1 = Leaky(-1.0f - a0 + 0.25f * b0);

      const float a2 = Leaky(1.5f * a1 + b1);
      const float b2 = Leaky(1.0f - 0.25f * a1 + 3.0f * b1);

      return std::vector<float>{{a2, b2}};
    };

  TestExample example1{
    .name = "zeroes",
    .input = {0.0f, 0.0f},
    .output = F(0.0f, 0.0f),
  };

  TestExample example2{
    .name = "negpos",
    .input = {-1.0f, 1.0f},
    .output = F(-1.0f, 1.0f),
  };

  return TestNet{
    .name = "two dense layers, each one chunk of width two",
    .net = net,
    .examples = {example1, example2},
  };
}

NetworkTestUtil::TrainNet NetworkTestUtil::LearnTrivialIdentitySparse() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 1;
  input_chunk.width = 1;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk sparse_chunk;
  sparse_chunk.type = CHUNK_SPARSE;
  sparse_chunk.num_nodes = 1;
  sparse_chunk.transfer_function = IDENTITY;
  sparse_chunk.width = 1;
  sparse_chunk.height = 1;
  sparse_chunk.channels = 1;
  sparse_chunk.span_start = 0;
  sparse_chunk.span_size = 1;
  sparse_chunk.indices_per_node = 1;
  sparse_chunk.indices = {0};
  sparse_chunk.weights = {0.0};
  sparse_chunk.biases = {0.0};

  Layer input_layer;
  input_layer.num_nodes = 1;
  input_layer.chunks = {input_chunk};

  Layer real_layer;
  real_layer.num_nodes = 1;
  real_layer.chunks = {sparse_chunk};

  Network net({input_layer, real_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 2);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);

  auto f = [](const std::vector<float> &input) {
      CHECK(input.size() == 1);
      return input;
    };

  return TrainNet{
    .name = "F(x) = x, one sparse node",
    .net = net,
    .f = f,
    .boolean_problem = false,
  };
}
