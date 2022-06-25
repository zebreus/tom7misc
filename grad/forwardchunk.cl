
// TODO: Fix docs to reflect that this runs over all examples in the
// training layer.

// Runs a network forward to produce the next layer of the stimulation,
// either as part of training or just inference. In the general case a
// layer is made up of multiple chunks with different parameters, so
// the kernels in here actually run an individual chunk. This code
// is compiled for each chunk in the network, baking in some constants
// (supplied as #defines) for performance and selecting the chunk type
// by name.

// Expects the following defines:

// FORWARD, the transfer function.
// INDICES_PER_NODE, an integer giving the number of output indices per
//   node.
// SPAN_START and SPAN_SIZE, integers giving the input span.
// CHUNK_START, an integer giving the start position of the output
//   in the output array (after selecting the example's output array).
// SRC_LAYER_SIZE and DST_LAYER_SIZE, integers giving the number of
//   nodes in the full source and destination layers. The actual arrays
//   passed have num_examples of these.
// For convolutional layers, the following must be defined to their
// corresponding fields in the chunk struct. These are ignored for
// sparse and dense layers but should be set to 1 to avoid compilation
// errors:
//   NUM_FEATURES, NUM_OCCURRENCES_ACROSS, NUM_OCCURRENCES_DOWN,
//   PATTERN_WIDTH, PATTERN_HEIGHT,
//   OCCURRENCE_X_STRIDE, OCCURRENCE_Y_STRIDE, SRC_WIDTH

// We don't actually need to know the number of nodes within the kernel;
// the global id just tells us which node we work on. But the number
// of indices per node is needed to compute offsets.
__kernel void ForwardChunkSparse(
                // size layers[src_layer].num_nodes for each example
                __global const half *restrict previous_layer_outputs,
                // size chunk.num_nodes * INDICES_PER_NODE.
                __global const int *restrict indices,
                // size chunk.num_nodes * INDICES_PER_NODE; parallel
                // to the previous.
                __global const half *restrict weights,
                // size chunk.num_nodes.
                __global const half *restrict bias,
                // size layers[dst_layer].num_nodes for each example
                __global half *restrict output_values) {
  const int node_idx = get_global_id(0);
  const int example_idx = get_global_id(1);

  const int src_start = example_idx * SRC_LAYER_SIZE;
  const int dst_start = example_idx * DST_LAYER_SIZE + CHUNK_START;

  // Start with bias.
  float potential = vload_half(node_idx, bias);
  const __global half *my_weights = weights + (node_idx * INDICES_PER_NODE);
  // Note that for sparse chunks, these are already global indices into
  // the previous layer, so we can ignore SPAN_START.
  const __global int *my_indices = indices + (node_idx * INDICES_PER_NODE);

  for (int i = 0; i < INDICES_PER_NODE; i++) {
    // Fetch this first since we'll do a data-dependent load from this index.
    const int in_idx = my_indices[i];
    const float w = vload_half(i, my_weights);
    const float v = vload_half(src_start + in_idx, previous_layer_outputs);
    // potential += w * v;
    potential = fma(w, v, potential);
  }

  const float out = FORWARD(potential);
  vstore_half(out, dst_start + node_idx, output_values);
}

// Dense version. Here we can read the indices in order without any
// indirection, which is a lot faster.
__kernel void ForwardChunkDense(
                // size layers[src_layer].num_nodes
                __global const half *restrict previous_layer_outputs,
                // unused indices array
                __global const int *restrict indices_unused,
                // size chunk.num_nodes * INDICES_PER_NODE
                __global const half *restrict weights,
                // size chunk.num_nodes
                __global const half *restrict bias,
                // size chunk.num_nodes. This is a sub-buffer pointing
                // to the interior of the destination layer's output
                // stimulation, so that output_values[0] is the first
                // output of the chunk.
                __global half *restrict output_values) {
  const int node_idx = get_global_id(0);
  const int example_idx = get_global_id(1);

  const int src_start = example_idx * SRC_LAYER_SIZE;
  const int dst_start = example_idx * DST_LAYER_SIZE + CHUNK_START;

  // Start with bias.
  float potential = vload_half(node_idx, bias);
  const __global half *my_weights = weights + (node_idx * INDICES_PER_NODE);

  // For dense layers, SPAN_SIZE == INDICES_PER_NODE.
  for (int i = 0; i < INDICES_PER_NODE; i++) {
    const float w = vload_half(i, my_weights);
    const float v = vload_half(src_start + SPAN_START + i,
                               previous_layer_outputs);
    // potential += w * v;
    potential = fma(w, v, potential);
  }

  const float out = FORWARD(potential);
  vstore_half(out, dst_start + node_idx, output_values);
}

