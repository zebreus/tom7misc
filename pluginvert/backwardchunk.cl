
// Propagates errors from the destination to the source. This acts on
// a chunk in the destination layer, but it propagates errors to any
// nodes used within its input span; therefore its writes can overlap
// with other chunks from the same layer. So we run the chunks
// sequentially, accumulating error (although see OVERWRITE below). A
// separate pass (backwardsecondpass.cl) multiplies by the derivative
// and optionally clips.

// Expects the following defines:
//
// SRC_LAYER_SIZE and DST_LAYER_SIZE, the number of nodes in the
//   two layers.
//
// CHUNK_START, an integer giving the start of the chunk within
//   the destination layer.
//
// SPAN_START and SPAN_SIZE, integers giving the input span in the
//   source layer.
//
// DST_INDICES_PER_NODE, an integer giving the number of indices
//   that each node in the destination chunk has. This is a constant
//   with the hopes that the integer division can be done with tricks.
//
// DST_NUM_NODES, an integer giving the number of nodes in the
//   chunk.
//
// DST_NUM_FEATURES, an integer giving the number of features in a
//   CHUNK_CONVOLUTION_ARRAY destination chunk. For sparse and dense
//   destination layers, this is ignored (but should be defined to 1
//   or whatever). Must divide the number of nodes in the dest chunk.
//
// OVERWRITE, a boolean. If true, then the values of src_error in
//   [SPAN_START, SPAN_START + SPAN_SIZE) are overwritten instead of
//   accumulating (= instead of +=). This is a small optimization for
//   the first chunk of each layer (or also even possible if the spans
//   are disjoint).


// For when the destination chunk is sparse.
__kernel void BackwardChunkSparse(
                  // Size SPAN_SIZE.
                  __global const uint *restrict inverted_indices_start,
                  __global const uint *restrict inverted_indices_length,

                  // XXX is there a simple expression for the size of this?
                  __global const int *restrict inverted_indices,
                  // Weights for this chunk. Size chunk.num_nodes *
                  // chunk.indices_per_node.
                  __global const float *restrict dst_weights,
                  // Full destination errors, size layers[dst].num_nodes per
                  // example.
                  __global const float *restrict dst_error,
                  // Full src errors, size layers[src].num_nodes per example.
                  // Where we write.
                  __global float *restrict src_error) {

  // node index within the input span. in [0, SPAN_SIZE).
  const int h_span = get_global_id(0);
  const int example_num = get_global_id(1);

  const int src_start_index = example_num * SRC_LAYER_SIZE;
  const int dst_start_index = example_num * DST_LAYER_SIZE;

  // index into this example's src_error etc.
  const int h_global = SPAN_START + h_span;

  // Unpack inverted index for this node, so that we can loop over all of
  // the places (within the chunk) that its output is sent.
  const uint start = inverted_indices_start[h_span];
  const uint length = inverted_indices_length[h_span];

  // The error for a hidden node is the sum of all the errors for
  // the next layer, but modulated by the weight of the edge.
  float weighted_error_sum = 0.0f;
  for (int i = start; i < start + length; i++) {
    // Index into destination layer's chunk. In [0, SPAN_SIZE).
    const int cidx = inverted_indices[i];
    // The global index of this node in the destination layer.
    const int dst_node_idx = CHUNK_START + cidx / DST_INDICES_PER_NODE;

    // weighted_error_sum += weight * error
    weighted_error_sum =
      fma(dst_weights[cidx],
          dst_error[dst_start_index + dst_node_idx],
          weighted_error_sum);
  }

  #if OVERWRITE
  src_error[src_start_index + h_global] = weighted_error_sum;
  #else
  src_error[src_start_index + h_global] += weighted_error_sum;
  #endif
}

