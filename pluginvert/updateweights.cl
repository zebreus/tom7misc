// Update the weights and biases for each node.

// If noclip is true, then allow updates of arbitrary size.
// This is formally "correct" but sometimes results in unusable
// networks (with infinite weights, etc.). Recommend NOCLIP=false.
#define NOCLIP false

// Alternative (with NOCLIP=false) you can require the weights
// and biases to stay within -MAX to MAX.
#define CONSTRAIN true
#define CONSTRAIN_WEIGHT_MAX 16.0f
#define CONSTRAIN_BIAS_MAX 16384.0f

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
// Note this one does not depend on the transfer function.

__kernel void UpdateWeightsSparse(
                 float learning_rate,
                 __global const float *restrict layer_error,
                 // num_nodes * INDICES_PER_NODE
                 __global const int *restrict layer_indices,
                 __global const float *restrict layer_values,
                 // num_nodes * INDICES_PER_NODE,
                 __global float *restrict layer_weights,
                 // num_nodes
                 __global float *restrict layer_biases) {
  const int node_idx = get_global_id(0);
  const float delta_j = layer_error[node_idx];
  const float learning_rate_times_delta_j = learning_rate * delta_j;

  for (int input_idx = 0; input_idx < INDICES_PER_NODE; input_idx++) {
    const int gidx = INDICES_PER_NODE * node_idx + input_idx;
    // Offset of the node, which we use to get its output.
    const int src_idx = layer_indices[gidx];
    const float x_ji = layer_values[src_idx];

    #if NOCLIP
    // PERF: fma()?
    layer_weights[gidx] += learning_rate_times_delta_j * x_ji;

    #elif CONSTRAIN
    // PERF fma()
    const float new_value = layer_weights[gidx] + learning_rate_times_delta_j * x_ji;
    const float constrained_value =
      fmax(-CONSTRAIN_WEIGHT_MAX, fmin(CONSTRAIN_WEIGHT_MAX, new_value));
    layer_weights[gidx] = constrained_value;

    #else

    // Clipping
    const float update =
      fmax(-1.0f,
           fmin(1.0f, learning_rate_times_delta_j * x_ji));
    if (!isnan(update))
      layer_weights[gidx] += update;
    #endif
  }

  #if NOCLIP
  layer_biases[node_idx] += learning_rate_times_delta_j;
  #elif CONSTRAIN
  const float new_bias = layer_biases[node_idx] + learning_rate_times_delta_j;
  // if (new_bias <= CONSTRAIN_MAX && new_bias >= -CONSTRAIN_MAX)
  // layer_biases[node_idx] = new_bias;

  const float constrained_value =
    fmax(-CONSTRAIN_BIAS_MAX, fmin(CONSTRAIN_BIAS_MAX, new_bias));
  layer_biases[node_idx] = constrained_value;

  #else
  const float bupdate =
    fmax(-1.0f,
         fmin(1.0f, learning_rate_times_delta_j));
  if (!isnan(bupdate))
    layer_biases[node_idx] += bupdate;
  #endif
}

