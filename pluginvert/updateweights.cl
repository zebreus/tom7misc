// Update the weights and biases for each node, using
// the errors and the outputs from the previous layer.

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
// CHUNK_START, the index of this chunk within layer_error.
// SPAN_START, SPAN_SIZE, the span of the input layer that we
//   read from.

// NOCLIP, CONSTRAIN - if noclip, allow any update. otherwise,
//   if constrain, then constrain weights to be within
//   +/- UPDATE_{WEIGHT,BIAS}_MAX at most. otherwise, clip the
//   update itself to +/-1.

// Note this one does not depend on the transfer function.


// Defines the macro we use to perform arr[idx] += u, but
// clipping or constraining the update. Note these are not
// hygenic with e.g. do { } while (0) because I'm superstitious
// about opencl performance. So just be careful.
#if NOCLIP
  // yolo
  // PERF that this can actually be done with fma(), although
  // we rarely disable clipping
  #define UPDATE(cmax, arr, idx, u) arr[idx] += (u)
#elif CONSTRAIN
  // value always within range
  // PERF arr[index] + (u) can be computed with fma()
  #define UPDATE(cmax, arr, idx, u)              \
    {                                            \
      const int index = idx;                     \
      arr[index] = fmax(-cmax,                   \
                        fmin(cmax,               \
                             arr[index] + (u))); \
    }
#else
  // clipping
  #define UPDATE(cmax_, arr, idx, u)                     \
    {                                                    \
      const float nu = fmax(-1.0f, fmin(1.0f, (u)));     \
      if (!isnan(nu)) arr[feature_num] += nu;   \
    }
#endif

__kernel void UpdateWeightsSparse(
                 float learning_rate,
                 // full previous layer's output values
                 __global const float *restrict prev_layer_output,
                 // full layer's error, size num_nodes
                 __global const float *restrict layer_error,
                 // chunk.num_nodes * INDICES_PER_NODE
                 __global const int *restrict chunk_indices,
                 // chunk.num_nodes * INDICES_PER_NODE,
                 __global float *restrict chunk_weights,
                 // chunk.num_nodes
                 __global float *restrict chunk_biases) {
  const int chunk_node_idx = get_global_id(0);
  const int global_node_idx = CHUNK_START + chunk_node_idx;
  const float delta_j = layer_error[global_node_idx];
  const float learning_rate_times_delta_j = learning_rate * delta_j;

  for (int input_idx = 0; input_idx < INDICES_PER_NODE; input_idx++) {
    const int edge_idx = INDICES_PER_NODE * chunk_node_idx + input_idx;
    // Offset of the node, which we use to get its output.
    // (These are already global to the previous layer, but should
    // be inside the span.)
    const int src_idx = chunk_indices[edge_idx];
    const float x_ji = prev_layer_output[src_idx];

    const float update = learning_rate_times_delta_j * x_ji;

    UPDATE(CONSTRAIN_WEIGHT_MAX, chunk_weights, edge_idx, update);
  }

  UPDATE(CONSTRAIN_BIAS_MAX,
         chunk_biases, chunk_node_idx, learning_rate_times_delta_j);
}

// When the layer is dense.
__kernel void UpdateWeightsDense(
                 float learning_rate,
                 // full previous layer's output values
                 __global const float *restrict prev_layer_output,
                 // full layer's error, size num_nodes
                 __global const float *restrict layer_error,
                 // (unused, invalid)
                 __global const int *restrict chunk_indices_unused,
                 // chunk.num_nodes * INDICES_PER_NODE,
                 __global float *restrict chunk_weights,
                 // chunk.num_nodes
                 __global float *restrict chunk_biases) {
  const int chunk_node_idx = get_global_id(0);
  const int global_node_idx = CHUNK_START + chunk_node_idx;

  const float delta_j = layer_error[global_node_idx];
  const float learning_rate_times_delta_j = learning_rate * delta_j;

  for (int input_idx = 0; input_idx < INDICES_PER_NODE; input_idx++) {
    const int edge_idx = INDICES_PER_NODE * chunk_node_idx + input_idx;
    // For a dense layer, each source node is used in order, starting
    // at the beginning of the span.
    const int src_idx = SPAN_START + input_idx;

    const float x_ji = prev_layer_output[src_idx];

    const float update = learning_rate_times_delta_j * x_ji;
    UPDATE(CONSTRAIN_WEIGHT_MAX, chunk_weights, edge_idx, update);
  }

  UPDATE(CONSTRAIN_BIAS_MAX,
         chunk_biases, chunk_node_idx, learning_rate_times_delta_j);
}

// PERF: rather than trying to tack on the bias, do it
// in its own kernel.
__kernel void UpdateWeightsConvolutional(
                 // Should be pre-scaled by caller to account for
                 // the fact that we are making many updates due
                 // to the many occurrences of the pattern.
                 float effective_learning_rate,
                 // full previous layer's output
                 __global const float *restrict prev_layer_output,
                 // full layer's error, size num_nodes
                 __global const float *restrict layer_error,
                 // chunk.num_nodes * INDICES_PER_NODE
                 __global const int *restrict chunk_indices,
                 // NUM_FEATURES * INDICES_PER_NODE,
                 __global float *restrict chunk_weights,
                 // NUM_FEATURES
                 __global float *restrict chunk_biases) {
  // in 0..NUM_FEATURES-1
  const int feature_num = get_global_id(0);
  // in 0..INDICES_PER_NODE-1
  const int pidx = get_global_id(1);

  // Compute the bias no matter what, but only write it for
  // pidx 0.
  // Both of these need to be multiplied by the learning rate
  // at the end.
  float bias_update = 0.0f;
  float weight_update = 0.0f;

  // Loop over every occurrence of the feature, each of which gives
  // us a different error term.
  for (int occ = 0; occ < NUM_OCCURRENCES; occ++) {
    const int chunk_node_idx = occ * NUM_FEATURES + feature_num;
    const int global_node_idx = CHUNK_START + chunk_node_idx;
    const float delta_j = layer_error[global_node_idx];

    // Normally we'd use learning_rate_times_delta_j here, but
    // every element of these sums has the same factor, so we
    // factor it out and multiply at the end.
    bias_update += delta_j;

    // But we are only updating a single weight.
    // Offset of the node, which we use to get its output.
    // (These are already global to the input layer, but should
    // be within the span.)
    const int src_idx = chunk_indices[occ * INDICES_PER_NODE + pidx];
    const float x_ji = prev_layer_output[src_idx];
    // weight_update += delta_j * x_ji;
    weight_update = fma(delta_j, x_ji, weight_update);
  }

  weight_update *= effective_learning_rate;
  bias_update *= effective_learning_rate;

  // Update the one weight.
  const int widx = feature_num * INDICES_PER_NODE + pidx;
  UPDATE(CONSTRAIN_WEIGHT_MAX, chunk_weights, widx, weight_update);

  if (pidx == 0) {
    UPDATE(CONSTRAIN_BIAS_MAX, chunk_biases, feature_num, bias_update);
  }
}

