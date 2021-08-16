
#include "network.h"

#include <cstdio>
#include <cmath>

#include <algorithm>
#include <utility>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

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
  default: return "??INVALID??";
  }
}

const char *LayerTypeName(LayerType lt) {
  switch (lt) {
  case LAYER_DENSE: return "LAYER_DENSE";
  case LAYER_SPARSE: return "LAYER_SPARSE";
  case LAYER_CONVOLUTION_ARRAY: return "LAYER_CONVOLUTION_ARRAY";
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

string Network::TransferFunctionDefines(TransferFunction tf) {
  switch (tf) {
  case SIGMOID: return Network::SIGMOID_FN;
  case RELU: return Network::RELU_FN;
  case LEAKY_RELU: return Network::LEAKY_RELU_FN;
  default:
    CHECK(false) << "No define for transfer function "
                 << tf << ": " << TransferFunctionName(tf);
  }
}

Network::Network(vector<int> num_nodes_in,
                 vector<Layer> layers_in) :
  num_layers(layers_in.size()),
  num_nodes(num_nodes_in),
  layers(std::move(layers_in)) {

  CHECK(num_nodes.size() - 1 == layers.size()) <<
    "num_nodes vec: " << num_nodes.size() << " layers vec: "
                      << layers.size();
  CHECK(num_layers >= 1) << "Must include input layer.";

  // Make these valid (N x 1 x 1).
  for (int n : num_nodes) {
    width.push_back(n);
    height.push_back(1);
    channels.push_back(1);
    renderstyle.push_back(RENDERSTYLE_FLAT);
  }

  printf("Reallocate inverted\n");
  ReallocateInvertedIndices();
  printf("Compute inverted\n");
  fflush(stdout);
  ComputeInvertedIndices();
  printf("Structural check\n");
  StructuralCheck();
}

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
    if (dst_layer_type == LAYER_CONVOLUTION_ARRAY) {
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

Network *Network::Clone(const Network &other) {
  return new Network(other);
}

int64 Network::Bytes() const {
  int64 ret = sizeof *this;
  ret += sizeof num_nodes[0] * num_nodes.size();
  ret += sizeof width[0] * width.size();
  ret += sizeof height[0] * height.size();
  ret += sizeof channels[0] * channels.size();
  // Layer structs.
  for (int i = 0; i < num_layers; i++) {
    ret += sizeof layers[i] +
      sizeof layers[i].indices[0] * layers[i].indices.size() +
      sizeof layers[i].weights[0] * layers[i].weights.size() +
      sizeof layers[i].biases[0] * layers[i].biases.size();
  }
  // Inverted index structs.
  for (int i = 0; i < num_layers; i++) {
    ret += sizeof inverted_indices[i] +
      sizeof inverted_indices[i].start[0] * inverted_indices[i].start.size() +
      sizeof inverted_indices[i].length[0] * inverted_indices[i].length.size() +
      sizeof inverted_indices[i].output_indices[0] *
          inverted_indices[i].output_indices.size();
  }

  return ret;
}

int64 Network::TotalParameters() const {
  int64 params = 0LL;
  for (int i = 0; i < num_layers; i++) {
    params += layers[i].weights.size() + layers[i].biases.size();
  }
  return params;
}

void Network::RunForward(Stimulation *stim) const {
  for (int src = 0; src < num_layers; src++) {
    RunForwardLayer(stim, src);
  }
}

void Network::RunForwardLayer(Stimulation *stim, int src_layer) const {
  // PERF avoid dispatching on every node
  const TransferFunction transfer_function =
    layers[src_layer].transfer_function;
  auto Forward =
    [transfer_function](float potential) -> float {
      switch (transfer_function) {
      case SIGMOID:
        return 1.0f / (1.0f + expf(-potential));
      case RELU:
        return (potential < 0.0f) ? 0.0f : potential;
      case LEAKY_RELU:
        return (potential < 0.0f) ? potential * 0.01f : potential;
      default:
        CHECK(false) << "Unimplemented transfer function " <<
          TransferFunctionName(transfer_function);
        return 0.0f;
      }
    };

  const vector<float> &src_values = stim->values[src_layer];
  vector<float> *dst_values = &stim->values[src_layer + 1];
  const vector<float> &biases = layers[src_layer].biases;
  const vector<float> &weights = layers[src_layer].weights;
  const vector<uint32> &indices = layers[src_layer].indices;
  const int indices_per_node = layers[src_layer].indices_per_node;
  const int num_features = layers[src_layer].num_features;
  const int number_of_nodes = num_nodes[src_layer + 1];
  const LayerType layer_type = layers[src_layer].type;

  // PERF in parallel
  for (int node_idx = 0; node_idx < number_of_nodes; node_idx++) {
    // Output features are interleaved.
    const int feature_number = node_idx % num_features;
    // Start with bias.
    float potential = [&](){
        if (layer_type == LAYER_CONVOLUTION_ARRAY) {
          return biases[feature_number];
        } else {
          return biases[node_idx];
        }
      }();
    const int my_weights = [&]() {
        if (layer_type == LAYER_CONVOLUTION_ARRAY) {
          // weights are feature-major
          return feature_number * indices_per_node;
        } else {
          return node_idx * indices_per_node;
        }
      }();
    const int my_indices = [&]() {
        if (layer_type == LAYER_CONVOLUTION_ARRAY) {
          // same array of indices for each feature in the
          // occurrence
          const int occurrence_number = node_idx / num_features;
          return occurrence_number * indices_per_node;
        } else {
          return node_idx * indices_per_node;
        }
      }();

    // PERF could support dense layers more efficiently
    // PERF generally, use a different loop for each layer
    // type...
    for (int i = 0; i < indices_per_node; i++) {
      const float w = weights[my_weights + i];
      const int srci = indices[my_indices + i];
      const float v = src_values[srci];
      potential += w * v;
    }

    float out = Forward(potential);
    (*dst_values)[node_idx] = out;
  }
}


void Network::RunForwardVerbose(Stimulation *stim) const {
  for (int src = 0; src < num_layers; src++) {
    const TransferFunction transfer_function =
      layers[src].transfer_function;
    auto Forward =
      [transfer_function](float potential) -> float {
        switch (transfer_function) {
        case SIGMOID:
          return 1.0f / (1.0f + expf(-potential));
        case RELU:
          return (potential < 0.0f) ? 0.0f : potential;
        case LEAKY_RELU:
          return (potential < 0.0f) ? potential * 0.01f : potential;
        default:
          CHECK(false) << "Unimplemented transfer function " <<
            TransferFunctionName(transfer_function);
          return 0.0f;
        }
      };


    const vector<float> &src_values = stim->values[src];
    vector<float> *dst_values = &stim->values[src + 1];
    const vector<float> &biases = layers[src].biases;
    const vector<float> &weights = layers[src].weights;
    const vector<uint32> &indices = layers[src].indices;
    const int indices_per_node = layers[src].indices_per_node;
    const int number_of_nodes = num_nodes[src + 1];
    const int num_features = layers[src].num_features;
    const LayerType layer_type = layers[src].type;

    for (int node_idx = 0; node_idx < number_of_nodes; node_idx++) {
      // Output features are interleaved.
      const int feature_number = node_idx % num_features;

      // Start with bias.
      float potential = [&](){
          if (layer_type == LAYER_CONVOLUTION_ARRAY) {
            return biases[feature_number];
          } else {
            return biases[node_idx];
          }
        }();

      printf("%d|L %d n %d. bias: %f\n",
             rounds, src, node_idx, potential);
      CHECK(!std::isnan(potential)) << node_idx;

      const int my_weights = [&]() {
          if (layer_type == LAYER_CONVOLUTION_ARRAY) {
            // weights are feature-major
            return feature_number * indices_per_node;
          } else {
            return node_idx * indices_per_node;
          }
        }();
      const int my_indices = [&]() {
          if (layer_type == LAYER_CONVOLUTION_ARRAY) {
            // same array of indices for each feature in the
            // occurrence
            const int occurrence_number = node_idx / num_features;
            return occurrence_number * indices_per_node;
          } else {
            return node_idx * indices_per_node;
          }
        }();

      for (int i = 0; i < indices_per_node; i++) {
        const float w = weights[my_weights + i];
        const int srci = indices[my_indices + i];
        // XXX check dupes
        CHECK(srci >= 0 && srci < src_values.size()) << srci;
        const float v = src_values[srci];
        CHECK(!std::isnan(w) &&
              !std::isnan(v) &&
              !std::isnan(potential)) <<
          StringPrintf("L %d, n %d. [%d=%d] %f * %f + %f\n",
                       src,
                       node_idx,
                       i, srci, w, v, potential);
        potential += w * v;
      }
      CHECK(!std::isnan(potential));

      CHECK(node_idx >= 0 && node_idx < dst_values->size());
      float out = Forward(potential);
      printf("    %f -> %f\n", potential, out);
      CHECK(!std::isnan(out)) << potential;
      (*dst_values)[node_idx] = out;
    }
  }
}


void Network::NaNCheck(const std::string &message) const {
  bool has_nans = false;
  vector<std::pair<int, int>> layer_nans;
  for (const Layer &layer : layers) {
    int w = 0, b = 0;
    for (float f : layer.weights) if (std::isnan(f)) w++;
    for (float f : layer.biases) if (std::isnan(f)) b++;
    layer_nans.emplace_back(w, b);
    if (w > 0 || b > 0) has_nans = true;
  }
  if (has_nans) {
    string err;
    for (int i = 0; i < layer_nans.size(); i++) {
      err += StringPrintf("(real) layer %d. %d/%d weights, %d/%d biases\n",
                          i,
                          layer_nans[i].first, layers[i].weights.size(),
                          layer_nans[i].second, layers[i].biases.size());
    }
    CHECK(false) << "[" << message << "] The network has NaNs :-(\n" << err;
  }
}


void Network::StructuralCheck() const {
  // TODO: Other checks!
  CHECK(layers.size() == num_layers);
  CHECK(width.size() == num_layers + 1);
  CHECK(height.size() == num_layers + 1);
  CHECK(channels.size() == num_layers + 1);
  CHECK(renderstyle.size() == num_layers + 1);

  CHECK(inverted_indices.size() == num_layers);

  for (int i = 0; i < num_layers; i++) {
    const Layer &layer = layers[i];
    const int num_prev_nodes = num_nodes[i];
    const int num_this_nodes = num_nodes[i + 1];

    if (layer.type == LAYER_SPARSE || layer.type == LAYER_DENSE) {
      CHECK(layer.indices.size() == num_this_nodes * layer.indices_per_node);
    }

    // Check indices are in bounds. Unsigned ints so this is just
    // the upper-bound check.
    for (const uint32 idx : layer.indices) {
      CHECK(idx < num_prev_nodes);
    }

    // If dense, check that they are the expected regular structure.
    if (layer.type == LAYER_DENSE) {
      CHECK(layer.indices_per_node == num_prev_nodes);
      for (int n = 0; n < num_this_nodes; n++) {
        for (int p = 0; p < num_prev_nodes; p++) {
          CHECK(layer.indices[n * layer.indices_per_node + p] == p);
        }
      }
    }

    if (layer.type == LAYER_CONVOLUTION_ARRAY) {
      CHECK(layer.num_features > 0);
      CHECK(layer.pattern_width > 0);
      CHECK(layer.pattern_height > 0);
      CHECK(layer.src_width > 0);
      CHECK(layer.src_height > 0);
      CHECK(layer.occurrence_x_stride > 0);
      CHECK(layer.occurrence_y_stride > 0);

      CHECK(layer.num_occurrences_across > 0);
      CHECK(layer.num_occurrences_down > 0);

      CHECK(layer.biases.size() == layer.num_features);
      CHECK(layer.weights.size() == layer.num_features *
            layer.indices_per_node);
      CHECK(num_this_nodes % layer.num_features == 0);
      CHECK(layer.indices.size() ==
            (layer.indices_per_node * num_this_nodes) /
            layer.num_features);

      CHECK(layer.indices_per_node ==
            layer.pattern_width * layer.pattern_height);

      CHECK(layer.num_occurrences_across *
            layer.num_occurrences_down *
            layer.num_features == num_this_nodes);

      CHECK(layer.src_width * layer.src_height <= num_prev_nodes);

      // TODO: Check the indices are what we expect, perhaps by
      // just calling MakeConvolutionArrayIndices?
    }
  }

  CheckInvertedIndices();
}

Network::Layer Network::MakeDenseLayer(int num_nodes,
                                       int indices_per_node,
                                       TransferFunction transfer_function) {
  Layer layer;
  layer.indices_per_node = indices_per_node;
  layer.type = LAYER_DENSE;
  layer.transfer_function = transfer_function;
  layer.indices.resize(num_nodes * indices_per_node, 0);
  for (int n = 0; n < num_nodes; n++) {
    for (int p = 0; p < indices_per_node; p++) {
      layer.indices[n * indices_per_node + p] = p;
    }
  }

  layer.weights.resize(num_nodes * indices_per_node, 0.0f);
  layer.biases.resize(num_nodes, 0.0f);
  return layer;
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
    if (dst_layer_type == LAYER_CONVOLUTION_ARRAY) {
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
    if (dst_layer_type == LAYER_CONVOLUTION_ARRAY) {
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

// static
std::tuple<std::vector<uint32_t>, int, int, int>
Network::MakeConvolutionArrayIndices(
    int prev_num_nodes,
    int num_features,
    int pattern_width,
    int pattern_height,
    int src_width,
    int src_height,
    int occurrence_x_stride,
    int occurrence_y_stride) {
  // Check for nonsensical inputs.
  CHECK(prev_num_nodes >= 1);
  CHECK(num_features >= 1);
  CHECK(pattern_width >= 1);
  CHECK(pattern_height >= 1);
  CHECK(src_width >= 1);
  CHECK(src_height >= 1);
  CHECK(occurrence_x_stride >= 1);
  CHECK(occurrence_y_stride >= 1);

  // Normally, equal.
  CHECK(src_width * src_height <= prev_num_nodes);

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
          indices.push_back(src_offset);
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


// Caller owns new-ly allocated Network object.
Network *Network::ReadNetworkBinary(const string &filename) {
  printf("Reading [%s]\n", filename.c_str());
  FILE *file = fopen(filename.c_str(), "rb");
  if (file == nullptr) {
    printf("  ... failed. If it's present, there may be a "
           "permissions problem?\n");
    fflush(stdout);
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

  CHECK(Read32() == Network::FORMAT_ID) << "Wrong magic number!";

  int64 round = Read64();
  int64 examples = Read64();
  // These values determine the size of the network vectors.
  int file_num_layers = Read32();
  CHECK_GE(file_num_layers, 0);
  printf("%s: %lld rounds, %lld examples, %d layers.\n",
         filename.c_str(), round, examples, file_num_layers);
  vector<int> num_nodes(file_num_layers + 1, 0);
  printf("%s: num nodes: ", filename.c_str());
  for (int i = 0; i < file_num_layers + 1; i++) {
    num_nodes[i] = Read32();
    printf("%d ", num_nodes[i]);
  }
  printf("\n");

  vector<int> width, height, channels;
  vector<uint32_t> renderstyle;
  for (int i = 0; i < file_num_layers + 1; i++)
    width.push_back(Read32());
  for (int i = 0; i < file_num_layers + 1; i++)
    height.push_back(Read32());
  for (int i = 0; i < file_num_layers + 1; i++)
    channels.push_back(Read32());
  for (int i = 0; i < file_num_layers + 1; i++)
    renderstyle.push_back(Read32());

  CHECK(num_nodes.size() == width.size());
  CHECK(num_nodes.size() == height.size());
  CHECK(num_nodes.size() == channels.size());
  CHECK(num_nodes.size() == renderstyle.size());

  for (int w : width) CHECK(w > 0);
  for (int h : height) CHECK(h > 0);
  for (int c : channels) CHECK(c > 0);

  for (int i = 0; i < file_num_layers + 1; i++) {
    printf("Layer %d: %d x %d x %d (as %08x)\n",
           i - 1, width[i], height[i], channels[i], renderstyle[i]);
  }

  printf("\n%s: indices per node/fns/type: ", filename.c_str());

  vector<Layer> layers(file_num_layers);
  for (int i = 0; i < file_num_layers; i++) {
    Layer &layer = layers[i];
    const int nodes_this_layer = num_nodes[i + 1];
    const int ipn_this_layer = Read32();
    layer.indices_per_node = ipn_this_layer;

    LayerType lt = (LayerType)Read32();
    CHECK(lt >= 0 && lt < NUM_LAYER_TYPES) << lt;
    layer.type = lt;

    TransferFunction tf = (TransferFunction)Read32();
    CHECK(tf >= 0 && tf < NUM_TRANSFER_FUNCTIONS) << tf;
    layer.transfer_function = tf;

    const int num_features = Read32();
    layer.num_features = num_features;

    layer.pattern_width = Read32();
    layer.pattern_height = Read32();
    layer.src_width = Read32();
    layer.src_height = Read32();
    layer.occurrence_x_stride = Read32();
    layer.occurrence_y_stride = Read32();
    layer.num_occurrences_across = Read32();
    layer.num_occurrences_down = Read32();

    printf("%d %s %s ",
           ipn_this_layer,
           TransferFunctionName(tf),
           LayerTypeName(lt));
    if (lt == LAYER_CONVOLUTION_ARRAY) {
      printf("(%d feat%s, %dx%d pat from %dx%d rect, +%d +%d) ",
             num_features,
             (num_features == 1) ? "" : "s",
             layer.pattern_width, layer.pattern_height,
             layer.src_width, layer.src_height,
             layer.occurrence_x_stride, layer.occurrence_y_stride);
    }

    // Correctly size the data chunks to be read below.

    switch (lt) {
    case LAYER_CONVOLUTION_ARRAY:
      // Indices are explicit but shared across all features:
      CHECK((nodes_this_layer * ipn_this_layer) % num_features == 0);
      layer.indices.resize((nodes_this_layer * ipn_this_layer) /
                           num_features, 0);
      // Shared weights.
      layer.weights.resize(ipn_this_layer * num_features, 0.0f);
      layer.biases.resize(num_features, 0.0f);
      break;
    case LAYER_SPARSE:
    case LAYER_DENSE:
      // Normal case: All layers with explicit indices.
      layer.indices.resize(ipn_this_layer * nodes_this_layer, 0);
      // Normal case: Individual weights/biases per input index.
      layer.weights.resize(ipn_this_layer * nodes_this_layer, 0.0f);
      layer.biases.resize(nodes_this_layer, 0.0f);
      break;
    default:
      CHECK(false) << "Unimplemented layer type " << lt;
    }
  }
  printf("\n");

  int64 large_weights = 0, large_biases = 0;

  // Read Layer structs.
  CHECK(layers.size() == file_num_layers);
  for (int i = 0; i < file_num_layers; i++) {
    Layer &layer = layers[i];
    switch (layer.type) {
    case LAYER_SPARSE:
      for (int j = 0; j < layer.indices.size(); j++) {
        layer.indices[j] = Read32();
      }
      break;
    case LAYER_CONVOLUTION_ARRAY: {
      // Indices are not stored for convolution layers, since they
      // can be computed.
      const int prev_num_nodes = num_nodes[i];
      const int this_num_nodes = num_nodes[i + 1];

      const auto [indices, this_num_nodes_computed,
                  num_occurrences_across, num_occurrences_down] =
        MakeConvolutionArrayIndices(
            prev_num_nodes,
            layer.num_features,
            layer.pattern_width,
            layer.pattern_height,
            layer.src_width,
            layer.src_height,
            layer.occurrence_x_stride,
            layer.occurrence_y_stride);

      CHECK(this_num_nodes_computed == this_num_nodes);

      CHECK(num_occurrences_across == layer.num_occurrences_across &&
            num_occurrences_down == layer.num_occurrences_down) <<
        "Wrong occurrences down/across; stored in file: " <<
        layer.num_occurrences_across << " x " <<
        layer.num_occurrences_down << "; computed: " <<
        num_occurrences_across << " x " << num_occurrences_down;
      CHECK(layer.indices.size() == indices.size());
      layer.indices = indices;
      break;
    }
    case LAYER_DENSE: {
      // Indices are not actually stored for dense layers, since they
      // can be computed.
      // (layer 0 is the input layer)
      const int prev_num_nodes = num_nodes[i];
      const int this_num_nodes = num_nodes[i + 1];
      CHECK_EQ(layer.indices.size(), prev_num_nodes * this_num_nodes) <<
        "For a dense layer, indices per node should be the size of "
        "the previous layer! prev * cur: " << prev_num_nodes <<
        " * " << this_num_nodes << " = " << prev_num_nodes * this_num_nodes <<
        " but got " << layer.indices.size();
      int64 offset = 0;
      for (int n = 0; n < this_num_nodes; n++) {
        for (int p = 0; p < prev_num_nodes; p++) {
          layer.indices[offset] = p;
          offset++;
        }
      }
      break;
    }
    default:
      CHECK(false) << "Unsupported layer type " << layer.type;
      break;
    }
    ReadFloats(&layer.weights);
    ReadFloats(&layer.biases);

    static constexpr float LARGE_WEIGHT = 8.0f;
    static constexpr float LARGE_BIAS = 128.0f;
    for (float f : layer.weights) {
      if (f > LARGE_WEIGHT || f < -LARGE_WEIGHT) {
        large_weights++;
      }
    }
    for (float f : layer.biases) {
      if (f > LARGE_BIAS || f < -LARGE_BIAS) {
        large_biases++;
      }
    }
  }

  if (large_weights > 0 || large_biases > 0) {
    printf("Warning: %lld large weights and %lld large biases\n",
           large_weights, large_biases);
  }

  fclose(file);
  printf("Read from %s.\n", filename.c_str());

  // Construct network.

  auto net = std::make_unique<Network>(num_nodes, layers);
  CHECK(net.get() != nullptr);
  net->width = width;
  net->height = height;
  net->channels = channels;
  net->renderstyle = renderstyle;

  net->rounds = round;
  net->examples = examples;

  // Now, fill in the inverted indices. These are not stored in the file.

  printf("Invert index:\n");
  net->ComputeInvertedIndices();
  printf("Check it:\n");
  net->StructuralCheck();

  return net.release();
}

void Network::SaveNetworkBinary(const string &filename) {
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

  Write32(Network::FORMAT_ID);
  Write64(rounds);
  Write64(examples);
  Write32(num_layers);
  CHECK(num_nodes.size() == num_layers + 1);
  CHECK(width.size() == num_layers + 1) << width.size();
  CHECK(height.size() == num_layers + 1);
  CHECK(channels.size() == num_layers + 1);
  CHECK(renderstyle.size() == num_layers + 1);

  for (const int i : num_nodes) Write32(i);
  for (const int w : width) Write32(w);
  for (const int h : height) Write32(h);
  for (const int c : channels) Write32(c);
  for (const uint32 s : renderstyle) Write32(s);

  for (const Network::Layer &layer : layers) {
    Write32(layer.indices_per_node);
    Write32(layer.type);
    Write32(layer.transfer_function);
    // Always write convolution stuff, even if not a
    // convolution type layer.
    Write32(layer.num_features);
    Write32(layer.pattern_width);
    Write32(layer.pattern_height);
    Write32(layer.src_width);
    Write32(layer.src_height);
    Write32(layer.occurrence_x_stride);
    Write32(layer.occurrence_y_stride);
    Write32(layer.num_occurrences_across);
    Write32(layer.num_occurrences_down);
  }

  for (const Network::Layer &layer : layers) {
    switch (layer.type) {
    case LAYER_CONVOLUTION_ARRAY:
      // Don't write convolution layers; the structure is computable.
      break;
    case LAYER_SPARSE:
      for (const uint32 idx : layer.indices) Write32(idx);
      break;
    case LAYER_DENSE:
      // Don't write dense layers; the structure is computable.
      break;
    default:
      CHECK(false) << "Unknown layer type!";
    }
    WriteFloats(layer.weights);
    WriteFloats(layer.biases);
  }

  // Inverted indices are not written.
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
