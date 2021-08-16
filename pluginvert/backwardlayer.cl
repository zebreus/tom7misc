
// Propagates errors from the destination to the source.

// Expects the following defines:
// DST_INDICES_PER_NODE, an integer giving the number of indices
//   that each node in the destination layer has. This is a constant
//   with the hopes that the integer division can be done with tricks.
//
// DST_NUM_NODES, an integer.
//
// DERIVATIVE, the derivative of the transfer function, given in
//   terms of the function's output.
//
// DST_NUM_FEATURES, an integer giving the number of features in a
//   LAYER_CONVOLUTION_ARRAY destination layer. For sparse and dense
//   destination layers, this is ignored (but should be defined to 1
//   or whatever). Must divide the number of nodes in the dest layer.


// If false, prevent the final source error from being more than +/-
// LARGE_ERROR.
#define NOCLIP false
#define LARGE_ERROR 1000000.0f

// For when the destination layer is sparse.
__kernel void BackwardLayerSparse(
                  // Size src_num_nodes.
                  __global const uint *restrict inverted_indices_start,
                  __global const uint *restrict inverted_indices_length,
                  // Size dst_num_nodes * dst_indices_per_node.
                  __global const int *restrict inverted_indices,
                  // Size dst_num_nodes * dst_indices_per_node.
                  __global const float *restrict dst_weights,
                  // Size src_num_nodes.
                  __global const float *restrict src_output,
                  // Size dst_num_nodes.
                  __global const float *restrict dst_error,
                  // Size src_num_nodes; finally where we write:
                  __global float *restrict src_error) {
  const int h = get_global_id(0);
  const float out_h = src_output[h];
  // Unpack inverted index for this node, so that we can loop over all of
  // the places its output is sent.
  const uint start = inverted_indices_start[h];
  const uint length = inverted_indices_length[h];

  // The error for a hidden node is the sum of all the errors for
  // the next layer, but modulated by the weight of the edge.
  float weighted_error_sum = 0.0f;
  for (int i = start; i < start + length; i++) {
    const int gidx = inverted_indices[i];
    // Compute from the index which destination node it belongs to.
    const int dst_node_idx = gidx / DST_INDICES_PER_NODE;

    // fma() seems slightly faster.
    // weighted_error_sum += dst_weights[gidx] * dst_error[dst_node_idx];
    weighted_error_sum = fma(dst_weights[gidx], dst_error[dst_node_idx],
                             weighted_error_sum);
  }

  #if NOCLIP
  const float final_error = DERIVATIVE(out_h) * weighted_error_sum;
  #else
  const float final_error =
    fmax(-LARGE_ERROR,
         fmin(LARGE_ERROR, DERIVATIVE(out_h) * weighted_error_sum));
  #endif

  src_error[h] = final_error;
}

// For when the destination layer is dense.
__kernel void BackwardLayerDense(
              // Size src_num_nodes.
              __global const uint *restrict inverted_indices_start_unused,
              __global const uint *restrict inverted_indices_length_unused,
              // Size dst_num_nodes * dst_indices_per_node.
              __global const int *restrict inverted_indices_unused,
              // Size dst_num_nodes * dst_indices_per_node.
              __global const float *restrict dst_weights,
              // Size src_num_nodes.
              __global const float *restrict src_output,
              // Size dst_num_nodes.
              __global const float *restrict dst_error,
              // Size src_num_nodes; finally where we write:
              __global float *restrict src_error) {
  // h in 0..src_num_nodes
  const int h = get_global_id(0);
  const float out_h = src_output[h];

  // The destination layer is dense, so each node there reads from
  // the source output at the same index. The offset is this node's
  // index (h), the stride is DST_INDICES_PER_NODE, and there
  // are DST_NUM_NODES such outputs.

  // Unpack inverted index for this node, so that we can loop over all of
  // the places its output is sent.
  // const uint start = inverted_indices_start[h];
  // const uint length = inverted_indices_length[h];

  // The error for a hidden node is the sum of all the errors for
  // the next layer, but modulated by the weight of the edge.
  float weighted_error_sum = 0.0f;
  for (int i = 0; i < DST_NUM_NODES; i++) {
    const int gidx = h + DST_INDICES_PER_NODE * i;

    // fma() seems slightly faster.
    // weighted_error_sum += dst_weights[gidx] * dst_error[dst_node_idx];
    weighted_error_sum = fma(dst_weights[gidx], dst_error[i],
                             weighted_error_sum);
  }

  #if NOCLIP
  const float final_error = DERIVATIVE(out_h) * weighted_error_sum;
  #else
  const float final_error =
    fmax(-LARGE_ERROR, fmin(LARGE_ERROR, DERIVATIVE(out_h) * weighted_error_sum));
  #endif

  src_error[h] = final_error;
}

// For convolutional layers. This is like sparse in that we read the
// indices out of the inverted index, but each element actually stands
// for DST_NUM_FEATURES edges; we have to derive the actual destinations
// and weights from the value and some parameters.
__kernel void BackwardLayerConvolutional(
                  // Size src_num_nodes.
                  __global const uint *restrict inverted_indices_start,
                  __global const uint *restrict inverted_indices_length,
                  // Size dst_num_nodes * dst_indices_per_node.
                  __global const int *restrict inverted_indices,
                  // Size dst_num_nodes * dst_indices_per_node.
                  __global const float *restrict dst_weights,
                  // Size src_num_nodes.
                  __global const float *restrict src_output,
                  // Size dst_num_nodes.
                  __global const float *restrict dst_error,
                  // Size src_num_nodes; finally where we write:
                  __global float *restrict src_error) {

  // Node in the source layer.
  const int h = get_global_id(0);
  // ... and its output value.
  const float out_h = src_output[h];
  // Unpack inverted index for this node, so that we can loop over all of
  // the places (here, occurrences of the convolution pattern) its output
  // is sent.
  const uint start = inverted_indices_start[h];
  const uint length = inverted_indices_length[h];

  // The error for a hidden node is the sum of all the errors for
  // the next layer, but modulated by the weight of the edge.
  float weighted_error_sum = 0.0f;
  for (int i = start; i < start + length; i++) {
    const int gidx = inverted_indices[i];
    // Each occurrence of the pattern has pat_width * pat_height = ipn
    // indices.
    const int occurrence_num = gidx / DST_INDICES_PER_NODE;
    // (flat) position in the pattern
    const int pattern_offset = gidx % DST_INDICES_PER_NODE;

    // ... but each index is actually shared by DST_NUM_FEATURES features.
    for (int f = 0; f < DST_NUM_FEATURES; f++) {
      const int dst_node_idx = occurrence_num * DST_NUM_FEATURES + f;
      // each index has its own weights
      const int weight_idx = DST_INDICES_PER_NODE * f + pattern_offset;
      // fma() seems slightly faster.
      // weighted_error_sum +=
      //   dst_weights[weight_idx] * dst_error[dst_node_idx];
      weighted_error_sum = fma(dst_weights[weight_idx],
                               dst_error[dst_node_idx],
                               weighted_error_sum);
    }
  }

  #if NOCLIP
  const float final_error = DERIVATIVE(out_h) * weighted_error_sum;
  #else
  const float final_error =
    fmax(-LARGE_ERROR,
         fmin(LARGE_ERROR, DERIVATIVE(out_h) * weighted_error_sum));
  #endif

  src_error[h] = final_error;
}
