
#include "network.h"

#include <cstdio>
#include <cmath>
#include <cstring>

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
#include "randutil.h"
#include "arcfour.h"

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

const char *WeightUpdateName(WeightUpdate wu) {
  switch (wu) {
  case SGD: return "SGD";
  case ADAM: return "ADAM";
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

  printf("Structural check...");
  StructuralCheck();
  printf("... OK!\n");
}

InvertedIndices Network::ComputeInvertedIndices(int dst_layer_idx,
                                                int chunk_idx) const {
  printf("ComputeInvertedIndices(layer %d, chunk %d)\n",
         dst_layer_idx, chunk_idx);
  if (dst_layer_idx == 0) {
    CHECK(chunk_idx == 0);
    return InvertedIndices();
  }

  CHECK_GE(dst_layer_idx, 0);
  CHECK_LT(dst_layer_idx, layers.size());

  const Layer &dst_layer = layers[dst_layer_idx];
  CHECK(chunk_idx >= 0 && chunk_idx < dst_layer.chunks.size());
  const Chunk &chunk = dst_layer.chunks[chunk_idx];

  CHECK(chunk.type != CHUNK_INPUT) <<
    "INPUT chunk should only be on layer 0.";

  if (chunk.type == CHUNK_DENSE) {
    // No indices are stored, so an empty InvertedIndices as well. It
    // would make sense to fall through to the code below, which would
    // result in filling the start and length fields with SPAN_SIZE
    // zeroes. But we don't have any use for those, so we save the
    // memory and this is just the documented behavior.
    return InvertedIndices();
  }

  // Otherwise, regardless of the layer type, we just invert the
  // indices array.

  // Indexed by node id in the source span.
  vector<vector<uint32>> occurrences;
  occurrences.resize(chunk.span_size);

  for (int dst_indices_idx = 0;
       dst_indices_idx < chunk.indices.size();
       dst_indices_idx++) {
    // The indices are global to the input layer, but we want them
    // within the span.
    const int src_global_idx = chunk.indices[dst_indices_idx];
    const int src_span_idx = src_global_idx - chunk.span_start;
    CHECK(src_span_idx >= 0 && src_span_idx < chunk.span_size) <<
      src_global_idx << " was outside the span (start " <<
      chunk.span_start << " size " << chunk.span_size << ")";
    occurrences[src_span_idx].push_back(dst_indices_idx);
  }

  // These can be in arbitrary order, but sort each subvector, for
  // locality of access and better compression.
  for (vector<uint32> &v : occurrences) {
    std::sort(v.begin(), v.end());
  }

  // Now flatten into the InvertedIndices structure.
  InvertedIndices ret;
  for (int src_span_idx = 0;
       src_span_idx < chunk.span_size;
       src_span_idx++) {
    // Next position.
    ret.start.push_back(ret.output_indices.size());
    ret.length.push_back(occurrences[src_span_idx].size());

    for (const int val : occurrences[src_span_idx]) {
      ret.output_indices.push_back(val);
    }
  }
  CHECK_EQ(ret.output_indices.size(), chunk.indices.size());
  CHECK_EQ(ret.start.size(), chunk.span_size);
  CHECK_EQ(ret.length.size(), chunk.span_size);

  return ret;
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

  // Some previous versions accidentally treated these as 32-bit signed
  // integers, eventually producing negative numbers. For examples,
  // it basically doesn't matter. For rounds, we use this in adam weight
  // adjustment.
  CHECK(rounds >= 0);
  CHECK(examples >= 0);

  // All layers
  for (int layer_idx = 0; layer_idx < layers.size(); layer_idx++) {
    const Layer &layer = layers[layer_idx];
    CHECK(layer.num_nodes > 0);
    CHECK(!layer.chunks.empty());

    int nodes_from_chunks = 0;
    for (const Chunk &chunk : layer.chunks) {
      nodes_from_chunks += chunk.num_nodes;
    }
    CHECK(nodes_from_chunks == layer.num_nodes) <<
      "On layer " << layer_idx << ": " <<
      nodes_from_chunks << " vs " << layer.num_nodes;

    for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
      const Chunk &chunk = layer.chunks[chunk_idx];
      CHECK(chunk.num_nodes ==
            chunk.width * chunk.height * chunk.channels) <<
        "On chunk " << layer_idx << "." << chunk_idx << ": " <<
        chunk.num_nodes << " vs " << chunk.width << " * " <<
        chunk.height << " * " << chunk.channels;

      if (chunk.weight_update == ADAM) {
        CHECK(chunk.weights.size() * 2 == chunk.weights_aux.size()) <<
          "On adam chunk " << layer_idx << "." << chunk_idx << ": " <<
          chunk.weights.size() << " * 2 " << " vs " <<
          chunk.weights_aux.size();
        CHECK(chunk.biases.size() * 2 ==
              chunk.biases_aux.size());
      }
    }
  }

  // Real layers:
  for (int layer_idx = 1; layer_idx < layers.size(); layer_idx++) {
    const Layer &layer = layers[layer_idx];

    const int previous_layer_size = layers[layer_idx - 1].num_nodes;

    for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
      const Chunk &chunk = layer.chunks[chunk_idx];
      CHECK(chunk.span_start >= 0);
      CHECK(chunk.span_size > 0) << "Layer " << layer_idx
                                 << " chunk " << chunk_idx;
      CHECK(chunk.span_start < previous_layer_size);
      CHECK(chunk.span_start + chunk.span_size <= previous_layer_size);

      if (chunk.type == CHUNK_SPARSE) {
        CHECK(chunk.indices.size() ==
              chunk.num_nodes * chunk.indices_per_node) <<
          "Layer " << layer_idx << ": " << chunk.indices.size() <<
          " indices but expected " << chunk.num_nodes <<
          " * " << chunk.indices_per_node;
        // Pigeonhole
        CHECK(chunk.indices_per_node <= chunk.span_size);

        // TODO: Similar test for CHUNK_CONVOLUTION_ARRAY
        for (int node = 0; node < chunk.num_nodes; node++) {
          std::unordered_set<uint32> seen;
          seen.reserve(chunk.indices_per_node);
          for (int i = 0; i < chunk.indices_per_node; i++) {
            int idx = chunk.indices[node * chunk.indices_per_node + i];
            CHECK(!seen.contains(idx)) << "Duplicate index: " << idx;
            seen.insert(idx);
          }
        }
        CHECK(chunk.weights.size() == chunk.indices.size());
        CHECK(chunk.biases.size() == chunk.num_nodes);
      } else if (chunk.type == CHUNK_DENSE) {
        // Not stored for dense layers.
        CHECK(chunk.indices.empty());
        CHECK(chunk.indices_per_node == chunk.span_size);
        CHECK(chunk.weights.size() == chunk.indices_per_node * chunk.num_nodes);
        CHECK(chunk.biases.size() == chunk.num_nodes);
      }

      // Check indices are in bounds. Indices are into the layer (not the span),
      // but must be in the range of the span.
      for (const uint32 idx : chunk.indices) {
        CHECK(idx >= chunk.span_start);
        CHECK(idx < chunk.span_start + chunk.span_size);
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
              chunk.indices_per_node) << chunk.weights.size() <<
          " weights but " << chunk.num_features << " features * " <<
          chunk.indices_per_node << " ipn = " <<
          (chunk.num_features * chunk.indices_per_node);
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
                              TransferFunction transfer_function,
                              WeightUpdate weight_update) {
  Chunk chunk;
  chunk.num_nodes = num_nodes;
  chunk.span_start = span_start;
  chunk.span_size = span_size;
  chunk.indices_per_node = span_size;
  chunk.type = CHUNK_DENSE;
  chunk.transfer_function = transfer_function;
  chunk.weights.resize(num_nodes * span_size, 0.0f);
  chunk.biases.resize(num_nodes, 0.0f);
  chunk.weight_update = weight_update;
  if (weight_update == ADAM) {
    chunk.weights.resize(num_nodes * span_size * 2, 0.0f);
    chunk.biases_aux.resize(num_nodes * 2, 0.0f);
  }
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
  CHECK(src_width * src_height <= span_size) <<
    src_width << " * " << src_height << " <= "  << span_size;

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

Chunk Network::Make1DConvolutionChunk(int span_start, int span_size,
                                      int num_features, int pattern_width,
                                      int x_stride,
                                      TransferFunction transfer_function,
                                      WeightUpdate weight_update) {
  Chunk conv;
  conv.type = CHUNK_CONVOLUTION_ARRAY;
  conv.transfer_function = transfer_function;
  conv.num_features = num_features;
  conv.occurrence_x_stride = x_stride;
  conv.occurrence_y_stride = 1;
  conv.pattern_width = pattern_width;
  conv.pattern_height = 1;
  conv.src_width = span_size;
  conv.src_height = 1;
  conv.span_start = span_start;
  conv.span_size = span_size;
  conv.indices_per_node = pattern_width;

  {
    auto [indices, this_num_nodes,
          num_occurrences_across, num_occurrences_down] =
      MakeConvolutionArrayIndices(conv.span_start,
                                  conv.span_size,
                                  conv.num_features,
                                  conv.pattern_width,
                                  conv.pattern_height,
                                  conv.src_width,
                                  conv.src_height,
                                  conv.occurrence_x_stride,
                                  conv.occurrence_y_stride);
    CHECK(this_num_nodes == 
          num_features * num_occurrences_across * num_occurrences_down);
    conv.num_nodes = this_num_nodes;
    conv.width = conv.num_nodes;
    conv.height = 1;
    conv.channels = 1;

    conv.num_occurrences_across = num_occurrences_across;
    CHECK(num_occurrences_down == 1);
    conv.num_occurrences_down = num_occurrences_down;
    conv.indices = std::move(indices);

    conv.weights = std::vector<float>(
        conv.indices_per_node * conv.num_features,
        0.0f);
    conv.biases = std::vector<float>(conv.num_features, 0.0f);
  }

  conv.weight_update = weight_update;
  if (conv.weight_update == ADAM) {
    conv.weights_aux.resize(conv.weights.size() * 2, 0.0f);
    conv.biases_aux.resize(conv.biases.size() * 2, 0.0f);
  }

  conv.fixed = false;
  return conv;
}



// TODO: This is not that portable (assumes endianness for float
// is the same as int32?)
static inline uint32_t PackFloat(float f) {
  static_assert(sizeof (float) == 4);
  uint32_t ret = 0;
  std::memcpy(&ret, &f, sizeof (float));
  return ret;
}

static inline float UnpackFloat(uint32_t u) {
  static_assert(sizeof (float) == 4);
  float ret = 0;
  std::memcpy(&ret, &u, sizeof (float));
  return ret;
}

namespace {
struct Reader {
  explicit Reader(const std::string &name, bool verbose) :
    name(name), verbose(verbose) {}

  virtual ~Reader() {}

  virtual uint8_t ReadByte() = 0;

  inline uint64_t Read64() {
    uint64_t a = Read32();
    uint64_t b = Read32();
    return (a << 32) | b;
  }

  inline float ReadFloat() {
    uint32_t u = Read32();
    return UnpackFloat(u);
  }

  inline uint32_t Read32() {
    uint8_t a = ReadByte();
    uint8_t b = ReadByte();
    uint8_t c = ReadByte();
    uint8_t d = ReadByte();

    return (a << 24) | (b << 16) | (c << 8) | d;
  }

  inline void ReadFloats(std::vector<float> *vec) {
    for (int i = 0; i < vec->size(); i++) {
      (*vec)[i] = ReadFloat();
    }
  }

  inline const std::string &Name() const { return name; }

  const std::string name;
  const bool verbose = false;
};

struct FileReader : public Reader {
  static Reader *Create(const std::string &filename,
                        bool verbose = false) {
    FILE *file = fopen(filename.c_str(), "rb");
    if (file == nullptr) {
      if (verbose) {
        printf("Failed to open %s. If it's present, there may be a "
               "permissions problem?\n", filename.c_str());
        fflush(stdout);
      }
      return nullptr;
    } else {
      if (verbose) {
        printf("Reading [%s]\n", filename.c_str());
      }
    }
    return new FileReader(file, filename, verbose);
  }

  FileReader(FILE *file, const std::string filename, bool verbose) :
    Reader(filename, verbose), file(file) {}

  ~FileReader() override {
    fclose(file);
  }


  // TODO: Instead of CHECK, this could set some failed flag
  // and return zeroes, which allows more graceful failure.
  uint8_t ReadByte() override {
    uint8_t i;
    CHECK(!feof(file));
    CHECK(1 == fread(&i, 1, 1, file));
    return i;
  }

  FILE *file = nullptr;
};

struct VecReader : public Reader {
  static Reader *Create(const std::vector<uint8_t> &bytes,
                        bool verbose = false) {
    return new VecReader(bytes, verbose);
  }

  uint8_t ReadByte() override {
    CHECK(pos < bytes.size());
    return bytes[pos++];
  }

  explicit VecReader(const std::vector<uint8_t> &bytes,
                     bool verbose = false) :
    Reader("memory", verbose), bytes(bytes) {}
  const std::vector<uint8_t> &bytes;
  int64_t pos = 0;
};

struct Writer {
  explicit Writer(const std::string &name, bool verbose) :
    name(name), verbose(verbose) {}
  virtual ~Writer() {}

  virtual void WriteByte(uint8_t b) = 0;

  inline void Write64(uint64_t i) {
    Write32((i >> 32) & 0xFFFFFFFF);
    Write32(i & 0xFFFFFFFF);
  }
  inline void Write32(uint32_t i) {
    WriteByte((i >> 24) & 0xFF);
    WriteByte((i >> 16) & 0xFF);
    WriteByte((i >> 8)  & 0xFF);
    WriteByte( i        & 0xFF);
  }
  inline void WriteFloat(float value) {
    Write32(PackFloat(value));
  }
  inline void WriteFloats(const vector<float> &vec) {
    for (float f : vec)
      WriteFloat(f);
  }

  const std::string &Name() const { return name; }

  const std::string name;
  const bool verbose = false;
};

struct FileWriter : public Writer {
  static Writer *Create(const std::string &filename,
                        bool verbose = false) {
    FILE *file = fopen(filename.c_str(), "wb");
    if (file == nullptr) {
      if (verbose) {
        printf("Failed to open %s for writing?", filename.c_str());
        fflush(stdout);
      }
      return nullptr;
    }
    return new FileWriter(file, filename, verbose);
  }

  FileWriter(FILE *file, const std::string filename, bool verbose) :
    Writer(filename, verbose), file(file) {}

  ~FileWriter() override {
    fclose(file);
  }

  // TODO: Instead of CHECK, this could set some failed flag
  // and return zeroes, which allows more graceful failure.
  void WriteByte(uint8_t b) override {
    CHECK(1 == fwrite(&b, 1, 1, file)) << Name();
  }

  FILE *file = nullptr;
};

struct VecWriter : public Writer {
  static Writer *Create(std::vector<uint8_t> *vec, bool verbose = false) {
    return new VecWriter(vec, verbose);
  }

  inline void WriteByte(uint8_t b) override {
    vec->push_back(b);
  }

  explicit VecWriter(std::vector<uint8_t> *vec,
                     bool verbose = false) :
    Writer("memory", verbose), vec(vec) {}

  std::vector<uint8_t> *vec = nullptr;
};

}  // namespace

static Network *ReadFromReader(Reader *r) {
  if (r->Read32() != Network::MAGIC) {
    if (r->verbose) printf("Not a serialized network!\n");
    return nullptr;
  }

  if (r->Read32() != Network::FORMAT_ID) {
    if (r->verbose) printf("Wrong format id!\n");
    return nullptr;
  }

  const int64 round = r->Read64();
  const int64 examples = r->Read64();
  // These values determine the size of the network vectors.
  const int file_num_layers = r->Read32();
  CHECK_GE(file_num_layers, 0);
  if (r->verbose) {
    printf("%s: %lld rounds, %lld examples, %d layers.\n",
           r->Name().c_str(), round, examples, file_num_layers);
  }

  vector<Layer> layers(file_num_layers);
  for (int i = 0; i < file_num_layers; i++) {
    if (r->verbose) printf("%s: Layer %d: ", r->Name().c_str(), i);
    Layer &layer = layers[i];
    layer.num_nodes = 0;
    const int num_chunks = r->Read32();

    layer.chunks.resize(num_chunks);
    for (Chunk &chunk : layer.chunks) {
      const int ct = r->Read32();
      CHECK(ct >= 0 && ct < NUM_CHUNK_TYPES) << ct;
      chunk.type = (ChunkType)ct;

      chunk.span_start = r->Read32();
      chunk.span_size = r->Read32();
      chunk.num_nodes = r->Read32();
      chunk.indices_per_node = r->Read32();
      const int tf = r->Read32();
      CHECK(tf >= 0 && tf <= NUM_TRANSFER_FUNCTIONS) << tf;
      chunk.transfer_function = (TransferFunction)tf;
      const int wu = r->Read32();
      CHECK(wu >= 0 && wu <= NUM_WEIGHT_UPDATES) << wu;
      chunk.weight_update = (WeightUpdate)wu;

      chunk.num_features = r->Read32();
      chunk.pattern_width = r->Read32();
      chunk.pattern_height = r->Read32();
      chunk.src_width = r->Read32();
      chunk.src_height = r->Read32();
      chunk.occurrence_x_stride = r->Read32();
      chunk.occurrence_y_stride = r->Read32();
      chunk.num_occurrences_across = r->Read32();
      chunk.num_occurrences_down = r->Read32();

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
          Network::MakeConvolutionArrayIndices(
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
             ii++) chunk.indices.push_back(r->Read32());
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
      r->ReadFloats(&chunk.weights);
      r->ReadFloats(&chunk.biases);

      // For ADAM, the aux parameters. We always have 2 per weight.
      if (chunk.weight_update == ADAM) {
        chunk.weights_aux.resize(chunk.weights.size() * 2);
        chunk.biases_aux.resize(chunk.biases.size() * 2);

        r->ReadFloats(&chunk.weights_aux);
        r->ReadFloats(&chunk.biases_aux);
      }

      chunk.width = r->Read32();
      chunk.height = r->Read32();
      chunk.channels = r->Read32();
      chunk.style = (RenderStyle)r->Read32();

      chunk.fixed = r->Read32() != 0;

      printf("%d %s %s %s%s",
             chunk.indices_per_node,
             TransferFunctionName(chunk.transfer_function),
             ChunkTypeName(chunk.type),
             WeightUpdateName(chunk.weight_update),
             chunk.fixed ? " [F]" : "");
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
  if (r->verbose)
    printf("\n");

  if (r->verbose)
    printf("Read from %s.\n", r->Name().c_str());

  // Construct network.

  auto net = std::make_unique<Network>(layers);
  CHECK(net.get() != nullptr);

  net->rounds = round;
  net->examples = examples;

  if (r->verbose)
    printf("Check it:\n");
  net->StructuralCheck();

  if (r->verbose)
    printf("OK!\n");

  return net.release();
}


Network *Network::ReadFromFile(const string &filename,
                               bool verbose) {
  std::unique_ptr<Reader> r(FileReader::Create(filename, verbose));
  if (r.get() == nullptr) {
    if (verbose)
      printf("Couldn't read %s\n", filename.c_str());
    return nullptr;
  }
  return ReadFromReader(r.get());
}

Network *Network::ParseSerialized(const std::vector<uint8_t> &bytes,
                                  bool verbose) {
  std::unique_ptr<Reader> r(VecReader::Create(bytes, verbose));
  CHECK(r.get() != nullptr);
  return ReadFromReader(r.get());
}

static void WriteToWriter(const Network &net, Writer *w) {
  w->Write32(Network::MAGIC);
  w->Write32(Network::FORMAT_ID);
  w->Write64(net.rounds);
  w->Write64(net.examples);
  w->Write32(net.layers.size());

  for (const Layer &layer : net.layers) {
    // don't write layer's num_nodes; it's computable
    w->Write32(layer.chunks.size());
    for (const Chunk &chunk : layer.chunks) {
      w->Write32(chunk.type);
      w->Write32(chunk.span_start);
      w->Write32(chunk.span_size);
      w->Write32(chunk.num_nodes);
      w->Write32(chunk.indices_per_node);
      w->Write32(chunk.transfer_function);
      w->Write32(chunk.weight_update);

      // Always write convolution stuff, even if not a
      // convolution type chunk.
      w->Write32(chunk.num_features);
      w->Write32(chunk.pattern_width);
      w->Write32(chunk.pattern_height);
      w->Write32(chunk.src_width);
      w->Write32(chunk.src_height);
      w->Write32(chunk.occurrence_x_stride);
      w->Write32(chunk.occurrence_y_stride);
      w->Write32(chunk.num_occurrences_across);
      w->Write32(chunk.num_occurrences_down);

      switch (chunk.type) {
      case CHUNK_CONVOLUTION_ARRAY:
        // Don't write convolution layers; the structure is computable.
        break;
      case CHUNK_SPARSE:
        for (const uint32 idx : chunk.indices) w->Write32(idx);
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
      w->WriteFloats(chunk.weights);
      w->WriteFloats(chunk.biases);

      w->WriteFloats(chunk.weights_aux);
      w->WriteFloats(chunk.biases_aux);

      w->Write32(chunk.width);
      w->Write32(chunk.height);
      w->Write32(chunk.channels);
      w->Write32(chunk.style);

      w->Write32(chunk.fixed ? 1 : 0);
    }
  }

  if (w->verbose) printf("Wrote %s.\n", w->Name().c_str());
}

void Network::SaveToFile(const string &filename) {
  // XXX make verbosity optional?
  std::unique_ptr<Writer> w(FileWriter::Create(filename, true));
  CHECK(w.get() != nullptr) << filename;
  WriteToWriter(*this, w.get());
}

std::vector<uint8_t> Network::Serialize() const {
  std::vector<uint8_t> vec;
  vec.reserve(Bytes() + 128);
  std::unique_ptr<Writer> w(VecWriter::Create(&vec, false));
  CHECK(w.get() != nullptr);
  WriteToWriter(*this, w.get());
  return vec;
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

// .. utils
template<class C>
static void DeleteElements(C *cont) {
  for (auto &elt : *cont) {
    delete elt;
  }
  cont->clear();
}

// Randomize the weights in a network. Doesn't do anything to indices.
void RandomizeNetwork(ArcFour *rc, Network *net, int max_parallelism) {
  [[maybe_unused]]
  auto RandomizeFloatsGaussian =
    [](float mag, ArcFour *rc, vector<float> *vec) {
      RandomGaussian gauss{rc};
      for (int i = 0; i < vec->size(); i++) {
        (*vec)[i] = mag * gauss.Next();
      }
    };

  [[maybe_unused]]
  auto RandomizeFloatsUniform =
    [](float mag, ArcFour *rc, vector<float> *vec) {
      // Uniform from -mag to mag.
      const float width = 2.0f * mag;
      for (int i = 0; i < vec->size(); i++) {
        // Uniform in [0,1]
        double d = (double)Rand32(rc) / (double)0xFFFFFFFF;
        float f = (width * d) - mag;
        (*vec)[i] = f;
      }
    };

  // This must access rc serially.
  vector<ArcFour *> rcs;
  rcs.reserve(net->layers.size());
  for (int i = 0; i < net->layers.size(); i++)
    rcs.push_back(Substream(rc, i));

  // But now we can do all layers in parallel.
  ParallelComp(
      net->layers.size(),
      [rcs, &RandomizeFloatsUniform, &net](int layer) {
        for (int chunk_idx = 0;
             chunk_idx < net->layers[layer].chunks.size();
             chunk_idx++) {
          Chunk *chunk = &net->layers[layer].chunks[chunk_idx];
          if (chunk->fixed)
            continue;

          // XXX instead rely on 'fixed' field.
          if (chunk->transfer_function == IDENTITY)
            continue;

          // XXX such hacks. How to best initialize?

          // Standard advice is to leave biases at 0 to start.
          for (float &f : chunk->biases) f = 0.0f;

          // The more indices we have, the smaller initial weights we
          // should use. "Xavier initialization"
          const float mag = 1.0f / sqrtf(chunk->indices_per_node);
          // "He initialization"
          // const float mag = sqrtf(2.0 / net->layers[layer].indices_per_node);
          // Tom initialization
          // const float mag = (0.0125f / net->layers[layer].indices_per_node);
          RandomizeFloatsUniform(mag, rcs[layer], &chunk->weights);
        }
      }, max_parallelism);

  DeleteElements(&rcs);
}