// When the layer is dense.
__kernel void UpdateWeightsDense(
                 float learning_rate,
                 __global const float *restrict layer_error,
                 __global const int *restrict layer_indices_unused,
                 // source layer's output values
                 __global const float *restrict layer_values,
                 // num_nodes * INDICES_PER_NODE,
                 __global float *restrict layer_weights,
                 // num_nodes
                 __global float *restrict layer_biases) {
  const int node_idx = get_global_id(0);
  const float delta_j = layer_error[node_idx];
  const float learning_rate_times_delta_j = learning_rate * delta_j;

  for (int input_idx = 0; input_idx < INDICES_PER_NODE; input_idx++) {
    const int gidx = INDICES_PER_NODE * node_idx + input_idx;
    // Offset of the node, which we use to get its output.
    // const int src_idx = layer_indices[gidx];
    // For a dense layer, each source node is used in order.
    const int src_idx = input_idx;

    const float x_ji = layer_values[src_idx];

    #if NOCLIP
    // PERF: fma()?
    layer_weights[gidx] += learning_rate_times_delta_j * x_ji;

    #elif CONSTRAIN
    // PERF fma()
    const float new_value = layer_weights[gidx] + learning_rate_times_delta_j * x_ji;
    const float constrained_value =
      fmax(-CONSTRAIN_WEIGHT_MAX, fmin(CONSTRAIN_WEIGHT_MAX, new_value));
    layer_weights[gidx] = constrained_value;

    #else
    // Clipping
    const float update =
      fmax(-1.0f,
           fmin(1.0f, learning_rate_times_delta_j * x_ji));
    if (!isnan(update))
      layer_weights[gidx] += update;
    #endif
  }

  #if NOCLIP
  layer_biases[node_idx] += learning_rate_times_delta_j;
  #elif CONSTRAIN
  const float new_bias = layer_biases[node_idx] + learning_rate_times_delta_j;
  // if (new_bias <= CONSTRAIN_MAX && new_bias >= -CONSTRAIN_MAX)
  //    layer_biases[node_idx] = new_bias;

  const float constrained_value =
    fmax(-CONSTRAIN_BIAS_MAX, fmin(CONSTRAIN_BIAS_MAX, new_bias));
  layer_biases[node_idx] = constrained_value;



  #else
  const float bupdate =
    fmax(-1.0f,
         fmin(1.0f, learning_rate_times_delta_j));
  if (!isnan(bupdate))
    layer_biases[node_idx] += bupdate;
  #endif
}


// For convolutional layers, the weights and biases are shared by
// each occurrence of a feature. We can't write to weights/biases
// in parallel because they may conflict. So here the global idx
// gives a feature...?

// This version computes both weights and biases, parallelized only
// over the feature id.
// XXXX This was much slower! Delete!
__kernel void UpdateWeightsConvolutional1D(
                 float learning_rate,
                 __global const float *restrict layer_error,
                 // num_nodes * INDICES_PER_NODE
                 __global const int *restrict layer_indices,
                 __global const float *restrict layer_values,
                 // num_nodes * INDICES_PER_NODE,
                 __global float *restrict layer_weights,
                 // num_nodes
                 __global float *restrict layer_biases) {
  const int feature_num = get_global_id(0);

  // Scale down the learning rate to be more like an average over all
  // the occurrences; otherwise we may apply updates that are way too
  // large.
  const float effective_learning_rate =
    learning_rate * (1.0f / sqrt((float)NUM_OCCURRENCES));

  float bias_update = 0.0f;

  // PERF a way to allocate as zeroes?
  __local float weight_updates[INDICES_PER_NODE];
  for (int pidx = 0; pidx < INDICES_PER_NODE; pidx++)
    weight_updates[pidx] = 0.0f;

  // Loop over every occurrence of the feature, each of which gives
  // us a different error term.
  for (int occ = 0; occ < NUM_OCCURRENCES; occ++) {
    const int node_idx = occ * NUM_FEATURES + feature_num;
    const float delta_j = layer_error[node_idx];
    // PERF factor out and multiply at the end?
    const float learning_rate_times_delta_j =
      effective_learning_rate * delta_j;

    bias_update += learning_rate_times_delta_j;

    // Update the weight for each of the pattern's edges.
    float weight_update = 0.0f;
    for (int pidx = 0; pidx < INDICES_PER_NODE; pidx++) {
      // Offset of the node, which we use to get its output.
      const int src_idx = layer_indices[occ * INDICES_PER_NODE + pidx];
      const float x_ji = layer_values[src_idx];
      // PERF fma
      weight_updates[pidx] += learning_rate_times_delta_j * x_ji;
    }
  }

  for (int pidx = 0; pidx < INDICES_PER_NODE; pidx++) {
    const int widx = feature_num * INDICES_PER_NODE + pidx;
    #if NOCLIP
    layer_weights[widx] += weight_updates[pidx];
    #elif CONSTRAIN
    const float new_value = layer_weights[widx] + weight_updates[pidx];
    const float constrained_value =
      fmax(-CONSTRAIN_WEIGHT_MAX, fmin(CONSTRAIN_WEIGHT_MAX, new_value));
    layer_weights[widx] = constrained_value;
    #else
    // Clipping
    const float update = fmax(-1.0f, fmin(1.0f, weight_updates[pidx]));
    if (!isnan(update))
      layer_weights[widx] += update;
    #endif
  }

  #if NOCLIP
  layer_biases[feature_num] += bias_update;
  #elif CONSTRAIN
  const float new_bias = layer_biases[feature_num] + bias_update;
  const float constrained_value =
    fmax(-CONSTRAIN_BIAS_MAX, fmin(CONSTRAIN_BIAS_MAX, new_bias));
  layer_biases[feature_num] = constrained_value;
  #else
  // Clipping
  const float bupdate = fmax(-1.0f, fmin(1.0f, bias_update));
  if (!isnan(bupdate))
    layer_biases[feature_num] += bupdate;
  #endif
}


