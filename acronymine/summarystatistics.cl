// Same kernel for all types of layers.

__kernel void SummaryStatistics(
       // number of nodes (per example) on the layer.
       int layer_num_nodes,
       // number of examples in the training round
       // (could be compile-time constant)
       int num_examples,
       __global const float *restrict layer_outputs,
       __global float *restrict layer_errors) {
  const int node_idx = get_global_id(0);

  double mean = 0.0;
  for (int i = 0; i < num_examples; i++) {
    mean += layer_outputs[i * layer_num_nodes + node_idx];
  }
  mean = mean / num_examples;

  double var = 0.0;
  for (int i = 0; i < num_examples; i++) {
    double d = mean - layer_outputs[i * layer_num_nodes + node_idx];
    var += (d * d);
  }
  var = var / num_examples;

  // example 0
  layer_errors[node_idx] = mean;
  // example 1
  layer_errors[node_idx + layer_num_nodes] = var;
}
