
#include "network.h"

#include <cstdio>
#include <cmath>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "threadutil.h"

using namespace std;

using uint32 = uint32_t;
using int64 = int64_t;

const char *TransferFunctionName(TransferFunction tf) {
  switch (tf) {
  case SIGMOID: return "SIGMOID";
  case RELU: return "RELU";
  case LEAKY_RELU: return "LEAKY_RELU";
  case IDENTITY: return "IDENTITY";
  default: return "??INVALID??";
  }
}

const char *ChunkTypeName(ChunkType lt) {
  switch (lt) {
  case CHUNK_INPUT: return "CHUNK_INPUT";
  case CHUNK_DENSE: return "CHUNK_DENSE";
  case CHUNK_SPARSE: return "CHUNK_SPARSE";
  case CHUNK_CONVOLUTION_ARRAY: return "CHUNK_CONVOLUTION_ARRAY";
  default: return "??INVALID??";
  }
}

// PERF: native_recip? native_exp? It's likely that we can tolerate
// inaccuracy of certain sorts.
const char *const Network::SIGMOID_FN =
  "#define FORWARD(potential) (1.0f / (1.0f + exp(-potential)))\n"
  // This wants to be given the actual output value f(potential).
  "#define DERIVATIVE(fx) (fx * (1.0f - fx))\n";

// PERF: I think LEAKY_ is generally better, but this could be
// implemented with fmax.
const char *const Network::RELU_FN =
  "#define FORWARD(potential) ((potential < 0.0f) ? 0.0f : potential)\n"
  // This is normally given as x < 0 ? 0 : 1, but note that f(x)
  // tells us which side of 0 the input is on (retaining the
  // not-very-important ambiguity at exactly 0), anyway. So we define
  // it in terms of f(x) to maintain the same interface we use for
  // sigmoid.
  "#define DERIVATIVE(fx) ((fx < 0.0f) ? 0.0f : 1.0f)\n";

// Like RELU but slight slope in the "zero" region.
// PERF: Perhaps a power-of-two multiplication that can be implemented
// by just changing the exponent? (a "shift")?
const char *const Network::LEAKY_RELU_FN =
  "#define FORWARD(potential) ((potential < 0.0f) ? potential * 0.01f : potential)\n"
  // See note above.
  "#define DERIVATIVE(fx) ((fx < 0.0f) ? 0.01f : 1.0f)\n";

// A bad general purpose transfer function (it needs to be non-linear
// or else we don't get any additional expressive power from hidden
// layers, but even more, it's just the identity!), but can be useful
// for special purposes (e.g. a layer that applies a permutation as
// some convenience.)
const char *const Network::IDENTITY_FN =
  "#define FORWARD(potential) (potential)\n"
  "#define DERIVATIVE(fx) (1.0f)\n";

string Network::TransferFunctionDefines(TransferFunction tf) {
  switch (tf) {
  case SIGMOID: return Network::SIGMOID_FN;
  case RELU: return Network::RELU_FN;
  case LEAKY_RELU: return Network::LEAKY_RELU_FN;
  case IDENTITY: return Network::IDENTITY_FN;
  default:
    CHECK(false) << "No define for transfer function "
                 << tf << ": " << TransferFunctionName(tf);
    return "#error no define\n";
  }
}

Network::Network(vector<Layer> layers_in) :
  layers(std::move(layers_in)) {

  printf("Structural check\n");
  StructuralCheck();
  printf("OK\n");
}

#if 0
// TODO: ComputeInvertedIndices
  // XXX needs to be rethought for chunks
  // I think what we want to do is have a vector of InvertedIndices
  // structs, with the same size as the number of chunks on the
  // NEXT layer (or just put these in the Layer structs..).
  struct InvertedIndices {
    // For a given node, where do I output to in the next layer?
    // Note that nodes don't all have the same number of outputs.
    // This is a packed structure to facilitate GPU operations.
    //
    // For a given node, where do my output indices start in
    // the indices array, and how many are there?
    // num_nodes[i]
    vector<uint32_t> start;
    vector<uint32_t> length;

    // Packed array of indices.
    // For all layer types, this is just the inverse of the corresponding
    // indices array. For sparse and dense layers:
    //
    // Since every node on the next layer has exactly
    // layers[l].indices_per_node inputs, this will be of size
    // layers[l].indices_per_node * num_nodes[l + 1]. However, any
    // given node on this layer may be used more or fewer times.
    //
    // The value here gives the index into the indices/weights vectors
    // for the next layer. If for each index i within the span (defined
    // by inverted_indices[layer].start[z]) for node id z
    // let gidx = inverted_indices[layer].output_indices[i]
    // and then layers[layer].indices[gidx] == z. (The same for the weight
    // vector gives us the weight, which is the point, and dividing
    // by INDICES_PER_NODE gives us the output node.) As such, this is
    // a permutation of 0..(num_nodes[ii] * layers[ii].indices_per_node - 1).
    //
    // If the destination layer is a convolution array, we still have
    // layers[layer].indices[gidx] == z. But the indices array only
    // stores indices for "one feature", since they are the same for
    // each. Thus this has size
    //    layers[l].indices_per_node * num_nodes[l + 1] /
    //    layers[l].num_features
    // The gidx in this vector is an index into indices[] as above; some
    // cell within the pattern in a specific occurrence. It stands for
    // num_features edges, each with its own weight.
    // int gidx = inverted_indices[layer].output_indices[i];
    //  .. TODO tips here on how to compute weights, output nodes ..
    // for (int f = 0; f < num_features; f++) {
    //    weight =  ...
    // }
    vector<uint32_t> output_indices;
  };

  // There are also num_layers of these, but be careful about the
  // offset. The 0th inverted index is about the gap between the input
  // layer (otherwise not represented in the network, except for its
  // size in num_nodes[0]) and the first hidden layer. The last one is
  // about the last gap, not the output layer, since the output layer
  // is not indexed by anything.
  vector<InvertedIndices> inverted_indices;