// PERF: rather than trying to tack on the bias, do it
// in its own kernel.
__kernel void UpdateWeightsConvolutional(
                 // Should be pre-scaled by caller.
                 float effective_learning_rate,
                 __global const float *restrict layer_error,
                 // num_nodes * INDICES_PER_NODE
                 __global const int *restrict layer_indices,
                 __global const float *restrict layer_values,
                 // num_nodes * INDICES_PER_NODE,
                 __global float *restrict layer_weights,
                 // num_nodes
                 __global float *restrict layer_biases) {
  // in 0..NUM_FEATURES-1
  const int feature_num = get_global_id(0);
  // in 0..INDICES_PER_NODE-1
  const int pidx = get_global_id(1);

  // Scale down the learning rate to be more like an average over all
  // the occurrences; otherwise we may apply updates that are way too
  // large.
  // const float effective_learning_rate =
  // learning_rate * (1.0f / sqrt((float)NUM_OCCURRENCES));

  // Compute the bias no matter what, but only write it for
  // pidx 0.
  // Both of these need to be multiplied by the learning rate
  // at the end.
  float bias_update = 0.0f;
  float weight_update = 0.0f;

  // Loop over every occurrence of the feature, each of which gives
  // us a different error term.
  for (int occ = 0; occ < NUM_OCCURRENCES; occ++) {
    const int node_idx = occ * NUM_FEATURES + feature_num;
    const float delta_j = layer_error[node_idx];
    // Normally we'd use learning_rate_times_delta_j here, but
    // every element of these sums has the same factor, so we
    // factor it out and multiply at the end.

    bias_update += delta_j;

    // But we are only updating a single weight.
    // Offset of the node, which we use to get its output.
    const int src_idx = layer_indices[occ * INDICES_PER_NODE + pidx];
    const float x_ji = layer_values[src_idx];
    // weight_update += delta_j * x_ji;
    weight_update = fma(delta_j, x_ji, weight_update);
  }

  weight_update *= effective_learning_rate;
  bias_update *= effective_learning_rate;

  // Update the one weight.
  const int widx = feature_num * INDICES_PER_NODE + pidx;
  #if NOCLIP
  layer_weights[widx] += weight_update;
  #elif CONSTRAIN
  const float new_value = layer_weights[widx] + weight_update;
  const float constrained_value =
    fmax(-CONSTRAIN_WEIGHT_MAX, fmin(CONSTRAIN_WEIGHT_MAX, new_value));
  layer_weights[widx] = constrained_value;
  #else
  // Clipping
  const float update = fmax(-1.0f, fmin(1.0f, weight_update));
  if (!isnan(update))
    layer_weights[widx] += update;
  #endif

  if (pidx == 0) {
    #if NOCLIP
    layer_biases[feature_num] += bias_update;
    #elif CONSTRAIN
    const float new_bias = layer_biases[feature_num] + bias_update;
    const float constrained_value =
      fmax(-CONSTRAIN_BIAS_MAX, fmin(CONSTRAIN_BIAS_MAX, new_bias));
    layer_biases[feature_num] = constrained_value;
    #else
    // Clipping
    const float bupdate = fmax(-1.0f, fmin(1.0f, bias_update));
    if (!isnan(bupdate))
      layer_biases[feature_num] += bupdate;
    #endif
  }
}