// For when the destination chunk is dense.
__kernel void BackwardChunkDense(
              // Unused inverted indices. Empty.
              __global const uint *restrict inverted_indices_start_unused,
              __global const uint *restrict inverted_indices_length_unused,
              __global const int *restrict inverted_indices_unused,
              // Weights for this chunk. Size chunk.num_nodes *
              // chunk.indices_per_node.
              __global const float *restrict dst_weights,
              // Full destination errors, size layers[dst].num_nodes per
              // example.
              __global const float *restrict dst_error,
              // Full src errors, size layers[src].num_nodes per example.
              // Where we write.
              __global float *restrict src_error) {
  // h_span in [0, SPAN_SIZE]
  const int h_span = get_global_id(0);
  const int example_num = get_global_id(1);

  const int src_start_index = example_num * SRC_LAYER_SIZE;
  const int dst_start_index = example_num * DST_LAYER_SIZE;

  const int h_global = CHUNK_START + h_span;

  // The destination chunk is dense, so this node is sent to each
  // node in that chunk.
  // The offset in the chunk is this node's index (h_span), the stride
  // is DST_INDICES_PER_NODE, and there are DST_NUM_NODES such
  // outputs.

  float weighted_error_sum = 0.0f;
  for (int i = 0; i < DST_NUM_NODES; i++) {
    const int cidx = DST_INDICES_PER_NODE * i + h_span;
    const int dst_node_idx = CHUNK_START + i;

    // weighted_error_sum += weight * error
    weighted_error_sum = fma(dst_weights[cidx],
                             dst_error[dst_start_index + dst_node_idx],
                             weighted_error_sum);
  }

  #if OVERWRITE
  src_error[src_start_index + h_global] = weighted_error_sum;
  #else
  src_error[src_start_index + h_global] += weighted_error_sum;
  #endif
}

// For convolutional chunks. This is like sparse in that we read the
// indices out of the inverted index, but each element actually stands
// for DST_NUM_FEATURES edges; we have to derive the actual destinations
// and weights from the value and some parameters.
__kernel void BackwardChunkConvolutional(
                  // Size SPAN_SIZE.
                  __global const uint *restrict inverted_indices_start,
                  __global const uint *restrict inverted_indices_length,
                  // XXX is there a simple expression for the size of this?
                  __global const int *restrict inverted_indices,
                  // Size chunk.num_nodes * dst_indices_per_node.
                  __global const float *restrict dst_weights,
                  // Full destination errors, size layers[dst].num_nodes
                  // per example.
                  __global const float *restrict dst_error,
                  // Full src errors, size layers[src].num_nodes per
                  // example. Where we write.
                  __global float *restrict src_error) {

  // node index within the input span. in [0, SPAN_SIZE).
  const int h_span = get_global_id(0);
  const int example_num = get_global_id(1);

  const int src_start_index = example_num * SRC_LAYER_SIZE;
  const int dst_start_index = example_num * DST_LAYER_SIZE;

  // index into src_error etc.
  const int h_global = SPAN_START + h_span;

  const uint start = inverted_indices_start[h_span];
  const uint length = inverted_indices_length[h_span];

  float weighted_error_sum = 0.0f;
  for (int i = start; i < start + length; i++) {
    // Index into the destination layer's chunk.
    const int cidx = inverted_indices[i];
    // Each occurrence of the pattern has pat_width * pat_height = ipn
    // indices.
    const int occurrence_num = cidx / DST_INDICES_PER_NODE;
    // (flat) position in the pattern
    const int pattern_offset = cidx % DST_INDICES_PER_NODE;

    // ... but each index is actually shared by DST_NUM_FEATURES features.
    for (int f = 0; f < DST_NUM_FEATURES; f++) {
      // Index into global error array.
      const int dst_node_idx = CHUNK_START +
        occurrence_num * DST_NUM_FEATURES + f;
      // each index has its own weights
      const int weight_idx = DST_INDICES_PER_NODE * f + pattern_offset;
      // weighted_error_sum += weight * error
      weighted_error_sum = fma(dst_weights[weight_idx],
                               dst_error[dst_start_index + dst_node_idx],
                               weighted_error_sum);
    }
  }

  #if OVERWRITE
  src_error[src_start_index + h_global] = weighted_error_sum;
  #else
  src_error[src_start_index + h_global] += weighted_error_sum;
  #endif
}
