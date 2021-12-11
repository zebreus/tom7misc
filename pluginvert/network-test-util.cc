
#include "network-test-util.h"

#include <functional>
#include <vector>
#include <string>

#include "network.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"

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

static void ForceWeightUpdateAdamOrYogi(NetworkTestUtil::TrainNet *net,
                                        WeightUpdate wu) {
  CHECK(wu == ADAM || wu == YOGI);
  for (Layer &layer : net->net.layers) {
    for (Chunk &chunk : layer.chunks) {
      // Only do this for real layers.
      if (chunk.type != CHUNK_INPUT) {
        // And only if the weight update method is currently SGD.
        if (chunk.weight_update == SGD) {
          // Wrong for these to have data for SGD, and
          // resize only sets new elements to zero.
          CHECK(chunk.weights_aux.empty());
          CHECK(chunk.biases_aux.empty());

          chunk.weights_aux.resize(chunk.weights.size() * 2, 0.0f);
          chunk.biases_aux.resize(chunk.biases.size() * 2, 0.0f);
        } else if (chunk.weight_update == ADAM ||
                   chunk.weight_update == YOGI) {
          CHECK(chunk.weights_aux.size() == chunk.weights.size() * 2);
          CHECK(chunk.biases_aux.size() == chunk.biases.size() * 2);
          // Same aux size for these two.
        }
        chunk.weight_update = wu;
      }
    }
  }
}

NetworkTestUtil::TrainNet NetworkTestUtil::ForceAdam(TrainNet net) {
  ForceWeightUpdateAdamOrYogi(&net, ADAM);
  net.name += " (Force Adam)";
  return net;
}

NetworkTestUtil::TrainNet NetworkTestUtil::ForceYogi(TrainNet net) {
  ForceWeightUpdateAdamOrYogi(&net, YOGI);
  net.name += " (Force Yogi)";
  return net;
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
  sparse_chunk.weight_update = SGD;

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

NetworkTestUtil::TestNet NetworkTestUtil::FixedSingle() {
  TestNet net = SingleSparse();
  net.name = "one sparse identity node, fixed";
  net.net.layers[1].chunks[0].fixed = true;
  return net;
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
  dense_chunk.weight_update = SGD;

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
  conv_chunk.weight_update = SGD;

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
  sparse_chunk.weight_update = SGD;

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
  dense_chunk1.weight_update = SGD;

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

NetworkTestUtil::TestNet NetworkTestUtil::SimpleConv() {
  // 3x2 convolution on 4x3 input.
  Chunk input;
  input.type = CHUNK_INPUT;
  input.num_nodes = 12;
  // Conv chunks use the pattern width/height, and ignore these.
  input.width = 1;
  input.height = 12;
  input.channels = 1;

  // One feature:
  // 3  2 1
  // 4 -5 6
  //  -100
  Chunk conv;
  conv.type = CHUNK_CONVOLUTION_ARRAY;
  conv.num_features = 1;
  conv.occurrence_x_stride = 1;
  conv.occurrence_y_stride = 1;
  conv.pattern_width = 3;
  conv.pattern_height = 2;
  conv.src_width = 4;
  conv.src_height = 3;
  conv.transfer_function = IDENTITY;
  conv.span_start = 0;
  conv.span_size = 12;
  conv.indices_per_node = 6;

  const auto [indices, this_num_nodes,
              num_occurrences_across, num_occurrences_down] =
    Network::MakeConvolutionArrayIndices(0, 12,
                                         conv.num_features,
                                         conv.pattern_width,
                                         conv.pattern_height,
                                         conv.src_width,
                                         conv.src_height,
                                         conv.occurrence_x_stride,
                                         conv.occurrence_y_stride);
  conv.num_nodes = this_num_nodes;
  conv.width = conv.num_nodes;
  conv.height = 1;
  conv.channels = 1;
  CHECK(num_occurrences_across == 2);
  CHECK(num_occurrences_down == 2);
  conv.num_occurrences_across = num_occurrences_across;
  conv.num_occurrences_down = num_occurrences_down;
  conv.indices = indices;
  CHECK(conv.indices.size() ==
        3 * 2 *
        num_occurrences_across * num_occurrences_down);
  // One feature:
  // 3  2 1
  // 4 -5 6
  //  -100
  conv.weights = {3.0f, 2.0f, 1.0f,
                  4.0f, -5.0f, 6.0f};
  conv.biases = {-100.0f};

  Network net({Network::LayerFromChunks({input}),
               Network::LayerFromChunks({conv})});
  net.NaNCheck(__func__);

  TestExample example1{
    .name = "zero",
    .input = {0.0, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 0.0},
    .output = {
      -100, -100,
      -100, -100,
    },
  };

  TestExample example2{
    .name = "one-hot",
    .input = {0.0, 1.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 0.0},
    .output = {
      -98, -97,
      -100, -100,
    },
  };

  TestExample example3{
    .name = "nwse-corners",
    .input = {1.0, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 1.0},
    .output = {
      -97, -100,
      -100, -94,
    },
  };

  TestExample example4{
    .name = "nesw-corners",
    .input = {0.0, 0.0, 0.0, 2.0,
              0.0, 0.0, 0.0, 0.0,
              3.0, 0.0, 0.0, 0.0},
    .output = {
      -100, -98,
      -88, -100,
    },
  };

  // One feature:
  // 3  2 1
  // 4 -5 6
  //  -100
  TestExample example5{
    .name = "uniform",
    .input = {1.0, 1.0, 1.0, 1.0,
              1.0, 1.0, 1.0, 1.0,
              1.0, 1.0, 1.0, 1.0},
    .output = {
      -89, -89,
      -89, -89,
    },
  };

  // Note: I used straightforward net cc code
  // as reference; didn't compute expected output
  // manually.
  TestExample example6{
    .name = "values",
    .input = {-0.6,  0.3, 0.9,  0.7,
               1.0, -2.0, 2.5,  0.6,
               1.1,  0.2, 0.4, -1.0},
    .output = {
      -71.3, -113.5,
      -92.7, -107.6,
    },
  };

  return TestNet{
    .name = "simple 3x2 convolution, overlapping",
    .net = net,
    .examples = {example1, example2, example3, example4,
                 example5, example6},
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
                            IDENTITY,
                            SGD);
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
  sparse_chunk.weight_update = SGD;

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

  net.rounds = int64_t{0x80000001};
  net.examples = int64_t{1} << 35;

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
  dense_chunk1.weight_update = SGD;

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
  dense_chunk2.weight_update = SGD;

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

NetworkTestUtil::TestNet NetworkTestUtil::Copy() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 8;
  input_chunk.width = 8;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk copy1 = Network::MakeCopyChunk(0, 3);
  Chunk copy2 = Network::MakeCopyChunk(3, 5);

  Network net({Network::LayerFromChunks({input_chunk}),
               Network::LayerFromChunks({copy1, copy2})});
  net.NaNCheck(__func__);

  return TestNet{
    .name = "Copies 8 inputs, using two chunks via MakeCopyChunk",
    .net = net,
    .examples = std::vector<TestExample>{
      TestExample{
        .name = "zeroes",
        .input = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,},
        .output = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,},
      },
      TestExample{
        .name = "values",
        .input = {-3.0f, 1.1f, 1.3f, -8.1f, 0.1f, 0.3f, 0.1f, -1.0f,},
        .output = {-3.0f, 1.1f, 1.3f, -8.1f, 0.1f, 0.3f, 0.1f, -1.0f,},
      },
    },
  };
}