void Network::ReallocateInvertedIndices() {
  CHECK(num_layers > 0);
  inverted_indices.resize(num_layers);
  for (int i = 0; i < num_layers; i++) {
    printf("Layer %d/%d. num nodes %d, next ipn %d nn %d\n",
           i, num_layers,
           num_nodes[i],
           layers[i].indices_per_node,
           num_nodes[i + 1]);
    InvertedIndices &ii = inverted_indices[i];
    CHECK(num_nodes[i] > 0);
    ii.start.resize(num_nodes[i], 0);
    ii.length.resize(num_nodes[i], 0);
    CHECK(layers[i].indices_per_node > 0);

    // This depends on the next layer, and has a different size for
    // convolutional ones.
    const int dst_layer_nodes = num_nodes[i + 1];
    CHECK(dst_layer_nodes > 0);
    const LayerType dst_layer_type = layers[i].type;
    if (dst_layer_type == CHUNK_CONVOLUTION_ARRAY) {
      const int dst_num_features = layers[i].num_features;
      CHECK(dst_layer_nodes % dst_num_features == 0);
      ii.output_indices.resize(
          (dst_layer_nodes / dst_num_features) *
          layers[i].indices_per_node);
    } else {
      ii.output_indices.resize(
          layers[i].indices_per_node * dst_layer_nodes, 0);
    }
  }
}

void Network::ComputeInvertedIndices(int max_parallelism) {
  // Computes the values for inverted_indices[layer]. Note that
  // although we use the [layer] offset throughout, this is really
  // talking about the gap between layers, with the 0th element's
  // index being the way the first hidden layer uses the inputs, and
  // the 0th element's inverted index being about the way the inputs map
  // to the first hidden layer.
  auto OneLayer = [this](int layer) {
    CHECK_GE(layer, 0);
    CHECK_LT(layer, layers.size());
    const int src_num_nodes = num_nodes[layer];
    const int dst_num_nodes = num_nodes[layer + 1];
    const LayerType dst_layer_type = layers[layer].type;
    CHECK_LT(layer, num_layers);
    CHECK_LT(layer, inverted_indices.size());
    vector<uint32> *start = &inverted_indices[layer].start;
    vector<uint32> *length = &inverted_indices[layer].length;
    // Number of nodes depends on size of source layer.
    CHECK_EQ(src_num_nodes, start->size());
    CHECK_EQ(src_num_nodes, length->size());
    vector<uint32> *inverted = &inverted_indices[layer].output_indices;
    // But this has to account for all the nodes on the destination layer.
    if (dst_layer_type == CHUNK_CONVOLUTION_ARRAY) {
      const int dst_layer_nodes = num_nodes[layer + 1];
      CHECK(dst_layer_nodes % layers[layer].num_features == 0);
      CHECK_EQ((dst_layer_nodes / layers[layer].num_features) *
               layers[layer].indices_per_node,
               inverted->size())
        << "Conv layer #" << layer
        << "\ndst layer nodes: " << dst_layer_nodes
        << "\nnum features: " << layers[layer].num_features
        << "\n = " << ((dst_layer_nodes / layers[layer].num_features) *
                       layers[layer].indices_per_node)
        << " vs " << inverted->size();
    } else {
      CHECK_EQ(layers[layer].indices_per_node * dst_num_nodes,
               inverted->size());
    }

    // Indexed by node id in the source layer.
    vector<vector<uint32>> occurrences;
    occurrences.resize(num_nodes[layer]);

    // Regardless of the layer type, just invert the indices array.
    for (int dst_indices_idx = 0;
         dst_indices_idx < layers[layer].indices.size();
         dst_indices_idx++) {
      // This index gets put into exactly one place in occurrences.
      CHECK(dst_indices_idx < layers[layer].indices.size());
      const int src_nodes_idx = layers[layer].indices[dst_indices_idx];
      CHECK(src_nodes_idx >= 0) << src_nodes_idx;
      CHECK(src_nodes_idx < occurrences.size()) << src_nodes_idx << " vs "
                                                << occurrences.size();
      occurrences[src_nodes_idx].push_back(dst_indices_idx);
    }

    // printf("Sort layer %d...\n", layer);
    // fflush(stdout);

    // These can be in arbitrary order, but sort each subvector, for
    // locality of access and better compression.
    for (vector<uint32> &v : occurrences) {
      std::sort(v.begin(), v.end());
    }

    // printf("Flatten layer %d...\n", layer);
    // fflush(stdout);

    // Now flatten.
    int flat_size = 0;
    for (int src_nodes_idx = 0;
         src_nodes_idx < src_num_nodes;
         src_nodes_idx++) {
      CHECK(src_nodes_idx < start->size());
      CHECK(src_nodes_idx < length->size());
      (*start)[src_nodes_idx] = flat_size;
      (*length)[src_nodes_idx] = occurrences[src_nodes_idx].size();

      for (const int val : occurrences[src_nodes_idx]) {
        CHECK(flat_size < inverted->size());
        (*inverted)[flat_size] = val;
        flat_size++;
      }
    }
    CHECK_EQ(inverted->size(), flat_size);
  };

  UnParallelComp(num_layers, OneLayer, max_parallelism);
}

