
// CPU version of network.
// Idea is to keep this pretty general and eventually promote it to
// cc-lib, but for now I've been cloning and making improvements
// with each ML project.

#ifndef _PLUGINVERT_NETWORK_H
#define _PLUGINVERT_NETWORK_H

#include <vector>
#include <string>
#include <cstdint>

#include "base/logging.h"

enum TransferFunction {
  SIGMOID = 0,
  RELU = 1,
  LEAKY_RELU = 2,
  IDENTITY = 3,

  NUM_TRANSFER_FUNCTIONS,
};

enum LayerType {
  // Every node takes input from every node in the previous layer.
  // (indices_per_node = size of the previous layer). This is the most
  // expressive setup, but also the largest. Training and prediction
  // can be more efficient (per weight) because of the regular
  // structure. However, this is not really a practical option for
  // large layers.
  LAYER_DENSE = 0,
  // Explicitly specify the input nodes. Every node has the same
  // number of inputs. Some overhead to store these indices.
  LAYER_SPARSE = 1,
  // An array of convolutions. Each of the num_convolution
  // convolutions is a node pattern that is repeated over and over,
  // with the same node weights. The indices of each occurrence of the
  // pattern are given explicitly just as in SPARSE. (They usually
  // follow a regular pattern, like some area around the related
  // "pixel" in the source layer, but this is not enforced.) This is a
  // good option when the input layer represents some array of pixels
  // or samples of like kind. A single convolution makes sense, but we
  // allow for an array so that the common case of several different
  // features (with the same indices but different weights) can be
  // treated more efficiently. The features are interleaved in the
  // output.
  LAYER_CONVOLUTION_ARRAY = 2,

  NUM_LAYER_TYPES,
};

// How to draw a layer's stimulations in UIs. Has no effect in network
// code itself. (If we standardize the rendering code, this enum should
// go with that.)
enum RenderStyle : uint32_t {
  // One pixel per channel.
  RENDERSTYLE_FLAT = 0,
  // Assign channels as RGB. Makes most sense when there are three
  // channels, but works fine for one (same as flat) or two
  // (red/green) as well.
  RENDERSTYLE_RGB = 1,
  // Use channels/3 RGB pixels.
  RENDERSTYLE_MULTIRGB = 2,

  // Rest of the range is reserved for users.
  RENDERSTYLE_USER = 0xF0000000,
};

const char *TransferFunctionName(TransferFunction tf);
const char *LayerTypeName(LayerType lt);

struct Stimulation;
struct Errors;

struct Network {
  template<class T> using vector = std::vector<T>;
  using string = std::string;

  struct Layer;
  // Create network from the given num_nodes and layers fields (see
  // documentation below). Computes inverted indices and does
  // structural checks, aborting if something is amiss. So this should
  // be created with valid layers.
  Network(vector<int> num_nodes, vector<Layer> layers);

  // Constants containing implementations of the different transfer
  // functions. These are provided for the sake of layer-specific
  // generated code, like the OpenCL. Each #defines FORWARD(p) and
  // DERIVATIVE(fx) as C/C++/OpenCL. FORWARD is as you'd expect.
  // DERIVATIVE is given in terms of the *output* of the transfer
  // function, because this is the most natural/efficient for the
  // sigmoid, and can be done (a bit less naturally) for ReLU.
  static const char *const SIGMOID_FN;
  static const char *const RELU_FN;
  static const char *const LEAKY_RELU_FN;
  static const char *const IDENTITY_FN;

  // Return one of the above constants (or abort for an unknown
  // transfer function).
  static string TransferFunctionDefines(TransferFunction tf);

  // Size of network in RAM. Note that this includes the indices
  // and inverted indices for dense layers (which are indeed still stored)
  // even though they are not used or represented on disk.
  int64_t Bytes() const;
  // Return the total number of parameters in the model (weights and biases
  // across all layers; the indices are not "parameters" in this sense).
  int64_t TotalParameters() const;

  void CopyFrom(const Network &other) {
    CHECK_EQ(this->num_layers, other.num_layers);
    this->num_nodes = other.num_nodes;
    this->width = other.width;
    this->height = other.height;
    this->channels = other.channels;
    this->renderstyle = other.renderstyle;
    this->layers = other.layers;
    this->inverted_indices = other.inverted_indices;
    this->rounds = other.rounds;
    this->examples = other.examples;
  }

  // Check for NaN weights and abort if any are found.
  void NaNCheck(const string &message) const;

  // Check for structural well-formedness (layers are the right size;
  // indices are in bounds; dense layers have the expected regular
  // structure; inverted indices are correct). Aborts if something is
  // wrong. Doesn't check weight values (see NaNCheck).
  void StructuralCheck() const;