NetworkTestUtil::TestNet NetworkTestUtil::CountInternalEdges() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 8;
  input_chunk.width = 8;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk one = Network::Make1DConvolutionChunk(
      0, 8,
      // two 2x1 features: 0-1 and 1-0 transition
      2, 2,
      // overlapping
      1,
      SIGMOID, ADAM);
  CHECK(one.indices.size() > 3);
  CHECK(one.indices[0] == 0);
  CHECK(one.indices[1] == 1);
  CHECK(one.indices[2] == 1) << one.indices[2];
  // Two features, two indices per node.
  CHECK(one.weights.size() == 4);
  // First feature counts 0-1  (~A & B)
  // Sigmoids output ~1.0 if the pattern matches, otherwise ~0.0.
  one.weights[0] = -1000000.0f;
  one.weights[1] =   100000.0f;
  // And 1-0 (A & ~B)
  one.weights[2] =   100000.0f;
  one.weights[3] = -1000000.0f;

  CHECK(one.biases.size() == 2);
  one.biases[0] = -100.0f;
  one.biases[1] = -100.0f;

  CHECK(one.num_nodes == 7 * 2);

  Chunk two = Network::MakeDenseChunk(1, 0, 7 * 2, IDENTITY, SGD);
  // Sum 'em up.
  for (float &f : two.weights) f = 1.0f;

  Network net({Network::LayerFromChunks({input_chunk}),
               Network::LayerFromChunks({one}),
               Network::LayerFromChunks({two})});
  net.NaNCheck(__func__);

  return TestNet{
    .name = "Counts internal 0-1 and 1-0 transitions, with conv and dense",
    .net = net,
    .examples = std::vector<TestExample>{
      TestExample{
        .name = "zeroes",
        .input = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,},
        .output = {0.0f},
      },
      TestExample{
        .name = "ones",
        .input = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,},
        .output = {0.0f},
      },
      TestExample{
        .name = "oneoff",
        .input = {1.f, 1.f, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f,},
        .output = {2.0f},
      },
      TestExample{
        .name = "onespanoff",
        .input = {1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1.f, 1.f,},
        .output = {2.0f},
      },
      TestExample{
        .name = "leftside",
        .input = {0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,},
        .output = {1.0f},
      },
      TestExample{
        .name = "rightside",
        .input = {0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,},
        .output = {1.0f},
      },
      TestExample{
        .name = "pattern",
        .input = {0.f, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 0.f,},
        .output = {4.0f},
      },
      TestExample{
        .name = "pattern2",
        .input = {1.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1.f, 0.f,},
        .output = {7.0f},
      },
    },
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
  sparse_chunk.weight_update = SGD;

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
    .boolean_input = false,
    .boolean_output = false,
  };
}

