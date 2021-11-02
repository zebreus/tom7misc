
// Sets the error on the output layer for a single chunk, which we can then
// propagate back up.
// Error distance is always linear, but this allows for remapping the values
// before computing that error. Define REMAP(c, i, x), which takes the chunk
// index c, the node index within that chunk i, and the float value
// (either actual or expected), and returns a float. For no
// remapping, just: #define REMAP(c, i, x) x

// Expects the following defines:
//  DERIVATIVE(v) - derivative of the transfer function (given in terms of
//     its output, as usual)
//  CHUNK_IDX - integer giving the chunk index on the layer (just for
//     passing this to the REMAP macro)
//  LAYER_SIZE - integer giving the full actual, expected, and error layer
//     sizes per example.
//  CHUNK_START - the position within the layer where the chunk's nodes
//     reside

// Each of the memories is a sub-buffer of the size of the chunk's span.
__kernel void SetOutputError(__global const float *restrict actual_outputs,
                             __global const float *restrict expected,
                             __global float *restrict output_error) {
  // k is the index within the chunk
  const int k = get_global_id(0);
  const int example_num = get_global_id(1);

  const int idx = LAYER_SIZE * example_num + CHUNK_START + k;

  const float out_k = actual_outputs[idx];
  const float expected_k = expected[idx];

  // Remapped values.
  const float rout_k = REMAP(CHUNK_IDX, k, out_k);
  const float rexpected_k = REMAP(CHUNK_IDX, k, expected_k);

  // Here we are multiplying by the derivative.
  // Derivative is defined in terms of f(x) (the actual output),
  // since this is most efficient for sigmoid and works for relu.

  // Note in some presentations this is out_k - expected_k.
  output_error[idx] = DERIVATIVE(rout_k) * (rexpected_k - rout_k);
}
