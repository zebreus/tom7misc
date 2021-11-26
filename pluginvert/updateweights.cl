// Update the weights and biases for each node, using
// the errors and the outputs from the previous layer.
// First pass.

// Defines:
// INDICES_PER_NODE, an int, the number of input indices
// per node on this layer.
//
// NUM_OCCURRENCES, an int giving the number of occurences
// of the pattern (occurrences_across * occurrences_down)
// for a convolutional layer. For sparse and dense layers,
// ignored but should be defined to something like 1.
//
// NUM_FEATURES, an int giving the number of features in a
// convolutional layer; should be 1 for sparse and dense.
//
// CHUNK_START, the index of this chunk within layer_error
//   (for each example).
// SPAN_START, SPAN_SIZE, the span of the input layer that we
//   read from.
// SRC_LAYER_SIZE and DST_LAYER_SIZE, the number of nodes in
//   the source and destination layers.

// NUM_WEIGHTS and NUM_BIASES, the number of weights in this
//   chunk. This is used to accumulate the output gradients
//   into the correct stripe.

// OVERWRITE_GRAD, a bool. If true, then we write the
//   gradients with = rather than accumulating with +=. This
//   saves a memory read and allows to skip clearing the
//   buffer as well.

#if OVERWRITE_GRAD
  #define ACCUMULATE(l, r) (l) = (r)
#else
  #define ACCUMULATE(l, r) (l) += (r)
#endif

// Note this kernel does not depend on the transfer function.

__kernel void UpdateWeightsSparse(
                 // full previous layer's output values;
                 // size src_layer.num_nodes per example
                 __global const float *restrict prev_layer_output,
                 // full layer's error,
                 // size dst_layer.num_nodes per example
                 __global const float *restrict layer_error,
                 // chunk.num_nodes * INDICES_PER_NODE
                 __global const int *restrict chunk_indices,
                 // chunk.num_nodes * INDICES_PER_NODE,
                 __global float *restrict weight_grads,
                 // chunk.num_nodes
                 __global float *restrict bias_grads,
                 // In [0, num_examples).
                 int example_batch_start) {
  const int chunk_node_idx = get_global_id(0);
  // In [0, W).
  const int example_num_in_batch = get_global_id(1);
  const int example_num = example_batch_start + example_num_in_batch;
  // start offset of this training example's data within the arrays
  const int src_layer_start = example_num * SRC_LAYER_SIZE;
  const int dst_layer_start = example_num * DST_LAYER_SIZE;

  const int global_node_idx = CHUNK_START + chunk_node_idx;
  const float delta_j = layer_error[dst_layer_start + global_node_idx];

  for (int input_idx = 0; input_idx < INDICES_PER_NODE; input_idx++) {
    const int edge_idx = INDICES_PER_NODE * chunk_node_idx + input_idx;
    // Offset of the node, which we use to get its output.
    // (These are already global to the previous layer, but should
    // be inside the span.)
    const int src_idx = chunk_indices[edge_idx];
    const float x_ji = prev_layer_output[src_layer_start + src_idx];

    const float grad = delta_j * x_ji;
    // PERF fma();
    ACCUMULATE(weight_grads[NUM_WEIGHTS * example_num_in_batch + edge_idx],
               grad);
  }

  const float bgrad = delta_j;
  ACCUMULATE(bias_grads[NUM_BIASES * example_num_in_batch + chunk_node_idx],
             bgrad);
}