NetworkTestUtil::TrainNet NetworkTestUtil::LearnTrivialIdentityDense() {
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
  dense_chunk.indices = {};
  dense_chunk.weights = {0.0};
  dense_chunk.biases = {0.0};
  dense_chunk.weight_update = SGD;

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

  auto f = [](const std::vector<float> &input) {
      CHECK(input.size() == 1);
      return input;
    };

  return TrainNet{
    .name = "F(x) = x, one dense node",
    .net = net,
    .f = f,
    .boolean_input = false,
    .boolean_output = false,
  };
}

NetworkTestUtil::TrainNet NetworkTestUtil::LearnTrivialIdentityConvolution() {
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = 1;
  input_chunk.width = 1;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk conv_chunk;
  conv_chunk.type = CHUNK_CONVOLUTION_ARRAY;
  conv_chunk.num_nodes = 1;
  conv_chunk.num_features = 1;
  conv_chunk.num_occurrences_across = 1;
  conv_chunk.num_occurrences_down = 1;
  conv_chunk.occurrence_x_stride = 1;
  conv_chunk.occurrence_y_stride = 1;
  conv_chunk.pattern_width = 1;
  conv_chunk.pattern_height = 1;
  conv_chunk.src_width = 1;
  conv_chunk.src_height = 1;
  conv_chunk.transfer_function = IDENTITY;
  conv_chunk.width = 1;
  conv_chunk.height = 1;
  conv_chunk.channels = 1;
  conv_chunk.span_start = 0;
  conv_chunk.span_size = 1;
  conv_chunk.indices_per_node = 1;
  conv_chunk.indices = {0};
  conv_chunk.weights = {0.0};
  conv_chunk.biases = {0.0};
  conv_chunk.weight_update = SGD;

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

  auto f = [](const std::vector<float> &input) {
      CHECK(input.size() == 1);
      return input;
    };

  return TrainNet{
    .name = "F(x) = x, one 1x1 convolution node",
    .net = net,
    .f = f,
    .boolean_input = false,
    .boolean_output = false,
  };
}