void Network::CheckInvertedIndices() const {
  for (int layer = 0; layer < num_layers; layer++) {
    const vector<uint32> &indices = layers[layer].indices;
    const LayerType dst_layer_type = layers[layer].type;
    const Network::InvertedIndices &inv = inverted_indices[layer];

    CHECK_EQ(inv.output_indices.size(), indices.size());
    // Need one start/length pair for every node in the source layer.
    CHECK_EQ(num_nodes[layer], inv.start.size());
    CHECK_EQ(num_nodes[layer], inv.length.size());
    // But the output size is determined by the next layer.
    if (dst_layer_type == CHUNK_CONVOLUTION_ARRAY) {
      const int dst_layer_nodes = num_nodes[layer + 1];
      CHECK(dst_layer_nodes % layers[layer].num_features == 0);
      CHECK_EQ((dst_layer_nodes / layers[layer].num_features) *
               layers[layer].indices_per_node,
               inv.output_indices.size());
    } else {
      CHECK_EQ(num_nodes[layer + 1] * layers[layer].indices_per_node,
               inv.output_indices.size());
    }
    // z is a node id from the src layer.
    for (int z = 0; z < inv.start.size(); z++) {
      // i is the index within the compacted inverted index.
      for (int i = inv.start[z]; i < inv.start[z] + inv.length[z]; i++) {
        // Global index into 'indices'.
        CHECK(i >= 0);
        CHECK(i < inv.output_indices.size());

        const int gidx = inv.output_indices[i];
        CHECK(gidx >= 0);
        CHECK(gidx < indices.size());
        // This should map back to our current node id.
        // (Should be true for sparse, dense, and convolution layers.)
        CHECK_EQ(indices[gidx], z);
      }
    }
  }
}

#endif

Network *Network::Clone(const Network &other) {
  return new Network(other);
}

int64 Network::Bytes() const {
  int64 ret = sizeof *this;
  // Layer structs.
  for (const Layer &layer : layers) {
    ret += sizeof layer;
    for (const Chunk &chunk : layer.chunks) {
      ret += sizeof chunk +
        sizeof chunk.indices[0] * chunk.indices.size() +
        sizeof chunk.weights[0] * chunk.weights.size() +
        sizeof chunk.biases[0] * chunk.biases.size();
    }
  }

  return ret;
}

int64 Network::TotalParameters() const {
  int64 params = 0LL;
  for (const Layer &layer : layers) {
    for (const Chunk &chunk : layer.chunks) {
      params += chunk.weights.size() + chunk.biases.size();
    }
  }
  return params;
}

void Network::RunForward(Stimulation *stim) const {
  // Not including final layer.
  for (int src = 0; src < layers.size() - 1; src++) {
    RunForwardLayer(stim, src);
  }
}

