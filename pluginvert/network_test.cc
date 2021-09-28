#include "network.h"

#include <cmath>

#include "base/logging.h"

// Note: Evaluates a and b a second time if check fails!
#define CHECK_FEQ(a, b) CHECK(fabs((a) - (b)) < 0.00001) \
  << #a " = " << (a) << " vs " #b " = " << (b)

static void TestSimple() {
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
  net.NaNCheck("test");

  CHECK(net.layers.size() == 2);
  CHECK(net.layers[0].chunks.size() == 1);
  CHECK(net.layers[1].chunks.size() == 2);
  
  Stimulation stim(net);

  CHECK(stim.values[0].size() == 3);
  stim.values[0] = {3.0, 5.0, 7.0};
  net.RunForward(&stim);

  stim.NaNCheck("test stim");
  
  // No change to input
  CHECK(stim.values[0][0] == 3.0);
  CHECK(stim.values[0][1] == 5.0);
  CHECK(stim.values[0][2] == 7.0);  

  const std::vector<float> &out = stim.values[1];
  CHECK(out.size() == real_layer.num_nodes);

  // dense chunk (identity transfer function)
  CHECK_FEQ(out[0], -100.0 + 2.0 * 3.0 + 3.0 * 5.0);
  CHECK_FEQ(out[1], -200.0 + 4.0 * 3.0 + 5.0 * 5.0);  

  // sparse chunk (leaky relu)
  CHECK_FEQ(out[2], 0.01f * (-1000.0 + 10.0 * 7.0));
  CHECK_FEQ(out[3], 0.01f * (-2000.0 + 70.0 * 5.0));
}


int main(int argc, char **argv) {
  TestSimple();

  printf("OK\n");
  return 0;
}