NetworkTestUtil::TrainNet NetworkTestUtil::LearnBoolean() {
  ArcFour rc("learn-boolean");

  static constexpr int INPUT_SIZE = 3;
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  // How small can this layer be?
  static constexpr int LAYER1_SPARSE_SIZE = 64;
  static constexpr int LAYER1_ID_SIZE = 6;
  static constexpr int LAYER1_SIZE = LAYER1_ID_SIZE + LAYER1_SPARSE_SIZE;
  // Copy of first layer (sparse identity), and then its negation.
  Chunk id_chunk;
  id_chunk.type = CHUNK_SPARSE;
  id_chunk.num_nodes = LAYER1_ID_SIZE;
  id_chunk.transfer_function = IDENTITY;
  id_chunk.width = LAYER1_ID_SIZE;
  id_chunk.height = 1;
  id_chunk.channels = 1;
  id_chunk.span_start = 0;
  id_chunk.span_size = INPUT_SIZE;
  id_chunk.indices_per_node = 1;
  id_chunk.indices = {0, 1, 2, 0, 1, 2};
  id_chunk.weights = {1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f};
  id_chunk.biases = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
  id_chunk.weight_update = SGD;

  Chunk sparse_chunk1;
  sparse_chunk1.type = CHUNK_SPARSE;
  sparse_chunk1.num_nodes = LAYER1_SPARSE_SIZE;
  sparse_chunk1.transfer_function = LEAKY_RELU;
  sparse_chunk1.width = LAYER1_SPARSE_SIZE;
  sparse_chunk1.height = 1;
  sparse_chunk1.channels = 1;
  sparse_chunk1.span_start = 0;
  sparse_chunk1.span_size = INPUT_SIZE;
  sparse_chunk1.indices_per_node = 2;
  for (int i = 0; i < LAYER1_SPARSE_SIZE; i++) {
    // Leave out one node.
    switch (i % 3) {
    default:
    case 0:
      sparse_chunk1.indices.push_back(0);
      sparse_chunk1.indices.push_back(1);
      break;
    case 1:
      sparse_chunk1.indices.push_back(0);
      sparse_chunk1.indices.push_back(2);
      break;
    case 2:
      sparse_chunk1.indices.push_back(1);
      sparse_chunk1.indices.push_back(2);
      break;
    }
  }
  sparse_chunk1.weights = std::vector<float>(
      LAYER1_SPARSE_SIZE * sparse_chunk1.indices_per_node,
      0.0f);
  sparse_chunk1.biases = std::vector<float>(LAYER1_SPARSE_SIZE, 0.0f);
  sparse_chunk1.weight_update = SGD;

  static constexpr int LAYER2_SIZE = 200; // 256
  static constexpr int IPN2 = 12;
  Chunk sparse_chunk2;
  sparse_chunk2.type = CHUNK_SPARSE;
  sparse_chunk2.num_nodes = LAYER2_SIZE;
  sparse_chunk2.transfer_function = LEAKY_RELU;
  sparse_chunk2.width = LAYER2_SIZE;
  sparse_chunk2.height = 1;
  sparse_chunk2.channels = 1;
  sparse_chunk2.span_start = 0;
  sparse_chunk2.span_size = LAYER1_SIZE;
  sparse_chunk2.indices_per_node = IPN2;

  {
    // Keep permuting sparse part and taking prefix so that we don't
    // generate duplicate indices.
    std::vector<int> sparse_part;
    for (int i = 0; i < LAYER1_SPARSE_SIZE; i++)
      sparse_part.push_back(i);

    for (int n = 0; n < LAYER2_SIZE; n++) {
      // All get a copy of the input.
      for (int i = 0; i < LAYER1_ID_SIZE; i++)
        sparse_chunk2.indices.push_back(i);

      Shuffle(&rc, &sparse_part);
      std::vector<int> rest;
      rest.reserve(IPN2 - LAYER1_ID_SIZE);
      for (int i = 0; i < IPN2 - LAYER1_ID_SIZE; i++) {
        rest.push_back(LAYER1_ID_SIZE + sparse_part[i]);
      }
      std::sort(rest.begin(), rest.end());

      for (int idx : rest)
        sparse_chunk2.indices.push_back(idx);
    }
  }
  sparse_chunk2.weights = std::vector<float>(LAYER2_SIZE * IPN2, 0.0f);
  sparse_chunk2.biases = std::vector<float>(LAYER2_SIZE, 0.0f);
  sparse_chunk2.weight_update = SGD;

  static constexpr int LAYER3_SIZE = 256;
  Chunk dense_chunk3;
  dense_chunk3.type = CHUNK_DENSE;
  dense_chunk3.num_nodes = LAYER3_SIZE;
  dense_chunk3.transfer_function = SIGMOID;
  dense_chunk3.width = LAYER3_SIZE;
  dense_chunk3.height = 1;
  dense_chunk3.channels = 1;
  dense_chunk3.span_start = 0;
  dense_chunk3.span_size = LAYER2_SIZE;
  dense_chunk3.indices_per_node = LAYER2_SIZE;
  dense_chunk3.indices = {};
  dense_chunk3.weights = std::vector<float>(LAYER3_SIZE * LAYER2_SIZE, 0.0f);
  dense_chunk3.biases = std::vector<float>(LAYER3_SIZE, 0.0f);
  dense_chunk3.weight_update = SGD;

  Layer input_layer;
  input_layer.num_nodes = INPUT_SIZE;
  input_layer.chunks = {input_chunk};

  Layer real_layer1;
  real_layer1.num_nodes = LAYER1_SIZE;
  real_layer1.chunks = {id_chunk, sparse_chunk1};

  Layer real_layer2;
  real_layer2.num_nodes = LAYER2_SIZE;
  real_layer2.chunks = {sparse_chunk2};

  Layer real_layer3;
  real_layer3.num_nodes = LAYER3_SIZE;
  real_layer3.chunks = {dense_chunk3};

  Network net({input_layer, real_layer1, real_layer2, real_layer3});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 4);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 2);
  CHECK(net.layers[2].chunks.size() == 1);
  CHECK(net.layers[3].chunks.size() == 1);

  // Random permutation of [0, 256).
  // I used this to see if a weird behavior (which turned out
  // to be a bug) was related to the structure of the output.
  // This can probably be removed, although enabling it should
  // also work!
  [[maybe_unused]]
  static constexpr std::array<int, 256> PERM = {
    161, 211, 255, 42, 34, 6, 38, 49, 234, 81, 244, 72, 98, 223,
    219, 91, 195, 61, 95, 88, 175, 14, 17, 87, 116, 178, 143, 233,
    157, 97, 29, 60, 250, 251, 140, 93, 108, 212, 207, 64, 162,
    134, 0, 237, 99, 177, 57, 124, 151, 48, 94, 22, 67, 191, 188,
    129, 25, 101, 21, 229, 221, 131, 169, 173, 96, 112, 46, 117,
    153, 31, 132, 196, 18, 113, 232, 5, 142, 114, 136, 13, 79,
    192, 58, 200, 227, 235, 37, 193, 230, 183, 245, 120, 186, 10,
    115, 74, 82, 20, 40, 119, 242, 19, 238, 71, 2, 59, 149, 76,
    187, 109, 181, 45, 247, 240, 228, 80, 23, 107, 69, 152, 32, 3,
    125, 214, 210, 150, 167, 199, 249, 184, 83, 102, 182, 126,
    141, 220, 138, 68, 194, 209, 15, 47, 11, 146, 44, 51, 56, 122,
    165, 41, 12, 180, 156, 103, 85, 236, 65, 213, 77, 62, 8, 163,
    203, 63, 139, 24, 254, 243, 33, 174, 201, 128, 179, 9, 92, 4,
    84, 208, 147, 43, 205, 154, 226, 137, 176, 189, 127, 104, 198,
    224, 241, 160, 78, 216, 168, 170, 197, 121, 248, 164, 133, 70,
    100, 90, 246, 171, 53, 106, 75, 30, 123, 52, 159, 225, 215,
    239, 148, 185, 145, 206, 110, 155, 105, 166, 7, 26, 54, 218,
    39, 55, 252, 217, 231, 66, 204, 16, 73, 1, 135, 158, 253, 172,
    27, 222, 35, 202, 28, 89, 118, 50, 86, 36, 144, 130, 190, 111,
  };
  static constexpr bool PERMUTE = false;

  auto f = [](const std::vector<float> &input) {
      CHECK(input.size() == 3);
      static_assert(INPUT_SIZE == 3);
      const bool a = input[0] > 0.5f;
      const bool b = input[1] > 0.5f;
      const bool c = input[2] > 0.5f;
      std::vector<float> out;
      static_assert(LAYER3_SIZE == 256);
      out.reserve(256);
      for (int i = 0; i < 256; i++) {
        // The truth table is a 2 * 2 * 2 cube with 8 cells.
        // There are 2^8 ways to assign truth values to this.
        // The 8 bits of i give us the values in each cell,
        // so just figure out which bit to look at.
        const int bit = (a ? 4 : 0) | (b ? 2 : 0) | (c ? 1 : 0);
        const int permi = PERMUTE ? PERM[i] : i;
        const bool value = 0 != (permi & (1 << bit));
        out.push_back(value ? 1.0f : 0.0f);
      }
      return out;
    };

  return TrainNet{
    .name = "All 256 boolean functions of 3 variables. "
      "Sparse and dense chunks; three hidden layers.",
    .net = net,
    .f = f,
    .boolean_input = true,
    .boolean_output = true,
  };
}

