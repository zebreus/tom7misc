// Same kernel used for sparse, dense, and convolutional chunks.
// We could be running this on the whole network's weights at once,
// but they are not stored in a single array.
__kernel void DecayWeights(
       __global float *restrict chunk_weights,
       // a float like 0.999f, the multiplicative factor by which to
       // scale every weight. Note that this should generally be much
       // larger than 1 - effective_learning_rate, or else the updates
       // will not overcome the decay, and weights will all trend
       // towards zero.
       // Biases are not decayed.
       float decay_factor) {
  const int weight_idx = get_global_id(0);
  chunk_weights[weight_idx] *= decay_factor;
}
