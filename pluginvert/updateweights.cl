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

// TODO: Make configurable, although it seems that these are
// rarely tuned.
#define ADAM_B1 0.9f
#define ADAM_B2 0.999f
// This one apparently can use some tuning; I don't have any
// good intuitions though.
#define ADAM_EPSILON 0.0000001f

// Note this one does not depend on the transfer function.

// Defines the macro we use to compute the scaled update, as
//     float u = learning_rate * grad;
// which can also read and write the aux array for the given
// index. round is the integer round number (first round = 0).
// Note that the gradient here is positive (direction to move) not
// negative (loss) as in some presentations.
#if WEIGHT_UPDATE_SGD
#define SCALE_UPDATE(u, aux, idx, round, learning_rate, g)  \
  const float u = (learning_rate) * (g)

#elif WEIGHT_UPDATE_ADAM
// PERF: Skip _hat step when round is sufficiently large
// PERF: Several of these things can be factored out of loops,
//   hopefully by opencl itself, but we could do it manually if not
// Prefix variables with u to avoid conflicts, sorry
#define SCALE_UPDATE(u, aux, idx, round, learning_rate, g)        \
  const float u ## gval = (g);                                    \
  const int u ## midx = (idx) * 2;                                \
  const int u ## vidx = (idx) * 2 + 1;                            \
  const float u ## m_prev = aux[u ## midx];                       \
  const float u ## v_prev = aux[u ## vidx];                       \
  const float u ## m_new =                                        \
    ADAM_B1 * u ## m_prev + (1.0f - ADAM_B1) * u ## gval;         \
  const float u ## v_new =                                              \
    ADAM_B2 * u ## v_prev + (1.0f - ADAM_B2) * (u ## gval * u ## gval); \
  aux[u ## midx] = u ## m_new;                                          \
  aux[u ## vidx] = u ## v_new;                                          \
  const float u ## m_hat = u ## m_new / (1.0f - pow(ADAM_B1, round + 1)); \
  const float u ## v_hat = u ## v_new / (1.0f - pow(ADAM_B2, round + 1));  \
  const float u =                                                       \
    (learning_rate) * (u ## m_hat / (sqrt(u ## v_hat) + ADAM_EPSILON))

#else
  #error Weight update must be SGD or ADAM
#endif

// TODO: Perhaps some of this should be happening before we
// apply the adam estimations, since for example we would not
// want our momentum to include any 'inf'!
//
// Defines the macro we use to perform arr[idx] += u, but
// clipping or constraining the update. Note these are not
// hygienic with e.g. do { } while (0) because I'm superstitious
// about opencl performance. So just be careful.
#if NOCLIP
  // yolo
  // PERF that this can actually be done with fma(), although
  // we rarely disable clipping
  #define APPLY_UPDATE(cmax, arr, idx, u) arr[idx] += (u)
#elif CONSTRAIN
  // value always within range
  // PERF arr[index] + (u) can be computed with fma()
  #define APPLY_UPDATE(cmax, arr, idx, u)        \
    {                                            \
      const int index = idx;                     \
      arr[index] = fmax(-cmax,                   \
                        fmin(cmax,               \
                             arr[index] + (u))); \
    }
#else
  // clipping
  #define APPLY_UPDATE(cmax_, arr, idx, u)               \
    {                                                    \
      const float nu = fmax(-1.0f, fmin(1.0f, (u)));     \
      if (!isnan(nu)) arr[feature_num] += nu;            \
    }
#endif

__kernel void UpdateWeightsSparse(
                 int round_number,
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
                 __global float *restrict chunk_biases,
                 // For SGD, empty. For ADAM, num_weights * 2
                 __global float *restrict chunk_weights_aux,
                 // For SGD, empty. For ADAM, num_biases * 2
                 __global float *restrict chunk_biases_aux) {
  const int chunk_node_idx = get_global_id(0);
  const int global_node_idx = CHUNK_START + chunk_node_idx;
  const float delta_j = layer_error[global_node_idx];
  // const float learning_rate_times_delta_j = learning_rate * delta_j;

  for (int input_idx = 0; input_idx < INDICES_PER_NODE; input_idx++) {
    const int edge_idx = INDICES_PER_NODE * chunk_node_idx + input_idx;
    // Offset of the node, which we use to get its output.
    // (These are already global to the previous layer, but should
    // be inside the span.)
    const int src_idx = chunk_indices[edge_idx];
    const float x_ji = prev_layer_output[src_idx];

    const float grad = delta_j * x_ji;

    SCALE_UPDATE(update, chunk_weights_aux, edge_idx, round_number,
                 learning_rate, grad);
    APPLY_UPDATE(CONSTRAIN_WEIGHT_MAX, chunk_weights, edge_idx, update);
  }

  const float bgrad = delta_j;
  SCALE_UPDATE(bupdate, chunk_biases_aux, chunk_node_idx, round_number,
               learning_rate, bgrad);
  APPLY_UPDATE(CONSTRAIN_BIAS_MAX, chunk_biases, chunk_node_idx, bupdate);
}

// When the layer is dense.
__kernel void UpdateWeightsDense(
                 int round_number,
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
                 __global float *restrict chunk_biases,
                 // For SGD, empty. For ADAM, num_weights * 2
                 __global float *restrict chunk_weights_aux,
                 // For SGD, empty. For ADAM, num_biases * 2
                 __global float *restrict chunk_biases_aux) {
  const int chunk_node_idx = get_global_id(0);
  const int global_node_idx = CHUNK_START + chunk_node_idx;

  const float delta_j = layer_error[global_node_idx];
  // const float learning_rate_times_delta_j = learning_rate * delta_j;

  for (int input_idx = 0; input_idx < INDICES_PER_NODE; input_idx++) {
    const int edge_idx = INDICES_PER_NODE * chunk_node_idx + input_idx;
    // For a dense layer, each source node is used in order, starting
    // at the beginning of the span.
    const int src_idx = SPAN_START + input_idx;

    const float x_ji = prev_layer_output[src_idx];

    const float grad = delta_j * x_ji;
    SCALE_UPDATE(update, chunk_weights_aux, edge_idx, round_number,
                 learning_rate, grad);
    APPLY_UPDATE(CONSTRAIN_WEIGHT_MAX, chunk_weights, edge_idx, update);
  }

  const float bgrad = delta_j;
  SCALE_UPDATE(bupdate, chunk_biases_aux, chunk_node_idx, round_number,
               learning_rate, bgrad);
  APPLY_UPDATE(CONSTRAIN_BIAS_MAX, chunk_biases, chunk_node_idx, bupdate);
}

// PERF: rather than tack on the bias when idx == 0 (stalls all other
// indices), try doing it in its own kernel.
__kernel void UpdateWeightsConvolutional(
                 int round_number,
                 // Should be pre-scaled by caller to account for
                 // the fact that we are making many updates due
                 // to the many occurrences of the pattern.
                 // (XXX when weight update is ADAM, perhaps we
                 // should be separating this scaling from the
                 // learning rate parameter)
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
                 __global float *restrict chunk_biases,
                 // For SGD, empty. For ADAM, num_weights * 2
                 __global float *restrict chunk_weights_aux,
                 // For SGD, empty. For ADAM, num_biases * 2
                 __global float *restrict chunk_biases_aux) {
  // in 0..NUM_FEATURES-1
  const int feature_num = get_global_id(0);
  // in 0..INDICES_PER_NODE-1
  const int pidx = get_global_id(1);

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
    const float delta_j = layer_error[global_node_idx];

    // Always compute bias term; all but one is thrown out.
    bias_grad += delta_j;

    // Multiple error terms, but we are only updating a single weight.
    // Offset of the node, which we use to get its output.
    // (These are already global to the input layer, but should
    // be within the span.)
    const int src_idx = chunk_indices[occ * INDICES_PER_NODE + pidx];
    const float x_ji = prev_layer_output[src_idx];
    // weight_grad += delta_j * x_ji;
    weight_grad = fma(delta_j, x_ji, weight_grad);
  }

  // TODO: With ADAM weight update, it might be better to scale by
  // the number of occurrences here, and just have "learning_rate".
  // Not sure how to think about this.
  // weight_update *= effective_learning_rate;
  // bias_update *= effective_learning_rate;

  {
    // Update the one weight.
    const int widx = feature_num * INDICES_PER_NODE + pidx;
    SCALE_UPDATE(weight_update, chunk_weights_aux, widx, round_number,
                 effective_learning_rate, weight_grad);
    APPLY_UPDATE(CONSTRAIN_WEIGHT_MAX, chunk_weights, widx, weight_update);
  }

  if (pidx == 0) {
    SCALE_UPDATE(bias_update, chunk_biases_aux, feature_num, round_number,
                 effective_learning_rate, bias_grad);
    APPLY_UPDATE(CONSTRAIN_BIAS_MAX, chunk_biases, feature_num, bias_update);
  }
}

