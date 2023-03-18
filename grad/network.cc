
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

#include "half.h"

using half = half_float::half;

using namespace std;

using uint32 = uint32_t;
using int64 = int64_t;

const char *TransferFunctionName(TransferFunction tf) {
  switch (tf) {
  case SIGMOID: return "SIGMOID";
  case RELU: return "RELU";
  case LEAKY_RELU: return "LEAKY_RELU";
  case IDENTITY: return "IDENTITY";
  case TANH: return "TANH";
  case GRAD1: return "GRAD1";
  case DOWNSHIFT2: return "DOWNSHIFT2";
  case PLUS64: return "PLUS64";
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
  case YOGI: return "YOGI";
  default: return "??INVALID??";
  }
}

TransferFunction ParseTransferFunction(const std::string &s) {
  for (int i = 0; i < NUM_TRANSFER_FUNCTIONS; i++) {
    TransferFunction tf = (TransferFunction)i;
    if (s == TransferFunctionName(tf))
      return tf;
  }
  CHECK(false) << "Unknown transfer function: " << s;
  return SIGMOID;
}

// For these, potential will be already rounded to the nearest half.
// The output of FORWARD should also be rounded.

// PERF: native_recip? native_exp? It's likely that we can tolerate
// inaccuracy of certain sorts.
const char *const Network::SIGMOID_FN =
  "#define FORWARD(potential) ToHalf(1.0f / ToHalf(1.0f + ToHalf(exp(-potential))))\n"
  // This wants to be given the actual output value f(potential).
  // "#define DERIVATIVE(fx) (fx * (1.0f - fx))\n";
  // XXX HAX!@!
  "#define DERIVATIVE(fx) ((fx * (1.0f - fx)) + 0.01f)\n";

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
  "#define FORWARD(potential) ((potential < 0.0f) ? ToHalf(potential * 0.01f) : potential)\n"
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

// This also has a nice version in terms of the output:
// 1 - tanh^2(x)   (which is possibly confusing notation for 1 - (tanh(x))^2).
const char *const Network::TANH_FN =
  // TODO: Make sure the common subexpressions are eliminated in opencl, or else
  // do it manually!
  // "#define FORWARD(potential) ((exp(potential) - exp(-(potential)))/(exp(potential) + exp(-(potential))))\n"
  // "#define FORWARD(potential) ((exp(2.0f * potential) - 1.0f) / (exp(2.0f * potential) + 1.0f))\n"
  // PERF native_tanh?
  "#define FORWARD(potential) ToHalf(tanh(potential))\n"
  "#define DERIVATIVE(fx) (1.0f - fx * fx)\n";

const char *const Network::GRAD1_FN =
  "#define FORWARD(potential) vload_half(FloatToU16(potential), forward_table)\n"
  "#define DERIVATIVE(fx) deriv_table[FloatToU16(fx)]\n";

const char *const Network::DOWNSHIFT2_FN =
  // Computing it on the fly is presumably faster than reading from the
  // table, but I didn't benchmark it.
  "#define FORWARD(potential) U16ToFloat(FloatToU16(potential) >> 2)\n"
  "#define DERIVATIVE(fx) deriv_table[FloatToU16(fx)]\n";

const char *const Network::PLUS64_FN =
  // Computing it on the fly is presumably faster than reading from the
  // table, but I didn't benchmark it.
  // PERF: Might be equivalent to avoid the outer ToHalf.
  "#define FORWARD(potential) ToHalf(ToHalf(potential + 64.0f) - 64.0f)\n"
  // The derivative is degenerate (0 everywhere except at discontinuities)
  // unless we apply significant smoothing. Even if we did that, it's not
  // possible to express it in terms of the output since there are only a
  // handful of discrete values. So we use an approximate analytical
  // derivative, which might still work (?).
  "#define DERIVATIVE(fx) 1.0f\n";

