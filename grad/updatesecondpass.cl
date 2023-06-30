
// Second pass of the update weights phase.
//
// grad_sums   - sum of updates (gradients that tell us the desired
//               change in weight - these have the opposite sign
//               in some presentations) across all examples
// weights     - existing weights for the chunk; same size
// weights_aux - for ADAM, this is 2x the size of the above. for SGD, empty
//
// the loop is basically weights[i] += scale * grad_sums[i]
// where scale includes the learning rate and (1/num_examples), at least
// naively. In the case of ADAM, we do something more complicated and
// update weights_aux as well.
//
// weights here can actually be biases; we use the same kernel for each.

// Expects the following defines:
//
// EXAMPLES_PER_ROUND, the number of examples that contributed so that
//   we can compute the average.
// Either WEIGHT_UPDATE_SGD or WEIGHT_UPDATE_ADAM, which tells us what
//   method to use to compute the update (and how to interpret the aux
//   array).
// ADAM_EPSILON, a float used in the denominator of the weight multiplier
//   "to avoid dividing by zero." 1.0e-6 is traditional, but some
//   recommend a much larger number (even 1.0!) for "sparse problems."
//   A larger value will result in smaller updates, so it may help
//   with divergence, whereas smaller values produce faster convergence
//   for stable problems.
// ADAM_B1 and ADAM_B2, floats slightly less than 1, giving the weights
//   on the exponential moving averages for the first and second moments.
//   0.9 and 0.999 are traditional and others recommend leaving these
//   as-is.
// NOHAT, if true, skips the "hat" step to correct the exponential
//   moving averages during early rounds. Should be set only if
//   1.0 - pow(max(ADAM_B1, ADAM_B2), round) is 1 (or close enough
//   for your tastes).

// CLIPPING, if true, clips each grad to [-1,1] before applying any
//   update.
// CONSTRAIN, if true, ensures that the resulting weights are always
//   in [-constrain_max, constrain_max] (kernel parameter).

__kernel void UpdateWeightsSecondPass(
                 // zero-based round number. PERF: if one-based, saves
                 // instruction
                 const int round_number,
                 const float learning_rate,
                 // Only used if CONSTRAIN is defined.
                 // PERF: Could be compile-time constant, but some
                 // complexity since it is different for weights and
                 // biases.
                 const float constrain_max,
                 // These two are the same size.
                 __global float *restrict grad_sums,
                 __global float *restrict chunk_weights,
                 // For SGD, empty. For ADAM, num_weights * 2
                 __global float *restrict chunk_weights_aux) {
  const int idx = get_global_id(0);

  // Average gradient over all examples.
  const float raw_grad = grad_sums[idx] * (1.0f / EXAMPLES_PER_ROUND);
  #if CLIPPING
    // fmin and fmax should reject nan, inf
    const float grad = fmax(-1.0f, fmin(1.0f, raw_grad));
  #else
    const float grad = raw_grad;
  #endif

  // compute the update u according to the method
  #if WEIGHT_UPDATE_SGD
    const float u = learning_rate * grad;
  #elif WEIGHT_UPDATE_ADAM
    const int midx = idx * 2;
    const int vidx = idx * 2 + 1;
    const float m_prev = chunk_weights_aux[midx];
    const float v_prev = chunk_weights_aux[vidx];
    const float m_new = ADAM_B1 * m_prev + (1.0f - ADAM_B1) * grad;
    // PERF see below: Is the factored expression faster?
    const float v_new = ADAM_B2 * v_prev + (1.0f - ADAM_B2) * (grad * grad);
    // TODO: Avoid nan poisoning here
    chunk_weights_aux[midx] = m_new;
    chunk_weights_aux[vidx] = v_new;
    #if NOHAT
      const float m_hat = m_new;
      const float v_hat = v_new;
    #else
      const float m_hat = m_new / (1.0f - pow(ADAM_B1, round_number + 1));
      const float v_hat = v_new / (1.0f - pow(ADAM_B2, round_number + 1));
    #endif
    const float u = learning_rate * (m_hat / (sqrt(v_hat) + ADAM_EPSILON));
  #elif WEIGHT_UPDATE_YOGI
    // Mostly the same as the Adam code.
    const int midx = idx * 2;
    const int vidx = idx * 2 + 1;
    const float m_prev = chunk_weights_aux[midx];
    const float v_prev = chunk_weights_aux[vidx];
    const float m_new = ADAM_B1 * m_prev + (1.0f - ADAM_B1) * grad;
    // In Adam, ADAM_B2 * v_prev + (1.0f - ADAM_B2) * (grad * grad), ie.
    //   B2 * v + (1 - B2) * g^2
    // but the Yogi paper writes this as
    //   v - (1 - B2) * (v - g^2)
    // which distributes as
    //   v - (1*v - 1*g^2 - B2 * v + B2 * g^2)
    // = v - (v - g^2 - B2*v + B2*g^2)
    // = v + -v + g^2 + B2*v - B2*g^2
    // =          B2*v + g^2 - B2*g^2
    // =          B2*v + (1 - B2)*g^2
    // which is the same as above.
    const float gsquared = grad * grad;
    // Some implementations allow tanh() instead of sign() for a softer
    // transition.
    const float s = sign(v_prev - gsquared);
    const float v_new = v_prev - (1.0f - ADAM_B2) * s * gsquared;
    // TODO: Avoid nan poisoning here
    chunk_weights_aux[midx] = m_new;
    chunk_weights_aux[vidx] = v_new;
    #if NOHAT
      const float m_hat = m_new;
      const float v_hat = v_new;
    #else
      const float m_hat = m_new / (1.0f - pow(ADAM_B1, round_number + 1));
      const float v_hat = v_new / (1.0f - pow(ADAM_B2, round_number + 1));
    #endif
    const float u = learning_rate * (m_hat / (sqrt(v_hat) + ADAM_EPSILON));
  #else
    #error Weight update must be SGD, ADAM, or YOGI
  #endif

    // PERF -- generate a separate multiplier and value and use fma()
  #if CONSTRAIN
    chunk_weights[idx] = fmax(-constrain_max,
                              fmin(constrain_max,
                                   chunk_weights[idx] + u));
  #else
    chunk_weights[idx] += u;
  #endif
}
