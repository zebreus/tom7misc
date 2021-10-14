// Defines:
// DECAY_FACTOR, a float like 0.999f, the multiplicative factor
// by which to scale every weight. Biases are not decayed.

// Same kernel used for sparse, dense, and convolutional chunks.
// We could be running this on the whole network's weights at once,
// but they are not stored in a single array.
__kernel void DecayWeights(__global float *restrict chunk_weights) {
  const int weight_idx = get_global_id(0);
  chunk_weights[weight_idx] *= DECAY_FACTOR;
}