// This version derives the indices programmatically, which reduces memory
// traffic and data-dependent reads.
__kernel void ForwardChunkConvolutional(
                // size layers[src_layer].num_nodes
                __global const half *restrict previous_layer_outputs,
                // size chunk.num_nodes * INDICES_PER_NODE.
                __global const int *restrict indices_unused,
                // size INDICES_PER_NODE * NUM_CONVOLUTIONS.
                __global const half *restrict weights,
                // size NUM_CONVOLUTIONS.
                __global const half *restrict bias,
                // size chunk.num_nodes. This is a sub-buffer pointing
                // to the interior of the destination layer's output
                // stimulation, so that output_values[0] is the first
                // output of the chunk.
                __global half *restrict output_values) {

  // Note: I tried making this a 2D kernel but it was measurably worse.
  const int node_idx = get_global_id(0);
  // (Hopefully avoiding integer division since the denominator is a
  // compile-time constant.)
  // PERF quotrem?
  const int feature_number = node_idx % NUM_FEATURES;
  const int occurrence_number = node_idx / NUM_FEATURES;

  const int example_idx = get_global_id(1);

  const int src_start = example_idx * SRC_LAYER_SIZE;
  const int dst_start = example_idx * DST_LAYER_SIZE + CHUNK_START;


  // Start with bias; shared by all the nodes in this convolution.
  float potential = vload_half(feature_number, bias);
  // Weights are also shared.
  const __global half *feature_weights =
    weights + (feature_number * INDICES_PER_NODE);

  // These are already global indices into the previous layer, so
  // we can ignore SPAN_START.
  // const __global int *my_indices =
  //     indices + (occurrence_number * INDICES_PER_NODE);

  // Common for these to be 1D. Dividing or modding by
  // ACROSS=1 is easy for the compiler to do, but help it know
  // that these expressions are trivial if DOWN=1.
  const int occ_row =
    NUM_OCCURRENCES_DOWN == 1 ?
    0 :
    (occurrence_number / NUM_OCCURRENCES_ACROSS);
  const int occ_col =
    NUM_OCCURRENCES_DOWN == 1 ?
    occurrence_number :
    (occurrence_number % NUM_OCCURRENCES_ACROSS);

  const int src_row = occ_row * OCCURRENCE_Y_STRIDE;
  const int src_col = occ_col * OCCURRENCE_X_STRIDE;

  // Generate indices for the occurrence the same way we do in
  // MakeConvolutionArrayIndices.
  const int src_start_offset = src_row * SRC_WIDTH + src_col;

  // index into weights. PERF: could also be derived from x,y
  int i = 0;
  for (int y = 0; y < PATTERN_HEIGHT; y++) {
    // PERF: Some manual strength reduction is possible here,
    // but OpenCL can probably do it? (Typically it is unrolling
    // this entire loop!)
    // Always the adjacent row.
    int src_offset = src_start_offset + (y * SRC_WIDTH);
    for (int x = 0; x < PATTERN_WIDTH; x++) {
      // Absolute index into the layer, but ignoring example num.
      const int in_idx = SPAN_START + src_offset;
      const float w = vload_half(i, feature_weights);
      const float v = vload_half(src_start + in_idx,
                                 previous_layer_outputs);

      // potential += w * v;
      potential = fma(w, v, potential);

      src_offset++;
      i++;
    }
  }

  const float out = FORWARD(potential);
  vstore_half(out, dst_start + node_idx, output_values);
}
