// Update the weights and biases for each node, using
// the errors and the outputs from the previous layer.
// First pass.

// Defines:
// INDICES_PER_NODE, an int, the number of input indices
// per node on this layer.
//
// NUM_OCCURRENCES_ACROSS and NUM_OCCURRENCES_DOWN, ints
// giving the number of occurences of the pattern
// for a convolutional layer. For sparse and dense layers,
// ignored but should be defined to something like 1.
// OCCURRENCE_X_STRIDE and OCCURRENCE_Y_STRIDE, the stride
// in each axis for convolutional layers.
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
//   saves a memory read and allows the caller to skip clearing
//   the buffer as well.

// CONV_UPDATE_EXPONENT, a float nominally in [0, 1]. Scales
//   the contribution of a convolutional node by
//   1/(occ^exp) where occ is the number of occurrences.

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
                 __global const int *restrict chunk_indices_unused,
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


  // position of pidx within the pattern.
  // For the common case that the pattern is 1D, make sure that
  // the compiler is able to avoid even mod and div by a constant.
  const int px = PATTERN_HEIGHT == 1 ?
    pidx :
    (pidx % PATTERN_WIDTH);
  const int py = PATTERN_HEIGHT == 1 ?
    0 :
    (pidx / PATTERN_WIDTH);

  // index offset from the top-left corner of an occurrence
  const int poff = SRC_WIDTH * py + px;

  // Loop over every occurrence of the feature, each of which gives
  // us a different error term.
  for (int y = 0; y < NUM_OCCURRENCES_DOWN; y++) {
    for (int x = 0; x < NUM_OCCURRENCES_ACROSS; x++) {
      const int occ = y * NUM_OCCURRENCES_ACROSS + x;

      const int chunk_node_idx = occ * NUM_FEATURES + feature_num;
      const int global_node_idx = CHUNK_START + chunk_node_idx;
      const float delta_j = layer_error[dst_layer_start + global_node_idx];

      // Always compute bias term; all but one is thrown out.
      bias_grad += delta_j;

      // Multiple error terms, but we are only updating a single weight.
      // Offset of the node, which we use to get its output.
      // (These are already global to the input layer, but should
      // be within the span.)

      // index (from SPAN_START) of the top-left corner of the occurrence
      const int top_left = SRC_WIDTH * y * OCCURRENCE_Y_STRIDE +
        x * OCCURRENCE_X_STRIDE;

      // const int src_idx = chunk_indices[occ * INDICES_PER_NODE + pidx];
      const int src_idx = SPAN_START + top_left + poff;

      const float x_ji = prev_layer_output[src_layer_start + src_idx];
      // weight_grad += delta_j * x_ji;
      weight_grad = fma(delta_j, x_ji, weight_grad);
    }
  }

  // In the past I used the square root here, although it is not
  // very principled. Adam may be able to fully account for the
  // benefit I (thought I) was getting. Maybe 1.0 is actually correct?
  // This does permit large weight updates but each of these is indeed
  // a downstream contribution, right?

  // TODO: Tune this hyperparameter.
  const float multiplier =
    // These special cases are covered by the formula below, but we
    // want to simplify the expression if possible. (Looks like my
    // opencl implementation doesn't have trouble reducing this even
    // if there's a call to pow()). But it doesn't fold a multiplication
    // by exactly 1.0 below!
    CONV_UPDATE_EXPONENT == 1.0f ? (1.0f / (NUM_OCCURRENCES_ACROSS * NUM_OCCURRENCES_DOWN)) :
    CONV_UPDATE_EXPONENT == 0.0f ? 1.0f :
    CONV_UPDATE_EXPONENT == 0.5f ? (1.0f / sqrt((float)(NUM_OCCURRENCES_ACROSS * NUM_OCCURRENCES_DOWN))) :
    // General case.
    (1.0f /
     pow((float)(NUM_OCCURRENCES_ACROSS * NUM_OCCURRENCES_DOWN), (float)CONV_UPDATE_EXPONENT));

  {
    // Update the one weight.
    const int widx = feature_num * INDICES_PER_NODE + pidx;
    if (CONV_UPDATE_EXPONENT != 0.0)
      weight_grad *= multiplier;
    // PERF fma()
    ACCUMULATE(weight_grads[NUM_WEIGHTS * example_num_in_batch + widx],
               weight_grad);
  }

  if (pidx == 0) {
    if (CONV_UPDATE_EXPONENT != 0.0)
      bias_grad *= multiplier;
    // PERF fma()
    ACCUMULATE(bias_grads[NUM_BIASES * example_num_in_batch + feature_num],
               bias_grad);
  }
}