// When the layer is dense.
__kernel void UpdateWeightsDense(
                 // full previous layer's output values
                 __global const float *restrict prev_layer_output,
                 // full layer's error, size num_nodes
                 __global const float *restrict layer_error,
                 // (unused, invalid)
                 __global const int *restrict chunk_indices_unused,
                 // chunk.num_nodes * INDICES_PER_NODE,
                 __global float *restrict weight_grads,
                 // chunk.num_nodes
                 __global float *restrict bias_grads,
                 // In [0, num_examples).
                 int example_batch_start) {
  const int chunk_node_idx = get_global_id(0);
  const int global_node_idx = CHUNK_START + chunk_node_idx;

  // In [0, W).
  const int example_num_in_batch = get_global_id(1);
  const int example_num = example_batch_start + example_num_in_batch;
  // start offset of this training example's data within the arrays
  const int src_layer_start = example_num * SRC_LAYER_SIZE;
  const int dst_layer_start = example_num * DST_LAYER_SIZE;

  const float delta_j = layer_error[dst_layer_start + global_node_idx];
  // const float learning_rate_times_delta_j = learning_rate * delta_j;

  for (int input_idx = 0; input_idx < INDICES_PER_NODE; input_idx++) {
    const int edge_idx = INDICES_PER_NODE * chunk_node_idx + input_idx;
    // For a dense layer, each source node is used in order, starting
    // at the beginning of the span.
    const int src_idx = SPAN_START + input_idx;

    const float x_ji = prev_layer_output[src_layer_start + src_idx];

    const float grad = delta_j * x_ji;
    // PERF fma();
    ACCUMULATE(weight_grads[NUM_WEIGHTS * example_num_in_batch + edge_idx],
               grad);
  }

  const float bgrad = delta_j;
  ACCUMULATE(bias_grads[NUM_BIASES * example_num_in_batch + chunk_node_idx],
             bgrad);
}

// PERF: Try doing the bias update in its own kernel, since the
// conditional approach here does unnecessary work for all but the 0th
// index (and may stall other kernels there). But it would not be
// weird if the current approach is actually faster.
__kernel void UpdateWeightsConvolutional(
                 // full previous layer's output
                 __global const float *restrict prev_layer_output,
                 // full layer's error, size num_nodes
                 __global const float *restrict layer_error,
                 // chunk.num_nodes * INDICES_PER_NODE
                 __global const int *restrict chunk_indices,
                 // NUM_FEATURES * INDICES_PER_NODE,
                 __global float *restrict weight_grads,
                 // NUM_FEATURES
                 __global float *restrict bias_grads,
                 // In [0, num_examples).
                 int example_batch_start) {
  // in 0..NUM_FEATURES-1
  const int feature_num = get_global_id(0);
  // in 0..INDICES_PER_NODE-1
  const int pidx = get_global_id(1);

  // In [0, W).
  const int example_num_in_batch = get_global_id(2);
  const int example_num = example_batch_start + example_num_in_batch;
  // start offset of this training example's data within the arrays
  const int src_layer_start = example_num * SRC_LAYER_SIZE;
  const int dst_layer_start = example_num * DST_LAYER_SIZE;

  // This is the sum of the gradient over all occurrences.
  float weight_grad = 0.0f;
  // Compute the bias no matter what, but only write it for
  // pidx 0.
  float bias_grad = 0.0f;

  // Loop over every occurrence of the feature, each of which gives
  // us a different error term.
  for (int occ = 0; occ < NUM_OCCURRENCES; occ++) {
    const int chunk_node_idx = occ * NUM_FEATURES + feature_num;
    const int global_node_idx = CHUNK_START + chunk_node_idx;
    const float delta_j = layer_error[dst_layer_start + global_node_idx];

    // Always compute bias term; all but one is thrown out.
    bias_grad += delta_j;

    // Multiple error terms, but we are only updating a single weight.
    // Offset of the node, which we use to get its output.
    // (These are already global to the input layer, but should
    // be within the span.)
    // PERF: Consider computing this index rather than reading it
    // from memory. It should be a constant offset from the start
    // position of the occurrence, which has a simple formula.
    const int src_idx = chunk_indices[occ * INDICES_PER_NODE + pidx];
    const float x_ji = prev_layer_output[src_layer_start + src_idx];
    // weight_grad += delta_j * x_ji;
    weight_grad = fma(delta_j, x_ji, weight_grad);
  }

  {
    // Update the one weight.
    const int widx = feature_num * INDICES_PER_NODE + pidx;
    // In the past I used the square root here, although it is not
    // very principled. Adam may be able to fully account for the
    // benefit I (thought I) was getting.
    weight_grad *= (1.0f / NUM_OCCURRENCES);
    // PERF fma()
    ACCUMULATE(weight_grads[NUM_WEIGHTS * example_num_in_batch + widx],
               weight_grad);
  }

  if (pidx == 0) {
    bias_grad *= (1.0f / NUM_OCCURRENCES);
    // PERF fma()
    ACCUMULATE(bias_grads[NUM_BIASES * example_num_in_batch + feature_num],
               bias_grad);
  }
}