NetworkTestUtil::TrainNet NetworkTestUtil::LearnCountOnesDense() {
  constexpr int INPUT_SIZE = 80;
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  // Single dense node to compute sum
  Chunk dense_chunk;
  dense_chunk.type = CHUNK_DENSE;
  dense_chunk.num_nodes = 1;
  dense_chunk.transfer_function = LEAKY_RELU;
  dense_chunk.width = 1;
  dense_chunk.height = 1;
  dense_chunk.channels = 1;
  dense_chunk.span_start = 0;
  dense_chunk.span_size = INPUT_SIZE;
  dense_chunk.indices_per_node = INPUT_SIZE;
  dense_chunk.indices = {};
  dense_chunk.weights = std::vector<float>(
      dense_chunk.num_nodes * INPUT_SIZE, 0.0f);
  dense_chunk.biases = std::vector<float>(dense_chunk.num_nodes, 0.0f);

  Layer input_layer;
  input_layer.num_nodes = input_chunk.num_nodes;
  input_layer.chunks = {input_chunk};

  Layer dense_layer;
  dense_layer.num_nodes = dense_chunk.num_nodes;
  dense_layer.chunks = {dense_chunk};

  Network net({input_layer, dense_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 2);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);

  auto f = [](const std::vector<float> &input) {
      int bits = 0;
      for (int i = 0; i < input.size(); i++) {
        bool a = input[i] > 0.5f;
        if (a) bits++;
      }
      std::vector<float> out;
      out.push_back((float)bits);
      return out;
    };

  return TrainNet{
    .name = "Dense layer counts 1-bits",
    .net = net,
    .f = f,
    .boolean_input = true,
    .boolean_output = false,
  };
}