template<float (*fwd)(float)>
static void RunForwardChunkWithFn(
    const std::vector<float> &src_values,
    const Chunk &chunk,
    std::vector<float> *dst_values,
    int out_start) {

  switch (chunk.type) {
  case CHUNK_DENSE:
    UnParallelComp(chunk.num_nodes,
                   [&](int node_idx) {
        // Start with bias.
        float potential = chunk.biases[node_idx];
        const int my_weights = node_idx * chunk.indices_per_node;

        for (int i = 0; i < chunk.indices_per_node; i++) {
          const float w = chunk.weights[my_weights + i];
          const int srci = chunk.span_start + i;
          const float v = src_values[srci];
          potential += w * v;
        }

        const float out = fwd(potential);
        (*dst_values)[out_start + node_idx] = out;
      }, 2);
    break;

  case CHUNK_SPARSE:
    UnParallelComp(chunk.num_nodes,
                   [&](int node_idx) {
        // Start with bias.
        float potential = chunk.biases[node_idx];
        const int my_weights = node_idx * chunk.indices_per_node;
        const int my_indices = node_idx * chunk.indices_per_node;
        
        for (int i = 0; i < chunk.indices_per_node; i++) {
          const float w = chunk.weights[my_weights + i];
          const int srci = chunk.indices[my_indices + i];
          const float v = src_values[srci];
          potential += w * v;
        }

        const float out = fwd(potential);
        (*dst_values)[out_start + node_idx] = out;
      }, 2);
    break;

  case CHUNK_CONVOLUTION_ARRAY: {
    const int num_features = chunk.num_features;
    const int num_occurrences = chunk.num_occurrences_across *
      chunk.num_occurrences_down;

    ParallelComp(num_occurrences,
                 [&](int occurrence_number) {
        // Output features are interleaved.
        for (int feature_number = 0;
             feature_number < num_features;
             feature_number++) {
          const int node_idx =
            occurrence_number * num_features + feature_number;
          // Start with bias.
          float potential = chunk.biases[feature_number];
          // weights are feature-major
          const int my_weights = feature_number * chunk.indices_per_node;
          // same array of indices for each feature in the
          // occurrence
          const int my_indices = occurrence_number * chunk.indices_per_node;

          for (int i = 0; i < chunk.indices_per_node; i++) {
            const float w = chunk.weights[my_weights + i];
            const int srci = chunk.indices[my_indices + i];
            const float v = src_values[srci];
            potential += w * v;
          }

          const float out = fwd(potential);
          (*dst_values)[out_start + node_idx] = out;
        }
      }, 4);
    break;
  }

  case CHUNK_INPUT:
    CHECK(false) << "Should not run forward to the input layer?";
    break;

  default:
    CHECK(false) << "Unsupported layer type";
    break;
  }
}

static float SigmoidFn(float potential) {
  return 1.0f / (1.0f + expf(-potential));
}

static float ReluFn(float potential) {
  return (potential < 0.0f) ? 0.0f : potential;
}

static float LeakyReluFn(float potential) {
  return (potential < 0.0f) ? potential * 0.01f : potential;
}

static float IdentityFn(float potential) {
  return potential;
}

// TODO could make verbose version with template param?
void Network::RunForwardLayer(Stimulation *stim, int src_layer) const {
  const Layer &dst_layer = layers[src_layer + 1];
  int out_idx = 0;
  // Both using global indices.
  const std::vector<float> &src_values = stim->values[src_layer];
  std::vector<float> *dst_values = &stim->values[src_layer + 1];
  for (const Chunk &chunk : dst_layer.chunks) {
    const TransferFunction transfer_function =
      chunk.transfer_function;
    switch (transfer_function) {
    case SIGMOID:
      RunForwardChunkWithFn<SigmoidFn>(
          src_values, chunk, dst_values, out_idx);
      break;
    case RELU:
      RunForwardChunkWithFn<ReluFn>(
          src_values, chunk, dst_values, out_idx);
      break;
    case LEAKY_RELU:
      RunForwardChunkWithFn<LeakyReluFn>(
          src_values, chunk, dst_values, out_idx);
      break;
    case IDENTITY:
      RunForwardChunkWithFn<IdentityFn>(
          src_values, chunk, dst_values, out_idx);
      break;
    default:
      CHECK(false) << "Unimplemented transfer function " <<
        TransferFunctionName(transfer_function);
      break;
    };
    out_idx += chunk.num_nodes;
  }
}

