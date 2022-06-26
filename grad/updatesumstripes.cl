
// We process the weight gradients in parallel, and need to sum the
// stripes when we're done, before running the second pass.

// Expects the following defines:
//
// W, an int giving the number of stripes

__kernel void UpdateSumStripes(
                 // number of gradients per stripe
                 const int num_grads,
                 // flat scratch space of size
                 // num_grads * W
                 __global float *restrict grad_sums) {
  const int idx = get_global_id(0);

  // PERF could compare looping from 1 to W and then using +=
  float sum = 0.0f;
  for (int i = 0; i < W; i++) {
    sum += grad_sums[i * num_grads + idx];
  }

  grad_sums[idx] = sum;
  // vstore_half(sum, idx, grad_sums);
}