NetworkTestUtil::TrainNet NetworkTestUtil::LearnCountOnesConvDense() {
  constexpr int INPUT_SIZE = 80;
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  // Convolution to count 4x1 segments
  Chunk conv_chunk;
  conv_chunk.type = CHUNK_CONVOLUTION_ARRAY;
  conv_chunk.num_features = 1;
  // overlapping.
  conv_chunk.occurrence_x_stride = 4;
  conv_chunk.occurrence_y_stride = 1;
  conv_chunk.pattern_width = 4;
  conv_chunk.pattern_height = 1;
  conv_chunk.src_width = INPUT_SIZE;
  conv_chunk.src_height = 1;
  conv_chunk.transfer_function = LEAKY_RELU;
  conv_chunk.span_start = 0;
  conv_chunk.span_size = INPUT_SIZE;
  conv_chunk.indices_per_node = 4;

  const auto [indices, this_num_nodes,
              num_occurrences_across, num_occurrences_down] =
    Network::MakeConvolutionArrayIndices(0, INPUT_SIZE,
                                         conv_chunk.num_features,
                                         conv_chunk.pattern_width,
                                         conv_chunk.pattern_height,
                                         conv_chunk.src_width,
                                         conv_chunk.src_height,
                                         conv_chunk.occurrence_x_stride,
                                         conv_chunk.occurrence_y_stride);
  CHECK(this_num_nodes == INPUT_SIZE / 4);
  conv_chunk.num_nodes = this_num_nodes;
  conv_chunk.width = conv_chunk.num_nodes;
  conv_chunk.height = 1;
  conv_chunk.channels = 1;

  conv_chunk.num_occurrences_across = num_occurrences_across;
  conv_chunk.num_occurrences_down = num_occurrences_down;
  conv_chunk.indices = indices;

  conv_chunk.weights = std::vector<float>(
      conv_chunk.indices_per_node * conv_chunk.num_features,
      0.0f);
  conv_chunk.biases = std::vector<float>(conv_chunk.num_features, 0.0f);

  // Single dense node to compute sum
  Chunk dense_chunk;
  dense_chunk.type = CHUNK_DENSE;
  dense_chunk.num_nodes = 1;
  dense_chunk.transfer_function = LEAKY_RELU;
  dense_chunk.width = 1;
  dense_chunk.height = 1;
  dense_chunk.channels = 1;
  dense_chunk.span_start = 0;
  dense_chunk.span_size = conv_chunk.num_nodes;
  dense_chunk.indices_per_node = conv_chunk.num_nodes;
  dense_chunk.indices = {};
  dense_chunk.weights = std::vector<float>(
      dense_chunk.num_nodes * conv_chunk.num_nodes, 0.0f);
  dense_chunk.biases = std::vector<float>(dense_chunk.num_nodes, 0.0f);

  Layer input_layer;
  input_layer.num_nodes = input_chunk.num_nodes;
  input_layer.chunks = {input_chunk};

  Layer conv_layer;
  conv_layer.num_nodes = conv_chunk.num_nodes;
  conv_layer.chunks = {conv_chunk};

  Layer dense_layer;
  dense_layer.num_nodes = dense_chunk.num_nodes;
  dense_layer.chunks = {dense_chunk};

  Network net({input_layer, conv_layer, dense_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 3);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);
  CHECK(net.layers[2].chunks.size() == 1);

  auto f = [](const std::vector<float> &input) {
      int bits = 0;
      for (int i = 0; i < input.size(); i++) {
        bool a = input[i] > 0.5f;
        if (a) bits++;
      }
      std::vector<float> out;
      out.push_back((float)bits);
      return out;
    };

  return TrainNet{
    .name = "4x1 convolution and then dense layer counts 1-bits",
    .net = net,
    .f = f,
    .boolean_input = true,
    .boolean_output = false,
  };
}