void Network::NaNCheck(const std::string &message) const {
  bool has_nans = false;
  // this could be chunk-by-chunk if we want
  vector<std::pair<int, int>> layer_nans;
  vector<std::pair<int, int>> layer_denom;
  for (const Layer &layer : layers) {
    int w = 0, wn = 0, b = 0, bn = 0;
    for (const Chunk &chunk : layer.chunks) {
      for (float f : chunk.weights) if (std::isnan(f)) wn++;
      for (float f : chunk.biases) if (std::isnan(f)) bn++;
      w += chunk.weights.size();
      b += chunk.biases.size();
    }
    layer_nans.emplace_back(wn, bn);
    layer_denom.emplace_back(w, b);
    if (wn > 0 || bn > 0) has_nans = true;
  }
  if (has_nans) {
    string err;
    for (int i = 0; i < layer_nans.size(); i++) {
      const auto [wn, bn] = layer_nans[i];
      const auto [wd, bd] = layer_denom[i];
      err += StringPrintf("(real) layer %d. %d/%d weights, %d/%d biases\n",
                          i,
                          wn, wd, bn, bd);
    }
    CHECK(false) << "[" << message << "] The network has NaNs :-(\n" << err;
  }
}


void Network::StructuralCheck() const {
  // TODO: Other checks!
  CHECK(!layers.empty());
  CHECK(layers[0].chunks.size() == 1);
  CHECK(layers[0].chunks[0].type == CHUNK_INPUT);
  // TODO: Other checks for input layer?

  // All layers
  for (const Layer &layer : layers) {
    CHECK(layer.num_nodes > 0);
    CHECK(!layer.chunks.empty());

    int nodes_from_chunks = 0;
    for (const Chunk &chunk : layer.chunks) {
      nodes_from_chunks += chunk.num_nodes;
    }
    CHECK(nodes_from_chunks == layer.num_nodes);

    for (const Chunk &chunk : layer.chunks) {
      CHECK(chunk.num_nodes ==
            chunk.width * chunk.height * chunk.channels);
    }
  }

  // Real layers:
  for (int i = 1; i < layers.size(); i++) {
    const Layer &layer = layers[i];

    const int previous_layer_size = layers[i - 1].num_nodes;

    for (const Chunk &chunk : layer.chunks) {
      CHECK(chunk.span_start >= 0);
      CHECK(chunk.span_size > 0);
      CHECK(chunk.span_start < previous_layer_size);
      CHECK(chunk.span_start + chunk.span_size <= previous_layer_size);

      if (chunk.type == CHUNK_SPARSE) {
        CHECK(chunk.indices.size() == chunk.num_nodes * chunk.indices_per_node);
        // Pigeonhole
        CHECK(chunk.indices_per_node <= chunk.span_size);
      } else if (chunk.type == CHUNK_DENSE) {
        // Not stored for dense layers.
        CHECK(chunk.indices.empty());
        CHECK(chunk.indices_per_node == chunk.span_size);
      }

      // Check indices are in bounds. Indices are into the layer (not the span),
      // but must be in the range of the span.
      std::unordered_set<uint32> seen;
      for (const uint32 idx : chunk.indices) {
        CHECK(idx >= chunk.span_start);
        CHECK(idx < chunk.span_start + chunk.span_size);
        CHECK(!seen.contains(idx)) << "Duplicate index: " << idx;
        seen.insert(idx);
      }

      if (chunk.type == CHUNK_CONVOLUTION_ARRAY) {
        CHECK(chunk.num_features > 0);
        CHECK(chunk.pattern_width > 0);
        CHECK(chunk.pattern_height > 0);
        CHECK(chunk.src_width > 0);
        CHECK(chunk.src_height > 0);
        CHECK(chunk.occurrence_x_stride > 0);
        CHECK(chunk.occurrence_y_stride > 0);

        CHECK(chunk.num_occurrences_across > 0);
        CHECK(chunk.num_occurrences_down > 0);

        CHECK(chunk.biases.size() == chunk.num_features);
        CHECK(chunk.weights.size() == chunk.num_features *
              chunk.indices_per_node);
        CHECK(chunk.num_nodes % chunk.num_features == 0);
        CHECK(chunk.indices.size() ==
              (chunk.indices_per_node * chunk.num_nodes) /
              chunk.num_features);

        CHECK(chunk.indices_per_node ==
              chunk.pattern_width * chunk.pattern_height);

        CHECK(chunk.num_occurrences_across *
              chunk.num_occurrences_down *
              chunk.num_features == chunk.num_nodes);

        CHECK(chunk.src_width * chunk.src_height <= chunk.span_size);

        // TODO: Check the indices are what we expect, perhaps by
        // just calling MakeConvolutionArrayIndices?
      }
    }
  }
}

Chunk Network::MakeDenseChunk(int num_nodes,
                              int span_start, int span_size,
                              TransferFunction transfer_function) {
  Chunk chunk;
  chunk.num_nodes = num_nodes;
  chunk.span_start = span_start;
  chunk.span_size = span_size;
  chunk.indices_per_node = span_size;
  chunk.type = CHUNK_DENSE;
  chunk.transfer_function = transfer_function;
  chunk.weights.resize(num_nodes * span_size, 0.0f);
  chunk.biases.resize(num_nodes, 0.0f);
  chunk.width = num_nodes;
  chunk.height = 1;
  chunk.channels = 1;
  return chunk;
}

