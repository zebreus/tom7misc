
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
// NUM_FEATURES, an integer giving the number of features in
//   a LAYER_CONVOLUTION_ARRAY layer. For sparse and dense layers,
//   this is ignored (but should be defined to 1 or whatever). Must
//   divide the number of nodes in the layer.
// SPAN_START and SPAN_SIZE, integers giving the input span.
// CHUNK_START, an integer giving the start position of the output
//   in the output array (after selecting the example's output array).
// SRC_LAYER_SIZE and DST_LAYER_SIZE, integers giving the number of
//   nodes in the full source and destination layers. The actual arrays
//   passed have num_examples of these.

// We don't actually need to know the number of nodes within the kernel;
// the global id just tells us which node we work on. But the number
// of indices per node is needed to compute offsets.
__kernel void ForwardChunkSparse(
                // size layers[src_layer].num_nodes for each example
                __global const float *restrict previous_layer_outputs,
                // size chunk.num_nodes * INDICES_PER_NODE.
                __global const int *restrict indices,
                // size chunk.num_nodes * INDICES_PER_NODE; parallel
                // to the previous.
                __global const float *restrict weights,
                // size chunk.num_nodes.
                __global const float *restrict bias,
                // size layers[dst_layer].num_nodes for each example
                __global float *restrict output_values) {
  const int node_idx = get_global_id(0);
  const int example_idx = get_global_id(1);

  const int src_start = example_idx * SRC_LAYER_SIZE;
  const int dst_start = example_idx * DST_LAYER_SIZE + CHUNK_START;

  // Start with bias.
  float potential = bias[node_idx];
  const __global float *my_weights = weights + (node_idx * INDICES_PER_NODE);
  // Note that for sparse chunks, these are already global indices into
  // the previous layer, so we can ignore SPAN_START.
  const __global int *my_indices = indices + (node_idx * INDICES_PER_NODE);

  for (int i = 0; i < INDICES_PER_NODE; i++) {
    // Fetch this first since we'll do a data-dependent load from this index.
    const int in_idx = my_indices[i];
    const float w = my_weights[i];
    const float v = previous_layer_outputs[src_start + in_idx];
    // potential += w * v;
    potential = fma(w, v, potential);
  }
  output_values[dst_start + node_idx] = FORWARD(potential);
}

// Dense version. Here we can read the indices in order without any
// indirection, which is a lot faster.
__kernel void ForwardChunkDense(
                // size layers[src_layer].num_nodes
                __global const float *restrict previous_layer_outputs,
                // unused indices array
                __global const int *restrict indices_unused,
                // size chunk.num_nodes * INDICES_PER_NODE
                __global const float *restrict weights,
                // size chunk.num_nodes
                __global const float *restrict bias,
                // size chunk.num_nodes. This is a sub-buffer pointing
                // to the interior of the destination layer's output
                // stimulation, so that output_values[0] is the first
                // output of the chunk.
                __global float *restrict output_values) {
  const int node_idx = get_global_id(0);
  const int example_idx = get_global_id(1);

  const int src_start = example_idx * SRC_LAYER_SIZE;
  const int dst_start = example_idx * DST_LAYER_SIZE + CHUNK_START;

  // Start with bias.
  float potential = bias[node_idx];
  const __global float *my_weights = weights + (node_idx * INDICES_PER_NODE);

  // For dense layers, SPAN_SIZE == INDICES_PER_NODE.
  for (int i = 0; i < INDICES_PER_NODE; i++) {
    const float w = my_weights[i];
    const float v = previous_layer_outputs[src_start + SPAN_START + i];
    // potential += w * v;
    potential = fma(w, v, potential);
  }

  output_values[dst_start + node_idx] = FORWARD(potential);
}

// PERF: Consider having this loop over all the features (i.e., pass
// an index into indices) rather than deriving where we are for each
// node.
//
// PERF: Could also derive the indices programmatically, instead of
// reading them from memory. If we do this, we should probably generate
// the OpenCL code?
__kernel void ForwardChunkConvolutional(
                // size layers[src_layer].num_nodes
                __global const float *restrict previous_layer_outputs,
                // size chunk.num_nodes * INDICES_PER_NODE.
                __global const int *restrict indices,
                // size INDICES_PER_NODE * NUM_CONVOLUTIONS.
                __global const float *restrict weights,
                // size NUM_CONVOLUTIONS.
                __global const float *restrict bias,
                // size chunk.num_nodes. This is a sub-buffer pointing
                // to the interior of the destination layer's output
                // stimulation, so that output_values[0] is the first
                // output of the chunk.
                __global float *restrict output_values) {

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
  float potential = bias[feature_number];
  // Weights are also shared.
  const __global float *feature_weights =
    weights + (feature_number * INDICES_PER_NODE);

  // These are already global indices into the previous layer, so
  // we can ignore SPAN_START.
  const __global int *my_indices =
    indices + (occurrence_number * INDICES_PER_NODE);

  for (int i = 0; i < INDICES_PER_NODE; i++) {
    const int in_idx = my_indices[i];
    const float w = feature_weights[i];
    const float v = previous_layer_outputs[src_start + in_idx];
    // potential += w * v;
    potential = fma(w, v, potential);
  }
  output_values[dst_start + node_idx] = FORWARD(potential);
}