  static Network *Clone(const Network &other);

  // Note: These use local byte order, so the serialized format is not
  // portable.
  static Network *ReadNetworkBinary(const string &filename);
  void SaveNetworkBinary(const string &filename);

  // If the number of nodes or indices per node change, this can be
  // used to reallocate the inverted index buffers; then you must call
  // ComputeInvertedIndices to put the network in a valid state.
  void ReallocateInvertedIndices();
  // Compute the inverted indices after any change to the indices.
  void ComputeInvertedIndices(int max_parallelism = 8);

  // Run the network to fill out the stimulation. The Stimulation
  // must be the right size (i.e. created from this Network) and
  // the input layer should be filled.
  void RunForward(Stimulation *stim) const;
  // Same, but only one layer. src_layer is the input layer.
  void RunForwardLayer(Stimulation *stim, int src_layer) const;
  // Same, but print lots of garbage and abort if a NaN is encountered
  // at any point.
  void RunForwardVerbose(Stimulation *stim) const;

  // Just used for serialization. Whenever changing the interpretation
  // of the data in an incomplete way, please change.
  static constexpr uint32_t FORMAT_ID = 0x27000733U;

  // The number of "real" layers, that is, not counting the input.
  const int num_layers;

  // num_layers + 1. num_nodes[0] is the size of the input layer.
  vector<int> num_nodes;
  // Parallel to num_nodes. These don't affect the network's behavior,
  // just its rendering. num_nodes[i] == width[i] * height[i] * channels[i].
  // (XXX for chunks, this would be a property of the chunk.)
  vector<int> width, height, channels;
  // Same, but a hint to the UI about how to render. Normal for this
  // to contain values outside the enum (i.e. in USER_RENDERSTYLE range).
  // (XXX should be a property of the chunk too)
  vector<uint32_t> renderstyle;

  // (XXX: Could just have LAYER_INPUT and even LAYER_OUTPUT, which
  // then have like empty input/output? It could perhaps simplify
  // thinking about these things.)
  // "Real" layer; none for the input.
  struct Layer {
    // Same number of input indices for each node.
    // For dense layers, this must be the size of the previous layer.
    int indices_per_node = 0;

    // Sparse, dense, or convolutional layer?
    LayerType type = LAYER_SPARSE;

    // The transfer function used to compute the output from the
    // weighted inputs.
    TransferFunction transfer_function = LEAKY_RELU;

    // For CONVOLUTION_ARRAY layers:
    //
    // Gives the number of features in the array. Each has the same
    // indices but a different set of weights/biases. Must divide the
    // number of nodes on the layer.
    int num_features = 1;
    // pattern_width * pattern_height = indices_per_node
    int pattern_width = 1;
    int pattern_height = 1;
    // How we perceive the previous layer as a rectangle; not necessarily
    // the same as the width array (typically, width * channels).
    // src_width * src_height <= prev_num_nodes (usually equal)
    int src_width = 1;
    int src_height = 1;
    // When advancing the pattern, how far to move in the x and y
    // directions? Must be positive.
    int occurrence_x_stride = 1;
    int occurrence_y_stride = 1;
    // Number of occurrences of the pattern that fit in each direction,
    // taking into account everything above.
    // num_occurrences_across * num_occurrences_down * num_features =
    // num_nodes
    int num_occurrences_across = 1;
    int num_occurrences_down = 1;

    // indices_per_node * num_nodes[l + 1], flat, node-major
    // If type = LAYER_DENSE, then for n < num_nodes[l + 1],
    // indices[n * indices_per_node + i] = i. (TODO: We should
    // perhaps save the ram by not storing indices for dense
    // layers.)
    // If type = LAYER_CONVOLUTION_ARRY, the instances are shared
    // for all features, so this has size
    // indices_per_node * num_nodes[l + 1] / num_features.
    // the nesting is
    //   occurrence_row
    //     occurrence_col
    //       pattern_row
    //         pattern_col
    // (with the loop over feature indices implicit at the innermost
    // level)
    //
    // (XXX For chunks, this would I guess be an index into the
    // whole layer, not any specific chunk.)
    vector<uint32_t> indices;

    // For SPARSE and DENSE layers, this is parallel to indices:
    // indices_per_node * num_nodes[l + 1]
    // For CONVOLUTIONAL layers, we have a set of weights for each
    // feature (shared for each occurrence of the feature). So
    // this is size num_features * indices_per_node. The weights
    // are "feature major": The weights for all the indices of a
    // given feature are adjacent in the array.
    vector<float> weights;
    // For SPARSE and DENSE layers, there is one per node:
    // num_nodes[l + 1].
    // For CONVOLUTIONAL layers, size num_features.
    vector<float> biases;
  };