NetworkTestUtil::TrainNet NetworkTestUtil::LearnCountOnesConvConvDense(
    bool fix_dense_layer) {
  constexpr int INPUT_SIZE = 80;
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  // Convolution to count 4x1 segments
  Chunk conv_chunk1;
  conv_chunk1.type = CHUNK_CONVOLUTION_ARRAY;
  conv_chunk1.num_features = 1;
  conv_chunk1.occurrence_x_stride = 4;
  conv_chunk1.occurrence_y_stride = 1;
  conv_chunk1.pattern_width = 4;
  conv_chunk1.pattern_height = 1;
  conv_chunk1.src_width = INPUT_SIZE;
  conv_chunk1.src_height = 1;
  conv_chunk1.transfer_function = LEAKY_RELU;
  conv_chunk1.span_start = 0;
  conv_chunk1.span_size = INPUT_SIZE;
  conv_chunk1.indices_per_node = 4;

  {
    const auto [indices, this_num_nodes,
                num_occurrences_across, num_occurrences_down] =
      Network::MakeConvolutionArrayIndices(0, INPUT_SIZE,
                                           conv_chunk1.num_features,
                                           conv_chunk1.pattern_width,
                                           conv_chunk1.pattern_height,
                                           conv_chunk1.src_width,
                                           conv_chunk1.src_height,
                                           conv_chunk1.occurrence_x_stride,
                                           conv_chunk1.occurrence_y_stride);
    CHECK(this_num_nodes == INPUT_SIZE / 4);
    conv_chunk1.num_nodes = this_num_nodes;
    conv_chunk1.width = conv_chunk1.num_nodes;
    conv_chunk1.height = 1;
    conv_chunk1.channels = 1;

    conv_chunk1.num_occurrences_across = num_occurrences_across;
    conv_chunk1.num_occurrences_down = num_occurrences_down;
    conv_chunk1.indices = indices;

    conv_chunk1.weights = std::vector<float>(
        conv_chunk1.indices_per_node * conv_chunk1.num_features,
        0.0f);
    conv_chunk1.biases = std::vector<float>(conv_chunk1.num_features, 0.0f);
  }

  // Then 1x5 segments
  Chunk conv_chunk2;
  conv_chunk2.type = CHUNK_CONVOLUTION_ARRAY;
  conv_chunk2.num_features = 1;
  conv_chunk2.occurrence_x_stride = 1;
  conv_chunk2.occurrence_y_stride = 5;
  conv_chunk2.pattern_width = 1;
  conv_chunk2.pattern_height = 5;
  conv_chunk2.src_width = 1;
  conv_chunk2.src_height = conv_chunk1.num_nodes;
  conv_chunk2.transfer_function = LEAKY_RELU;
  conv_chunk2.span_start = 0;
  conv_chunk2.span_size = conv_chunk1.num_nodes;
  conv_chunk2.indices_per_node = 5;

  {
    const auto [indices, this_num_nodes,
                num_occurrences_across, num_occurrences_down] =
      Network::MakeConvolutionArrayIndices(0, conv_chunk1.num_nodes,
                                           conv_chunk2.num_features,
                                           conv_chunk2.pattern_width,
                                           conv_chunk2.pattern_height,
                                           conv_chunk2.src_width,
                                           conv_chunk2.src_height,
                                           conv_chunk2.occurrence_x_stride,
                                           conv_chunk2.occurrence_y_stride);
    CHECK(this_num_nodes == conv_chunk1.num_nodes / 5);
    conv_chunk2.num_nodes = this_num_nodes;
    conv_chunk2.width = 1;
    conv_chunk2.height = conv_chunk2.num_nodes;
    conv_chunk2.channels = 1;

    conv_chunk2.num_occurrences_across = num_occurrences_across;
    conv_chunk2.num_occurrences_down = num_occurrences_down;
    conv_chunk2.indices = indices;

    conv_chunk2.weights = std::vector<float>(
        conv_chunk2.indices_per_node * conv_chunk2.num_features,
        0.0f);
    conv_chunk2.biases = std::vector<float>(conv_chunk2.num_features, 0.0f);
  }

  // Single dense node to compute sum
  Chunk dense_chunk;
  dense_chunk.type = CHUNK_DENSE;
  dense_chunk.num_nodes = 1;
  dense_chunk.transfer_function = LEAKY_RELU;
  dense_chunk.width = 1;
  dense_chunk.height = 1;
  dense_chunk.channels = 1;
  dense_chunk.span_start = 0;
  dense_chunk.span_size = conv_chunk2.num_nodes;
  dense_chunk.indices_per_node = conv_chunk2.num_nodes;
  dense_chunk.indices = {};

  // Initialize to the correct solution, but if this is not marked fixed
  // because of the arg, it gets randomized.
  dense_chunk.weights = std::vector<float>(
      dense_chunk.num_nodes * conv_chunk2.num_nodes, 1.0f);
  dense_chunk.biases = std::vector<float>(dense_chunk.num_nodes, 0.0f);
  dense_chunk.fixed = fix_dense_layer;

  Layer input_layer;
  input_layer.num_nodes = input_chunk.num_nodes;
  input_layer.chunks = {input_chunk};

  Layer conv_layer1;
  conv_layer1.num_nodes = conv_chunk1.num_nodes;
  conv_layer1.chunks = {conv_chunk1};

  Layer conv_layer2;
  conv_layer2.num_nodes = conv_chunk2.num_nodes;
  conv_layer2.chunks = {conv_chunk2};

  Layer dense_layer;
  dense_layer.num_nodes = dense_chunk.num_nodes;
  dense_layer.chunks = {dense_chunk};

  Network net({input_layer, conv_layer1, conv_layer2, dense_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 4);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);
  CHECK(net.layers[2].chunks.size() == 1);
  CHECK(net.layers[3].chunks.size() == 1);

  auto f = [](const std::vector<float> &input) {
      int bits = 0;
      for (int i = 0; i < input.size(); i++) {
        bool a = input[i] > 0.5f;
        if (a) bits++;
      }
      std::vector<float> out;
      out.push_back((float)bits);
      return out;
    };

  return TrainNet{
    .name =
      StringPrintf("4x1, then 5x1 conv, then small dense%s counts 1-bits",
                   fix_dense_layer ? " (fixed)" : ""),
    .net = net,
    .f = f,
    .boolean_input = true,
    .boolean_output = false,
  };
}