// static
std::tuple<std::vector<uint32_t>, int, int, int>
Network::MakeConvolutionArrayIndices(
    int span_start, int span_size,
    int num_features,
    int pattern_width,
    int pattern_height,
    int src_width,
    int src_height,
    int occurrence_x_stride,
    int occurrence_y_stride) {
  // Check for nonsensical inputs.
  CHECK(span_start >= 0);
  CHECK(span_size >= 1);
  CHECK(num_features >= 1);
  CHECK(pattern_width >= 1);
  CHECK(pattern_height >= 1);
  CHECK(src_width >= 1);
  CHECK(src_height >= 1);
  CHECK(occurrence_x_stride >= 1);
  CHECK(occurrence_y_stride >= 1);

  // Normally, equal.
  // (XXX perhaps we could even require this?)
  CHECK(src_width * src_height <= span_size);

  const int indices_per_node = pattern_width * pattern_height;

  const int num_occurrences_across = [&]() {
      int count = 0;
      int xpos = 0;
      // pattern_width-1 is the last index we actually read
      while (xpos + (pattern_width - 1) < src_width) {
        // ok position
        count++;
        xpos += occurrence_x_stride;
      }
      return count;
    }();
  const int num_occurrences_down = [&]() {
      int count = 0;
      int ypos = 0;
      // pattern_height-1 is the last index we actually read
      while (ypos + (pattern_height - 1) < src_height) {
        // ok position
        count++;
        ypos += occurrence_y_stride;
      }
      return count;
    }();

  CHECK(num_occurrences_down > 0);

  const int num_occurrences = num_occurrences_across * num_occurrences_down;
  const int this_num_nodes = num_occurrences * num_features;

  vector<uint32_t> indices;
  // Note: we store the indices once per occurrence; they are shared by
  // all the features!
  indices.reserve(num_occurrences * indices_per_node);
  // Output all the indices in the correct order.

  // Loop over occurrences.
  for (int occ_row = 0; occ_row < num_occurrences_down; occ_row++) {
    const int src_row = occ_row * occurrence_y_stride;
    for (int occ_col = 0; occ_col < num_occurrences_across; occ_col++) {

      const int src_col = occ_col * occurrence_x_stride;

      // Loop over pattern for each occurrence.
      const int src_start_offset = src_row * src_width + src_col;
      for (int y = 0; y < pattern_height; y++) {
        // Always the adjacent row.
        int src_offset = src_start_offset + (y * src_width);
        for (int x = 0; x < pattern_width; x++) {
          indices.push_back(span_start + src_offset);
          src_offset++;
        }
      }
    }
  }
  CHECK(indices.size() == num_occurrences * indices_per_node);

  return std::make_tuple(std::move(indices),
                         this_num_nodes,
                         num_occurrences_across,
                         num_occurrences_down);
};