string Network::TransferFunctionDefines(TransferFunction tf) {
  switch (tf) {
  case SIGMOID: return Network::SIGMOID_FN;
  case RELU: return Network::RELU_FN;
  case LEAKY_RELU: return Network::LEAKY_RELU_FN;
  case IDENTITY: return Network::IDENTITY_FN;
  case TANH: return Network::TANH_FN;
  case GRAD1: return Network::GRAD1_FN;
  case DOWNSHIFT2: return Network::DOWNSHIFT2_FN;
  case PLUS64: return Network::PLUS64_FN;
  default:
    CHECK(false) << "No define for transfer function "
                 << tf << ": " << TransferFunctionName(tf);
    return "#error no define\n";
  }
}

Network::Network(vector<Layer> layers_in) :
  layers(std::move(layers_in)) {
  StructuralCheck();
}

InvertedIndices Network::ComputeInvertedIndices(int dst_layer_idx,
                                                int chunk_idx) const {
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

static inline half GetHalf(uint16 u) {
  half h;
  static_assert(sizeof (h) == sizeof (u));
  memcpy((void*)&h, (void*)&u, sizeof (u));
  return h;
}

static inline uint16 GetU16(half h) {
  uint16 u;
  static_assert(sizeof (h) == sizeof (u));
  memcpy((void*)&u, (void*)&h, sizeof (u));
  return u;
}

namespace {
struct Grad1Table {
  std::array<uint16_t, 65536> data;
  Grad1Table() {
    half mult = GetHalf(0x3bffu);
    half scale = GetHalf(0x3d4b);
    // half scale = GetHalf(0x3b60);
    for (int i = 0; i < 65536; i++) {
      half h = GetHalf(i);
      for (int i = 0; i < 500; i++)
        h *= mult;
      h *= scale;
      data[i] = GetU16(h);
    }
  }
};
}  // namespace

static float Grad1Fn(float f) {
  static Grad1Table *table = new Grad1Table;
  half in = (half)f;
  half out = GetHalf(table->data[GetU16(in)]);
  return (float)out;
}

static float Downshift2Fn(float f) {
  uint16_t in = GetU16((half)f);
  return (float)GetHalf(in >> 2);
}

static float Plus64Fn(float f) {
  half h = (half)f;
  h += GetHalf(0x5400);  // 64
  h += GetHalf(0xd400);  // -64
  return (float)h;
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

static float TanhFn(float potential) {
  /*
  const float ex = expf(potential);
  const float enx = expf(-potential);
  return (ex - enx)/(ex + enx);
  */
  const float e2x = exp(2.0f * potential);
  return (e2x - 1.0f) / (e2x + 1.0f);
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
    case TANH:
      RunForwardChunkWithFn<TanhFn>(
          src_values, chunk, dst_values, out_idx);
      break;
    case GRAD1:
      RunForwardChunkWithFn<Grad1Fn>(
          src_values, chunk, dst_values, out_idx);
      break;
    case DOWNSHIFT2:
      RunForwardChunkWithFn<Downshift2Fn>(
          src_values, chunk, dst_values, out_idx);
      break;
    case PLUS64:
      RunForwardChunkWithFn<Plus64Fn>(
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

      if (chunk.weight_update == ADAM ||
          chunk.weight_update == YOGI) {
        CHECK(chunk.weights.size() * 2 == chunk.weights_aux.size()) <<
          "On " << WeightUpdateName(chunk.weight_update) << " chunk "
                << layer_idx << "." << chunk_idx << ": " <<
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
  if (weight_update == ADAM || weight_update == YOGI) {
    chunk.weights_aux.resize(num_nodes * span_size * 2, 0.0f);
    chunk.biases_aux.resize(num_nodes * 2, 0.0f);
  }
  chunk.width = num_nodes;
  chunk.height = 1;
  chunk.channels = 1;
  return chunk;
}

Chunk Network::MakeRandomSparseChunk(
    ArcFour *rc,
    int num_nodes,
    const std::vector<SparseSpan> &spans,
    TransferFunction transfer_function,
    WeightUpdate weight_update) {

  CHECK(!spans.empty());

  // Check overlap, O(n^2).
  for (int s = 0; s < spans.size(); s++) {
    const SparseSpan &ss = spans[s];
    const int ss_end = ss.span_start + ss.span_size;
    for (int c = 0; c < s; c++) {
      const SparseSpan &os = spans[c];
      const int os_end = os.span_start + os.span_size;
      if (os_end <= ss.span_start ||
          os.span_start >= ss_end) {
        // disjoint, ok
      } else {
        LOG(FATAL) <<
          StringPrintf("SparseSpan %d (%d->%d) overlaps %d (%d->%d)",
                       s, ss.span_start, ss_end,
                       c, os.span_start, os_end);
      }
    }
  }

  int span_start = spans[0].span_start;
  int span_end = span_start + spans[0].span_size;
  int indices_per_node = 0;
  for (const SparseSpan &s : spans) {
    CHECK(s.ipn > 0);
    CHECK(s.ipn <= s.span_size);
    indices_per_node += s.ipn;
    int end = s.span_start + s.span_size;
    span_start = std::min(span_start, s.span_start);
    span_end = std::max(span_end, end);
  }

  CHECK(span_end > span_start);
  CHECK(num_nodes > 0);

  Chunk chunk;
  chunk.type = CHUNK_SPARSE;
  chunk.num_nodes = num_nodes;
  chunk.span_start = span_start;
  chunk.span_size = span_end - span_start;
  chunk.indices_per_node = indices_per_node;
  chunk.transfer_function = transfer_function;
  chunk.weight_update = weight_update;
  chunk.width = num_nodes;
  chunk.height = 1;
  chunk.channels = 1;
  chunk.fixed = false;

  chunk.indices.reserve(num_nodes * indices_per_node);
  for (int n = 0; n < num_nodes; n++) {
    // Same procedure for every node.
    vector<uint32_t> node_indices;
    node_indices.reserve(indices_per_node);
    for (const SparseSpan &s : spans) {
      // Depending on the span size and ipn, different
      // approaches could be much faster here (e.g. if
      // ipn is very small relative to the span size, or
      // equal to it), but this has fine worst-case
      // performance (always O(n)).
      // PERF: Could set up index_pool for each SparseSpan
      // once and keep reusing (reshuffling) it.
      vector<uint32_t> index_pool;
      index_pool.reserve(s.span_size);
      for (int i = 0; i < s.span_size; i++)
        index_pool.push_back(s.span_start + i);
      CHECK(index_pool.size() == s.span_size);
      Shuffle(rc, &index_pool);
      for (int i = 0; i < s.ipn; i++)
        node_indices.push_back(index_pool[i]);
    }
    std::sort(node_indices.begin(), node_indices.end());
    for (uint32_t idx : node_indices)
      chunk.indices.push_back(idx);
  }

  chunk.weights.resize(num_nodes * indices_per_node, 0.0f);
  chunk.biases.resize(num_nodes, 0.0f);
  if (weight_update == ADAM || weight_update == YOGI) {
    chunk.weights_aux.resize(chunk.weights.size() * 2, 0.0f);
    chunk.biases_aux.resize(chunk.biases.size() * 2, 0.0f);
  }

  return chunk;
}

std::pair<int, int>
Network::GetNumOccurrences(
    int pattern_width,
    int pattern_height,
    int src_width,
    int src_height,
    int occurrence_x_stride,
    int occurrence_y_stride) {
  CHECK(pattern_width >= 1);
  CHECK(pattern_height >= 1);
  CHECK(src_width >= 1);
  CHECK(src_height >= 1);
  CHECK(occurrence_x_stride >= 1);
  CHECK(occurrence_y_stride >= 1);

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

  return std::make_pair(num_occurrences_across,
                        num_occurrences_down);
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

  const auto [num_occurrences_across,
              num_occurrences_down] =
    GetNumOccurrences(
        pattern_width,
        pattern_height,
        src_width,
        src_height,
        occurrence_x_stride,
        occurrence_y_stride);

  CHECK(num_occurrences_across > 0);
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
  if (conv.weight_update == ADAM || weight_update == YOGI) {
    conv.weights_aux.resize(conv.weights.size() * 2, 0.0f);
    conv.biases_aux.resize(conv.biases.size() * 2, 0.0f);
  }

  conv.fixed = false;
  return conv;
}

Chunk Network::Make2DConvolutionChunk(int span_start, int span_width, int span_height,
                                      int num_features, int pattern_width, int pattern_height,
                                      int x_stride, int y_stride,
                                      TransferFunction transfer_function,
                                      WeightUpdate weight_update) {
  Chunk conv;
  conv.type = CHUNK_CONVOLUTION_ARRAY;
  conv.transfer_function = transfer_function;
  conv.num_features = num_features;
  conv.occurrence_x_stride = x_stride;
  conv.occurrence_y_stride = y_stride;
  conv.pattern_width = pattern_width;
  conv.pattern_height = pattern_height;
  conv.src_width = span_width;
  conv.src_height = span_height;
  conv.span_start = span_start;
  conv.span_size = span_width * span_height;
  conv.indices_per_node = pattern_width * pattern_height;

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
    // XXX probably output should be presented in 2D as well
    conv.width = conv.num_nodes;
    conv.height = 1;
    conv.channels = 1;

    conv.num_occurrences_across = num_occurrences_across;
    conv.num_occurrences_down = num_occurrences_down;
    conv.indices = std::move(indices);

    conv.weights = std::vector<float>(
        conv.indices_per_node * conv.num_features,
        0.0f);
    conv.biases = std::vector<float>(conv.num_features, 0.0f);
  }

  conv.weight_update = weight_update;
  if (conv.weight_update == ADAM || weight_update == YOGI) {
    conv.weights_aux.resize(conv.weights.size() * 2, 0.0f);
    conv.biases_aux.resize(conv.biases.size() * 2, 0.0f);
  }

  conv.fixed = false;
  return conv;
}

Chunk Network::MakeCopyChunk(int span_start, int span_size) {
  Chunk copy;
  copy.type = CHUNK_SPARSE;
  copy.num_nodes = span_size;
  copy.indices_per_node = 1;
  copy.span_start = span_start;
  copy.span_size = span_size;
  copy.width = span_size;
  copy.height = 1;
  copy.channels = 1;
  copy.weight_update = SGD;
  copy.transfer_function = IDENTITY;
  copy.indices.reserve(span_size);
  for (int i = 0; i < span_size; i++)
    copy.indices.push_back(span_start + i);
  copy.weights.resize(span_size, 1.0f);
  copy.biases.resize(span_size, 0.0f);

  copy.fixed = true;
  return copy;
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
  float ret = 0.0f;
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

      // For ADAM or YOGI, the aux parameters. We always have 2 per weight.
      if (chunk.weight_update == ADAM ||
          chunk.weight_update == YOGI) {
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

      if (r->verbose) {
        printf("%d %s %s %s%s ",
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

      if (r->verbose && (large_weights > 0 || large_biases > 0)) {
        printf("Warning: %lld large weights and %lld large biases\n",
               large_weights, large_biases);
      }
    }
    if (r->verbose) {
      printf("\n");
    }
  }

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

string RandomizationParams::ToString() const {
  return StringPrintf("{.sigmoid_uniform = %s, "
                      ".sigmoid_mag = %.11g, "
                      ".zeromean_uniform = %s, "
                      ".zeromean_numer = %.11g}",
                      sigmoid_uniform ? "true" : "false",
                      sigmoid_mag,
                      zeromean_uniform ? "true" : "false",
                      zeromean_numer);
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
void RandomizeNetwork(ArcFour *rc, Network *net,
                      RandomizationParams params,
                      int max_parallelism) {

  // This must access rc serially.
  vector<ArcFour *> rcs;
  rcs.reserve(net->layers.size());
  for (int i = 0; i < net->layers.size(); i++)
    rcs.push_back(Substream(rc, i));

  // But now we can do all layers in parallel.
  ParallelComp(
      net->layers.size(),
      [&params, rcs, &net](int layer) {
        printf("Layer %d: there are %d chunks\n",
               layer, (int)net->layers[layer].chunks.size());
        for (int chunk_idx = 0;
             chunk_idx < net->layers[layer].chunks.size();
             chunk_idx++) {
          Chunk *chunk = &net->layers[layer].chunks[chunk_idx];
          if (chunk->fixed)
            continue;

          // Standard advice is to leave biases at 0 to start.
          for (float &f : chunk->biases) f = 0.0f;

          // For sparse chunks, weights of exactly zero are just
          // wasting indices, as they do not affect the prediction nor
          // propagated error. Here we reject samples in (-hole,
          // +hole). Note that if the hole is nonempty, and mean is
          // nonzero, the generated samples with the hole rejected
          // will not actually have the requested mean.
          auto RandomizeFloatsDonut =
            [layer, chunk_idx](
                float mean, float hole, float mag, bool uniform,
                ArcFour *rc, vector<float> *vec) {
              printf("Layer %d chunk %d: mean %.5f, hole %.5f, mag %.5f, "
                     "%s\n", layer, chunk_idx, mean, hole, mag,
                     uniform ? "unif" : "gauss");
              RandomGaussian gauss(rc);
              CHECK(hole >= 0.0) << hole;
              CHECK(!uniform ||
                    (mean + mag) > hole ||
                    (mean - mag) < -hole) << "Won't get any samples!";
              for (int i = 0; i < vec->size(); i++) {
                float f = 0.0;
                do {
                  // Uniform in [-1,1] or gaussian with mean 0.
                  double d =
                    uniform ?
                    ((double)Rand32(rc) / (double)0xFFFFFFFF) * 2.0 - 1.0 :
                    gauss.Next();
                  f = (d - mean) * mag;
                } while (fabsf(f) < hole);
                (*vec)[i] = f;
              }
            };


          // Good weight initialization is important for training deep
          // models; if the initialized weights are too small, the
          // gradient vanishes and training stalls. If they are too
          // large, it explodes. One standard goal is to maintain that
          // the output variance is the same as the input variance
          // for each node.

          // XXX this is bogus!

          // The variance of a node depends on the number of indices
          // and the activation function. TANH and LINEAR activations
          // have zero mean; for these "Xavier initialization" is
          // appropriate. ("Understanding the difficulty of
          // training deep feedforward neural networks."
          // X. Glorot, Y.Bengio 2010) For RELU and LEAKY_RELU activations,
          // the mean is not zero; "He initialization" corrects for this
          // ("Delving Deep into Rectifiers: Surpassing Human-Level
          // Performance on ImageNet Classification." K. He et. al.,
          // https://arxiv.org/pdf/1502.01852v1.pdf ).
          //
          // SIGMOID is well-known for vanishing gradients. Yilmaz and
          // Poli (2022) recommend setting the mean nonzero (and
          // in particular, negative) to avoid vanishing gradients.
          // (I haven't seen this work yet, though.)

          const float hole = chunk->type == CHUNK_SPARSE ? 0.001f : 0.0f;

          switch (chunk->transfer_function) {
          default:
          case GRAD1:
            // Grad1 also has zero mean, I believe, but we
            // should maybe verify
          case TANH:
          case IDENTITY: {
            // Glorot and Bengio use sqrt(6) here, but their denominator
            // is sqrt of the number of nodes in this layer plus the
            // previous.
            const float numer = params.zeromean_numer;
            RandomizeFloatsDonut(0.0, hole,
                                 numer / sqrtf(chunk->indices_per_node),
                                 true, rcs[layer], &chunk->weights);
            break;
          }

          case SIGMOID: {
            // Yilmaz and Poli.
            const float mean =
              std::max(-1.0f, -8.0f / chunk->indices_per_node);
            // The paper recommends stddev = 0.1.
            const float mag = params.sigmoid_mag;
            RandomizeFloatsDonut(mean, hole, mag,
                                 params.sigmoid_uniform,
                                 rcs[layer], &chunk->weights);
            break;
          }

          case DOWNSHIFT2:
            // Downshift2 is definitely not symmetric so using
            // something like relu makes more sense. Can we
            // compute this empirically? Doesn't matter?
          case RELU:
          case LEAKY_RELU: {
            // XXX parameterize numerator
            const float mag = sqrtf(2.0 / chunk->indices_per_node);
            RandomizeFloatsDonut(0.0f, hole, mag, true,
                                 rcs[layer], &chunk->weights);
            break;
          }
          }
        }
      }, max_parallelism);

  DeleteElements(&rcs);
}

// Representing a linear (affine) function of the input layer.
namespace {
struct Lin {
  Lin(int size) : weights(size, 0.0) {}
  // Weighted sum.
  std::vector<double> weights;
  // ... plus this bias term.
  double bias = 0.0;

  void Transfer(TransferFunction tf) {
    switch (tf) {
    default:
      CHECK(false) << TransferFunctionName(tf) << " unimplemented.";
      break;
    case SIGMOID:
    case TANH:
    case LEAKY_RELU:
    case RELU:
    case DOWNSHIFT2:
      CHECK(false) << TransferFunctionName(tf) << " is not linear.";
      break;
    case IDENTITY:
    case PLUS64:
      // Nothing to do, as the function is mathematically f(x) = x.
      break;
    case GRAD1:
      // On the one hand we've rescaled it such that f(1) = 1 in practice,
      // but the ideologically consistent thing to do here is the
      // result of all the linear operations therein, which is to
      // first multiply by (1-1/2048)^500, then by the normalizing
      // constant. These don't match, so it's about 1.037.
      auto MG = [](double d) -> double {
          return d * 0.78333075731269282352007446863408 * 1.3232421875;
        };
      for (double &w : weights) w = MG(w);
      bias = MG(bias);
      break;
    }
  }
};
}  // namespace

static Lin operator +(const Lin &a, const Lin &b) {
  Lin ret = a;
  for (int i = 0; i < a.weights.size(); i++)
    ret.weights[i] += b.weights[i];
  ret.bias += b.bias;
  return ret;
}

static Lin &operator +=(Lin &a, const Lin &b) {
  for (int i = 0; i < a.weights.size(); i++)
    a.weights[i] += b.weights[i];
  a.bias += b.bias;
  return a;
}

static Lin operator *(const Lin &a, double s) {
  Lin ret = a;
  for (double &w : ret.weights) w *= s;
  ret.bias *= s;
  return ret;
}

bool Network::CanFlatten(const Network &net) {
  for (const Layer &layer : net.layers) {
    for (const Chunk &chunk : layer.chunks) {
      if (chunk.type != CHUNK_INPUT) {
        // XXX FIX
        if (chunk.type != CHUNK_DENSE &&
            chunk.type != CHUNK_SPARSE)
          return false;

        switch (chunk.transfer_function) {
        case IDENTITY:
        case PLUS64:
        case GRAD1:
          // OK
          break;
        case SIGMOID:
        case TANH:
        case LEAKY_RELU:
        case RELU:
        case DOWNSHIFT2:
          return false;
          break;
        default:
          CHECK(false) << "Not implemented: "
                       << TransferFunctionName(chunk.transfer_function);
        }
      }
    }
  }
  return true;
}

Network Network::Flatten(const Network &net) {
  std::vector<Layer> ret;
  // Input layer stays the same.
  ret.push_back(net.layers[0]);

  const int input_nodes = net.layers[0].num_nodes;

  // Single output chunk, the same size as the original output layer.
  // For simplicity, it depends on every node in the input.
  Chunk out = MakeDenseChunk(net.layers.back().num_nodes,
                             0, input_nodes,
                             IDENTITY,
                             SGD);

  // Now set its weights.
  // Each node will end up being some linear function of all the
  // inputs. A simple way to do this is to just recursively
  // compute that. But since there are exponentially many paths
  // to the input layer, this is crazy slow. Instead, we work from
  // the input layer forward, computing each layer as array of Lin.
  // In essence, this is the forward pass, but representing each
  // activation as a linear function of the input, rather than
  // a single scalar value.

  // Identity matrix for input.
  std::vector<Lin> prev_layer;
  prev_layer.reserve(input_nodes);
  for (int idx = 0; idx < input_nodes; idx++) {
    Lin lin(input_nodes);
    lin.bias = 0.0;
    lin.weights[idx] = 1.0;
    prev_layer.push_back(std::move(lin));
  }
  printf("(start) prev_layer size: %d\n", prev_layer.size());

  for (int layer_idx = 1; layer_idx < net.layers.size(); layer_idx++) {
    // Loop invariant: prev_layer is the solution so far for layer_idx-1.
    const int layer_nodes = net.layers[layer_idx].num_nodes;
    // We build this incrementally by pushing so that we don't
    // need to keep track of so many ids.
    std::vector<Lin> this_layer;
    this_layer.reserve(layer_nodes);
    for (const Chunk &chunk : net.layers[layer_idx].chunks) {
      switch (chunk.type) {
      case CHUNK_INPUT: CHECK(false) << "Input chunk after first layer.";
        break;
      case CHUNK_DENSE:
        for (int idx = 0; idx < chunk.num_nodes; idx++) {
          Lin lin(input_nodes);
          lin.bias = chunk.biases[idx];
          for (int off = 0; off < chunk.indices_per_node; off++) {
            const int src_idx = chunk.span_start + off;
            const int widx = idx * chunk.indices_per_node + off;
            const float weight = chunk.weights[widx];
            lin += prev_layer[src_idx] * weight;
          }

          // And the transfer function.
          lin.Transfer(chunk.transfer_function);

          this_layer.push_back(std::move(lin));
        }
        break;
      case CHUNK_SPARSE:
        for (int idx = 0; idx < chunk.num_nodes; idx++) {
          Lin lin(input_nodes);
          lin.bias = chunk.biases[idx];
          for (int off = 0; off < chunk.indices_per_node; off++) {
            const int widx = idx * chunk.indices_per_node + off;
            CHECK(widx < chunk.indices.size());
            CHECK(widx < chunk.weights.size());
            // span_start is already included in the index
            const int src_idx = chunk.indices[widx];
            CHECK(src_idx < prev_layer.size());
            const float weight = chunk.weights[widx];
            lin += prev_layer[src_idx] * weight;
          }

          // And the transfer function.
          lin.Transfer(chunk.transfer_function);

          this_layer.push_back(std::move(lin));
        }
        break;

      case CHUNK_CONVOLUTION_ARRAY:
        CHECK(false) << "Unimplemented: conv.";
        break;
      default:
        CHECK(false) << "Unimplemented chunk type "
                     << ChunkTypeName(chunk.type);
      }
    }

    CHECK(this_layer.size() == layer_nodes);
    prev_layer = std::move(this_layer);
  }

  printf("(start) prev_layer size: %d\n", prev_layer.size());

  // So now prev_layer represents the linear function of the input
  // layer that we wanted.
  Chunk chunk = MakeDenseChunk(prev_layer.size(),
                               // span is entire input
                               0, input_nodes,
                               // by definition the identity transfer
                               // function
                               IDENTITY,
                               // probably aren't going to train on
                               // this.
                               SGD);
  chunk.fixed = true;

  // Copy weights and biases.
  int widx = 0, bidx = 0;
  for (const Lin &lin : prev_layer) {
    for (double w : lin.weights)
      chunk.weights[widx++] = w;
    chunk.biases[bidx++] = lin.bias;
  }

  CHECK(widx == chunk.weights.size()) << widx << " vs " << chunk.weights.size();
  CHECK(bidx == chunk.biases.size());

  ret.push_back(Network::LayerFromChunks(std::move(chunk)));

  // XXX HERE
  return Network(ret);
}