NetworkTestUtil::TrainNet NetworkTestUtil::LearnCountEdges() {
  constexpr int INPUT_SIZE = 80;
  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  Chunk conv_chunk;
  conv_chunk.type = CHUNK_CONVOLUTION_ARRAY;
  // one feature for 0-1 transition, one for 1-0
  conv_chunk.num_features = 2;
  // overlapping.
  conv_chunk.occurrence_x_stride = 1;
  conv_chunk.occurrence_y_stride = 1;
  conv_chunk.pattern_width = 2;
  conv_chunk.pattern_height = 1;
  conv_chunk.src_width = INPUT_SIZE;
  conv_chunk.src_height = 1;
  conv_chunk.transfer_function = LEAKY_RELU;
  conv_chunk.span_start = 0;
  conv_chunk.span_size = INPUT_SIZE;
  conv_chunk.indices_per_node = 2;

  {
    const auto [indices, this_num_nodes,
                num_occurrences_across, num_occurrences_down] =
      Network::MakeConvolutionArrayIndices(0, INPUT_SIZE,
                                           conv_chunk.num_features,
                                           conv_chunk.pattern_width,
                                           conv_chunk.pattern_height,
                                           conv_chunk.src_width,
                                           conv_chunk.src_height,
                                           conv_chunk.occurrence_x_stride,
                                           conv_chunk.occurrence_y_stride);
    conv_chunk.num_nodes = this_num_nodes;
    conv_chunk.width = conv_chunk.num_nodes;
    conv_chunk.height = 1;
    conv_chunk.channels = 1;

    conv_chunk.num_occurrences_across = num_occurrences_across;
    conv_chunk.num_occurrences_down = num_occurrences_down;
    conv_chunk.indices = indices;

    conv_chunk.weights = std::vector<float>(
        conv_chunk.indices_per_node * conv_chunk.num_features,
        0.0f);
    conv_chunk.biases = std::vector<float>(conv_chunk.num_features, 0.0f);
  }

  // To actually implement a & ~b and ~a & b I think we need two layers.
  // So convolve again, but as a simple 1x1 window.
  Chunk conv2_chunk;
  conv2_chunk.type = CHUNK_CONVOLUTION_ARRAY;
  conv2_chunk.num_features = 1;
  conv2_chunk.occurrence_x_stride = 1;
  conv2_chunk.occurrence_y_stride = 1;
  conv2_chunk.pattern_width = 1;
  conv2_chunk.pattern_height = 1;
  conv2_chunk.src_width = conv_chunk.num_nodes;
  conv2_chunk.src_height = 1;
  conv2_chunk.transfer_function = LEAKY_RELU;
  conv2_chunk.span_start = 0;
  conv2_chunk.span_size = conv_chunk.num_nodes;
  conv2_chunk.indices_per_node = 1;

  {
    const auto [indices, this_num_nodes,
                num_occurrences_across, num_occurrences_down] =
      Network::MakeConvolutionArrayIndices(0, conv_chunk.num_nodes,
                                           conv2_chunk.num_features,
                                           conv2_chunk.pattern_width,
                                           conv2_chunk.pattern_height,
                                           conv2_chunk.src_width,
                                           conv2_chunk.src_height,
                                           conv2_chunk.occurrence_x_stride,
                                           conv2_chunk.occurrence_y_stride);
    conv2_chunk.num_nodes = this_num_nodes;
    conv2_chunk.width = conv2_chunk.num_nodes;
    conv2_chunk.height = 1;
    conv2_chunk.channels = 1;

    conv2_chunk.num_occurrences_across = num_occurrences_across;
    conv2_chunk.num_occurrences_down = num_occurrences_down;
    conv2_chunk.indices = indices;

    conv2_chunk.weights = std::vector<float>(
        conv2_chunk.indices_per_node * conv2_chunk.num_features,
        0.0f);
    conv2_chunk.biases = std::vector<float>(conv2_chunk.num_features, 0.0f);
  }

  // Single dense node to compute sum
  Chunk dense_chunk;
  dense_chunk.type = CHUNK_DENSE;
  dense_chunk.num_nodes = 1;
  dense_chunk.transfer_function = LEAKY_RELU;
  dense_chunk.width = 1;
  dense_chunk.height = 1;
  dense_chunk.channels = 1;
  dense_chunk.span_start = 0;
  dense_chunk.span_size = conv2_chunk.num_nodes;
  dense_chunk.indices_per_node = conv2_chunk.num_nodes;
  dense_chunk.indices = {};
  dense_chunk.weights = std::vector<float>(
      dense_chunk.num_nodes * dense_chunk.indices_per_node, 0.0f);
  dense_chunk.biases = std::vector<float>(dense_chunk.num_nodes, 0.0f);

  Layer input_layer;
  input_layer.num_nodes = input_chunk.num_nodes;
  input_layer.chunks = {input_chunk};

  Layer conv_layer;
  conv_layer.num_nodes = conv_chunk.num_nodes;
  conv_layer.chunks = {conv_chunk};

  Layer conv2_layer;
  conv2_layer.num_nodes = conv2_chunk.num_nodes;
  conv2_layer.chunks = {conv2_chunk};

  Layer dense_layer;
  dense_layer.num_nodes = dense_chunk.num_nodes;
  dense_layer.chunks = {dense_chunk};

  Network net({input_layer, conv_layer, conv2_layer, dense_layer});
  net.NaNCheck(__func__);

  CHECK(net.layers.size() == 4);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 1);
  CHECK(net.layers[2].chunks.size() == 1);
  CHECK(net.layers[3].chunks.size() == 1);

  auto f = [](const std::vector<float> &input) {
      int edges = 0;
      for (int i = 0; i < input.size() - 1; i++) {
        bool a = input[i] > 0.5f;
        bool b = input[i + 1] > 0.5f;
        // 0-1 or 1-0
        if (a != b) edges++;
      }
      std::vector<float> out;
      out.push_back((float)edges);
      return out;
    };

  return TrainNet{
    .name = "2x1 convolution + 1x1 conv counts (interior) 0-1 and 1-0 edges",
    .net = net,
    .f = f,
    .boolean_input = true,
    .boolean_output = false,
  };
}