Network *Network::ReadFromFile(const string &filename,
                               bool verbose) {
  if (verbose) printf("Reading [%s]\n", filename.c_str());
  FILE *file = fopen(filename.c_str(), "rb");
  if (file == nullptr) {
    if (verbose) {
      printf("  ... failed. If it's present, there may be a "
             "permissions problem?\n");
      fflush(stdout);
    }
    return nullptr;
  }

  // TODO: Instead of CHECK, these could set some failed flag
  // and return zeroes, which allows more graceful failure.
  auto Read64 = [file]() {
    int64_t i;
    CHECK(!feof(file));
    CHECK(1 == fread(&i, 8, 1, file));
    return i;
  };
  auto Read32 = [file]() {
    int32_t i;
    CHECK(!feof(file));
    CHECK(1 == fread(&i, 4, 1, file));
    return i;
  };
  auto ReadFloat = [file]() {
    float value;
    CHECK(!feof(file));
    CHECK(1 == fread(&value, 4, 1, file));
    return value;
  };
  auto ReadFloats = [&ReadFloat](vector<float> *vec) {
    for (int i = 0; i < vec->size(); i++) {
      (*vec)[i] = ReadFloat();
    }
  };

  if (Read32() != Network::MAGIC) {
    if (verbose) printf("Not a serialized network!\n");
    return nullptr;
  }
  if (Read32() != Network::FORMAT_ID) {
    if (verbose) printf("Wrong format id!\n");
    return nullptr;
  }

  const int64 round = Read64();
  const int64 examples = Read64();
  // These values determine the size of the network vectors.
  const int file_num_layers = Read32();
  CHECK_GE(file_num_layers, 0);
  if (verbose) {
    printf("%s: %lld rounds, %lld examples, %d layers.\n",
           filename.c_str(), round, examples, file_num_layers);
  }

  vector<Layer> layers(file_num_layers);
  for (int i = 0; i < file_num_layers; i++) {
    if (verbose) printf("%s: Layer %d: ", filename.c_str(), i);
    Layer &layer = layers[i];
    layer.num_nodes = 0;
    const int num_chunks = Read32();

    layer.chunks.resize(num_chunks);
    for (Chunk &chunk : layer.chunks) {
      const int ct = Read32();
      CHECK(ct >= 0 && ct < NUM_CHUNK_TYPES) << ct;
      chunk.type = (ChunkType)ct;

      chunk.span_start = Read32();
      chunk.span_size = Read32();
      chunk.num_nodes = Read32();
      chunk.indices_per_node = Read32();
      const int tf = Read32();
      CHECK(tf >= 0 && tf <= NUM_TRANSFER_FUNCTIONS) << tf;
      chunk.transfer_function = (TransferFunction)tf;
      chunk.num_features = Read32();
      chunk.pattern_width = Read32();
      chunk.pattern_height = Read32();
      chunk.src_width = Read32();
      chunk.src_height = Read32();
      chunk.occurrence_x_stride = Read32();
      chunk.occurrence_y_stride = Read32();
      chunk.num_occurrences_across = Read32();
      chunk.num_occurrences_down = Read32();

      // Read (or derive) indices, and set the correct sizes for
      // weights and biases.
      CHECK(chunk.indices.empty());
      switch (chunk.type) {
      case CHUNK_CONVOLUTION_ARRAY: {
        // They are not stored on disk, but we have to derive them
        // here because they are stored in memory.
        // Indices are explicit but shared across all features.
        CHECK((chunk.num_nodes * chunk.indices_per_node) %
              chunk.num_features == 0);
        chunk.indices.resize((chunk.num_nodes * chunk.indices_per_node) /
                             chunk.num_features, 0);

        const auto [indices, this_num_nodes_computed,
                    num_occurrences_across, num_occurrences_down] =
          MakeConvolutionArrayIndices(
              chunk.span_start, chunk.span_size,
              chunk.num_features,
              chunk.pattern_width,
              chunk.pattern_height,
              chunk.src_width,
              chunk.src_height,
              chunk.occurrence_x_stride,
              chunk.occurrence_y_stride);

        CHECK(this_num_nodes_computed == chunk.num_nodes);

        CHECK(num_occurrences_across == chunk.num_occurrences_across &&
              num_occurrences_down == chunk.num_occurrences_down) <<
          "Wrong occurrences down/across; stored in file: " <<
          chunk.num_occurrences_across << " x " <<
          chunk.num_occurrences_down << "; computed: " <<
          num_occurrences_across << " x " << num_occurrences_down;
        CHECK(chunk.indices.size() == indices.size());
        chunk.indices = indices;

        // Shared weights.
        chunk.weights.resize(chunk.indices_per_node * chunk.num_features,
                             0.0f);
        chunk.biases.resize(chunk.num_features, 0.0f);
        break;
      }

      case CHUNK_SPARSE:
        chunk.indices.reserve(chunk.num_nodes * chunk.indices_per_node);
        for (int ii = 0; ii < chunk.num_nodes * chunk.indices_per_node;
             ii++) chunk.indices.push_back(Read32());
        chunk.weights.resize(chunk.num_nodes * chunk.indices_per_node);
        chunk.biases.resize(chunk.num_nodes);
        break;

      case CHUNK_DENSE:
        // No indices stored.
        chunk.weights.resize(chunk.num_nodes * chunk.indices_per_node);
        chunk.biases.resize(chunk.num_nodes);
        break;

      case CHUNK_INPUT:
        // (nothing)
        break;

      default:
        CHECK(false) << "Unknown layer type!";
      }

      // Read the appropriate number of weights and biases.
      ReadFloats(&chunk.weights);
      ReadFloats(&chunk.biases);

      chunk.width = Read32();
      chunk.height = Read32();
      chunk.channels = Read32();
      chunk.style = (RenderStyle)Read32();

      printf("%d %s %s ",
             chunk.indices_per_node,
             TransferFunctionName(chunk.transfer_function),
             ChunkTypeName(chunk.type));
      if (chunk.type == CHUNK_CONVOLUTION_ARRAY) {
        printf("(%d feat%s, %dx%d pat from %dx%d rect, +%d +%d) ",
               chunk.num_features,
               (chunk.num_features == 1) ? "" : "s",
               chunk.pattern_width, chunk.pattern_height,
               chunk.src_width, chunk.src_height,
               chunk.occurrence_x_stride, chunk.occurrence_y_stride);
      }


      layer.num_nodes += chunk.num_nodes;
    }


    // (XXX PERF only if verbose?)
    for (const Chunk &chunk : layer.chunks) {
      int64 large_weights = 0, large_biases = 0;

      static constexpr float LARGE_WEIGHT = 8.0f;
      static constexpr float LARGE_BIAS = 128.0f;

      for (float f : chunk.weights) {
        if (f > LARGE_WEIGHT || f < -LARGE_WEIGHT) {
          large_weights++;
        }
      }
      for (float f : chunk.biases) {
        if (f > LARGE_BIAS || f < -LARGE_BIAS) {
          large_biases++;
        }
      }

      if (large_weights > 0 || large_biases > 0) {
        printf("Warning: %lld large weights and %lld large biases\n",
               large_weights, large_biases);
      }
    }
  }
  if (verbose)
    printf("\n");

  fclose(file);

  if (verbose)
    printf("Read from %s.\n", filename.c_str());

  // Construct network.

  auto net = std::make_unique<Network>(layers);
  CHECK(net.get() != nullptr);

  net->rounds = round;
  net->examples = examples;

  if (verbose)
    printf("Check it:\n");
  net->StructuralCheck();

  if (verbose)
    printf("OK!\n");

  return net.release();
}