  // Create a dense layer with the expected regular structure of indices
  // and zero weights (you gotta initialize these).
  static Layer MakeDenseLayer(int num_nodes,
                              // ipn is = size of the previous layer
                              int indices_per_node,
                              TransferFunction transfer_function);

  // Maybe this should return a Layer?
  // returns indices, this_num_nodes,
  // num_occurrences_across, num_occurrences_down
  static std::tuple<std::vector<uint32_t>, int, int, int>
  MakeConvolutionArrayIndices(int prev_num_nodes,
                              int num_features,
                              int pattern_width,
                              int pattern_height,
                              int src_width,
                              int src_height,
                              int occurrence_x_stride,
                              int occurrence_y_stride);

  // XXX Probably needs to be rethought for 'chunks'? Perhaps this is
  // a concept of the full layer, after assigning each parallel
  // chunk distinct node ids.
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

  // num_layers
  vector<Layer> layers;
  // There are also num_layers of these, but be careful about the
  // offset. The 0th inverted index is about the gap between the input
  // layer (otherwise not represented in the network, except for its
  // size in num_nodes[0]) and the first hidden layer. The last one is
  // about the last gap, not the output layer, since the output layer
  // is not indexed by anything.
  vector<InvertedIndices> inverted_indices;

  // Rounds trained. This matters when restarting from disk, because
  // for example the learning rate depends on the round.
  int64_t rounds = 0;
  // Total number of training examples processed.
  int64_t examples = 0;

private:
  // Value type, but require calling Clone explicitly.
  Network(const Network &other) = default;
  // Check the inverted indices specifically. Use StructuralCheck
  // instead.
  void CheckInvertedIndices() const;
};

// A stimulation is an evaluation (perhaps an in-progress one) of a
// network on a particular input; when it's complete we have the
// activation value of each node on each layer, plus the input itself.
struct Stimulation {
  explicit Stimulation(const Network &net) : num_layers(net.num_layers),
                                             num_nodes(net.num_nodes) {
    values.resize(num_layers + 1);
    for (int i = 0; i < values.size(); i++)
      values[i].resize(num_nodes[i], 0.0f);
  }

  // Empty, useless stimulation, but can be used to initialize
  // vectors, etc.
  Stimulation() : num_layers(0) {}
  Stimulation(const Stimulation &other) = default;

  int64_t Bytes() const;

  // TODO: would be nice for these to be const, but then we can't have an
  // assignment operator.
  // Same as in Network.
  int num_layers;
  // num_layers + 1
  std::vector<int> num_nodes;

  // Keep track of what's actually been computed?

  // Here the outer vector has size num_layers + 1; first is the input.
  // Inner vector has size num_nodes[i], and just contains their output values.
  std::vector<std::vector<float>> values;

  void CopyFrom(const Stimulation &other) {
    CHECK_EQ(this->num_layers, other.num_layers);
    CHECK_EQ(this->num_nodes.size(), other.num_nodes.size());
    for (int i = 0; i < this->num_nodes.size(); i++) {
      CHECK_EQ(this->num_nodes[i], other.num_nodes[i]);
    }
    this->values = other.values;
  }

  void NaNCheck(const std::string &message) const;
};


struct Errors {
  explicit Errors(const Network &net) : num_layers(net.num_layers),
                                        num_nodes(net.num_nodes) {
    error.resize(num_layers);
    for (int i = 0; i < error.size(); i++) {
      error[i].resize(num_nodes[i + 1], 0.0f);
    }
  }
  // Empty, useless errors, but can be used to initialize vectors etc.
  Errors() : num_layers(0) {}
  Errors(const Errors &other) = default;

  // Would be nice for these to be const, but then we can't have an
  // assignment operator.
  int num_layers;
  // The first entry here is unused (it's the size of the input layer,
  // which doesn't get errors), but we keep it like this to be
  // consistent with Network and Stimulation.
  std::vector<int> num_nodes;

  // These are the delta terms in Mitchell. We have num_layers of
  // them, where the error[0] is the first real layer (we don't
  // compute errors for the input) and error[num_layers] is the error
  // for the output.
  std::vector<std::vector<float>> error;

  int64_t Bytes() const;

  void CopyFrom(const Errors &other) {
    CHECK_EQ(this->num_layers, other.num_layers);
    CHECK_EQ(this->num_nodes.size(), other.num_nodes.size());
    for (int i = 0; i < this->num_nodes.size(); i++) {
      CHECK_EQ(this->num_nodes[i], other.num_nodes[i]);
    }
    this->error = other.error;
  }

};


#endif
