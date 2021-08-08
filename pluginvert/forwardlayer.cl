
// A layer is simply defined by the values of the previous layer, and
// weights of the incoming edges. Because our layers are so big (as a
// consequence of representing image data), we don't store this as
// a dense vector (it would be like (3*2^16)^2 floats; ~150GB); instead
// each node has a sparse set of its inputs from the previous layer.
//
// We have to know how many indices each node uses, as a constant.

// Expects the following defines:

// FORWARD, the transfer function.
// INDICES_PER_NODE, an integer giving the number of output indices per
//   node.
// NODES_IN_LAYER, an integer giving the total number of nodes in the
///  layer we're computing.
// NUM_CONVOLUTIONS, an integer giving the number of convolutions in
//   a LAYER_CONVOLUTION_ARRAY layer. For sparse and dense layers,
//   this is ignored (but should be defined to 1 or whatever). Must
//   divide NODES_IN_LAYER.

// We don't actually need to know the number of nodes within the kernel;
// the global id just tells us which node we work on. But the number
// of indices per node is needed to compute offsets.
__kernel void ForwardLayerSparse(
                // size num_nodes[layer]
                __global const float *restrict previous_layer_outputs,
                // size num_nodes[layer + 1] * INDICES_PER_NODE.
                __global const int *restrict indices,
                // size num_nodes[layer + 1] * INDICES_PER_NODE; parallel
                // to the previous.
                __global const float *restrict weights,
                // size num_nodes[layer + 1] (this layer).
                __global const float *restrict bias,
                // size num_nodes[layer + 1].
                __global float *restrict output_values) {
  const int node_idx = get_global_id(0);

  // Start with bias.
  float potential = bias[node_idx];
  const __global float *my_weights = weights + (node_idx * INDICES_PER_NODE);
  const __global int *my_indices = indices + (node_idx * INDICES_PER_NODE);

  // Could itself be a kernel? Not sure what the right granularity of these is.
  for (int i = 0; i < INDICES_PER_NODE; i++) {
    // Fetch this first since we'll do a data-dependent load from this index.
    const int in_idx = my_indices[i];
    const float w = my_weights[i];
    const float v = previous_layer_outputs[in_idx];
    // potential += w * v;
    potential = fma(w, v, potential);
  }
  output_values[node_idx] = FORWARD(potential);
}

// Dense version. Here we can read the indices in order without any
// indirection, which is a lot faster.
__kernel void ForwardLayerDense(
                // size num_nodes[layer]
                __global const float *restrict previous_layer_outputs,
                // unused indices array
                __global const int *restrict indices_unused,
                // size num_nodes[layer + 1] * INDICES_PER_NODE.
                __global const float *restrict weights,
                // size num_nodes[layer + 1] (this layer).
                __global const float *restrict bias,
                // size num_nodes[layer + 1].
                __global float *restrict output_values) {
  const int node_idx = get_global_id(0);

  // Start with bias.
  float potential = bias[node_idx];
  const __global float *my_weights = weights + (node_idx * INDICES_PER_NODE);

  // Could itself be a kernel? Not sure what the right granularity of these is.
  for (int i = 0; i < INDICES_PER_NODE; i++) {
    const float w = my_weights[i];
    const float v = previous_layer_outputs[i];
    // potential += w * v;
    potential = fma(w, v, potential);
  }
  output_values[node_idx] = FORWARD(potential);
}


__kernel void ForwardLayerConvolutional(
                // size num_nodes[layer]
                __global const float *restrict previous_layer_outputs,
                // size num_nodes[layer + 1] * INDICES_PER_NODE.
                __global const int *restrict indices,
                // size INDICES_PER_NODE * NUM_CONVOLUTIONS.
                __global const float *restrict weights,
                // size NUM_CONVOLUTIONS.
                __global const float *restrict bias,
                // size num_nodes[layer + 1].
                __global float *restrict output_values) {
  const int NODES_PER_CONV = NODES_IN_LAYER / NUM_CONVOLUTIONS;
  const int node_idx = get_global_id(0);
  // (Hopefully avoiding integer division since the denominator is a
  // compile-time constant.)
  const int conv_number = node_idx / NODES_PER_CONV;

  // Start with bias; shared by all the nodes in this convolution.
  float potential = bias[conv_number];
  // Weights are also shared.
  const __global float *conv_weights =
    weights + (conv_number * INDICES_PER_NODE);
  // But indices can be anything, just like in a sparse layer.
  const __global int *my_indices = indices + (node_idx * INDICES_PER_NODE);

  for (int i = 0; i < INDICES_PER_NODE; i++) {
    const int in_idx = my_indices[i];
    const float w = conv_weights[i];
    const float v = previous_layer_outputs[in_idx];
    // potential += w * v;
    potential = fma(w, v, potential);
  }
  output_values[node_idx] = FORWARD(potential);
}