void Network::SaveToFile(const string &filename) {
  // Not portable, obviously.
  FILE *file = fopen(filename.c_str(), "wb");
  auto Write64 = [file](int64_t i) {
    CHECK(1 == fwrite(&i, 8, 1, file));
  };
  auto Write32 = [file](int32_t i) {
    CHECK(1 == fwrite(&i, 4, 1, file));
  };
  auto WriteFloat = [file](float value) {
    CHECK(1 == fwrite(&value, 4, 1, file));
  };
  auto WriteFloats = [&WriteFloat](const vector<float> &vec) {
    for (float f : vec)
      WriteFloat(f);
  };

  Write32(Network::MAGIC);
  Write32(Network::FORMAT_ID);
  Write64(rounds);
  Write64(examples);
  Write32(layers.size());

  for (const Layer &layer : layers) {
    // don't write layer's num_nodes; it's computable
    Write32(layer.chunks.size());
    for (const Chunk &chunk : layer.chunks) {
      Write32(chunk.type);
      Write32(chunk.span_start);
      Write32(chunk.span_size);
      Write32(chunk.num_nodes);
      Write32(chunk.indices_per_node);
      Write32(chunk.transfer_function);
      // Always write convolution stuff, even if not a
      // convolution type chunk.
      Write32(chunk.num_features);
      Write32(chunk.pattern_width);
      Write32(chunk.pattern_height);
      Write32(chunk.src_width);
      Write32(chunk.src_height);
      Write32(chunk.occurrence_x_stride);
      Write32(chunk.occurrence_y_stride);
      Write32(chunk.num_occurrences_across);
      Write32(chunk.num_occurrences_down);

      switch (chunk.type) {
      case CHUNK_CONVOLUTION_ARRAY:
        // Don't write convolution layers; the structure is computable.
        break;
      case CHUNK_SPARSE:
        for (const uint32 idx : chunk.indices) Write32(idx);
        break;
      case CHUNK_DENSE:
        // Nothing stored for dense layers.
        break;
      case CHUNK_INPUT:
        // (nothing)
        break;
      default:
        CHECK(false) << "Unknown layer type!";
      }
      WriteFloats(chunk.weights);
      WriteFloats(chunk.biases);

      Write32(chunk.width);
      Write32(chunk.height);
      Write32(chunk.channels);
      Write32(chunk.style);
    }
  }

  printf("Wrote %s.\n", filename.c_str());
  fclose(file);
}

int64 Stimulation::Bytes() const {
  int64 ret = sizeof *this;
  for (int i = 0; i < values.size(); i++) {
    ret += sizeof values[i] + sizeof values[i][0] * values[i].size();
  }
  return ret;
}

void Stimulation::NaNCheck(const std::string &message) const {
  bool has_nans = false;
  vector<int> layer_nans;
  for (const vector<float> &layer : values) {
    int v = 0;
    for (float f : layer) if (std::isnan(f)) v++;
    layer_nans.push_back(v);
    if (v > 0) has_nans = true;
  }
  if (has_nans) {
    string err;
    for (int i = 0; i < layer_nans.size(); i++) {
      err += StringPrintf("stim layer %d. %d/%d values\n",
                          i,
                          layer_nans[i], values[i].size());
    }
    CHECK(false) << "[" << message
                 << "] The stimulation has NaNs :-(\n" << err;
  }
}


int64 Errors::Bytes() const {
  int64 ret = sizeof *this;
  for (int i = 0; i < error.size(); i++)
    ret += sizeof error[i] + sizeof error[i][0] * error[i].size();
  return ret;
}
