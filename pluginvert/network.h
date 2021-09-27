
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

// A layer consists of one or more chunks. Each takes as input some
// given span of output nodes from the previous layer (perhaps the
// whole layer), and processes them according to one of these types.
// The layer's output is just the concatenation of each chunk's
// output. A typical use of multiple chunks would be to have some
// convolutional portion (propagating local information) as well as
// some sparse or dense portion (propagating global information).
enum ChunkType {
  // The input layer is special but we represent it as the first
  // element in the layer array to avoid off-by-one hell. It always
  // consists of a single chunk with this type.
  CHUNK_INPUT = 0,

  // Every node takes input from every node in the span.
  // (indices_per_node = size of the span). This is the most
  // expressive setup, but also the largest. Training and prediction
  // can be more efficient (per weight) because of the regular
  // structure. However, this is not really a practical option for
  // large spans.
  CHUNK_DENSE = 1,
  // Explicitly specify the input nodes. Every node has the same
  // number of inputs. Some overhead to store these indices, and
  // data-dependent loads during inference/training.
  CHUNK_SPARSE = 2,
  // An array of convolutions. Each of the num_convolution
  // convolutions is a node pattern that is repeated over and over,
  // with the same node weights. The indices of each occurrence of the
  // pattern are given explicitly just as in SPARSE. (They usually
  // follow a regular pattern, like some area around the related
  // "pixel" in the source layer, but this is not enforced.) This is a
  // good option when the input span represents some 2D or 1D array of
  // pixels or samples of like kind. A single convolution makes sense,
  // but we allow for an array so that the common case of several
  // different features (with the same indices but different weights)
  // can be treated more efficiently. The features are interleaved in
  // the output.
  CHUNK_CONVOLUTION_ARRAY = 3,

  NUM_CHUNK_TYPES,
};

// Transfer function for a chunk.
enum TransferFunction {
  SIGMOID = 0,
  RELU = 1,
  LEAKY_RELU = 2,
  IDENTITY = 3,

  NUM_TRANSFER_FUNCTIONS,
};

// How to draw a chunk's stimulations in UIs. Has no effect in network
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
const char *ChunkTypeName(ChunkType ct);

struct Stimulation;
struct Errors;



struct Chunk {
  ChunkType type = CHUNK_SPARSE;

  // Indices are relative to this span of the previous layer:
  //    [span_start, span_start + span_size)
  // For INPUT chunks, ignored.
  int span_start = 0;
  int span_size = 0;

  // Number of (output) nodes in this chunk.
  int num_nodes = 0;

  // Same number of input indices for each node.
  // For dense chunks, this must be equal to span_size.
  // For INPUT chunks, ignored.
  int indices_per_node = 0;

  // The transfer function used to compute the output from the
  // weighted inputs.
  // For INPUT chunks, ignored.
  TransferFunction transfer_function = LEAKY_RELU;

  // For CONVOLUTION_ARRAY chunks:
  //
  // Gives the number of features in the array. Each has the same
  // indices but a different set of weights/biases. Must divide the
  // number of nodes on the layer.
  int num_features = 1;
  // pattern_width * pattern_height = indices_per_node
  int pattern_width = 1;
  int pattern_height = 1;
  // How we perceive the input span as a rectangle; not necessarily
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

  // Indices into the span. These must be in
  //   [span_start, span_start + span_size)
  // (that is, they already have the span's offset added).
  //
  // indices_per_node * num_nodes, flat, node-major
  // For DENSE chunks, with n < num_nodes
  // indices[n * indices_per_node + i] = span_start + i.
  // (TODO: We should perhaps save the ram by not even storing
  // indices for dense chunks.)
  // For CONVOLUTION_ARRY chunks, the instances are shared
  // for all features, so this has size
  // indices_per_node * num_nodes / num_features.
  // the nesting is
  //   occurrence_row
  //     occurrence_col
  //       pattern_row
  //         pattern_col
  // (with the loop over feature indices implicit at the innermost
  // level)
  // For INPUT chunks, ignored.
  std::vector<uint32_t> indices;

  // For SPARSE and DENSE chunks, this is parallel to indices:
  // indices_per_node * num_nodes
  // For CONVOLUTIONAL chunk, we have a set of weights for each
  // feature (shared for each occurrence of the feature). So
  // this is size num_features * indices_per_node. The weights
  // are "feature major": The weights for all the indices of a
  // given feature are adjacent in the array.
  // For INPUT, ignored.
  std::vector<float> weights;
  // For SPARSE and DENSE chunks, one per node, so size num_nodes.
  // For CONVOLUTIONAL chunks, size num_features.
  // For INPUT chunks, ignored.
  std::vector<float> biases;

  // These are presentational (e.g. used when rendering in the
  // training view), but width * height * channels must equal num_nodes.
  int width = 0;
  int height = 0;
  int channels = 1;

  RenderStyle style = RENDERSTYLE_FLAT;
};

struct Layer {
  // Number of nodes in this layer's output.
  // Must be the sum of the num_nodes for each chunk, since it is
  // their concatenation that forms the layer.
  int num_nodes;

  // For the 0th layer, this must be exactly one chunk of type
  // CHUNK_INPUT.
  std::vector<Chunk> chunks;
};

static constexpr inline
uint32_t FOURCC(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  return (a << 24) | (b << 16) | (c << 8) | d;
}

struct Network {
  template<class T> using vector = std::vector<T>;
  using string = std::string;

  // Create network from the given layers (see documentation below).
  // Computes inverted indices and does structural checks, aborting if
  // something is amiss. So this should be created with valid layers.
  explicit Network(vector<Layer> layers);

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
  // and inverted indices for dense chunks (which are indeed still stored)
  // even though they are not used or represented on disk.
  int64_t Bytes() const;
  // Return the total number of parameters in the model (weights and biases
  // across all layers; the indices are not "parameters" in this sense).
  int64_t TotalParameters() const;

  void CopyFrom(const Network &other) {
    CHECK_EQ(this->layers.size(), other.layers.size());
    this->layers = other.layers;
    this->rounds = other.rounds;
    this->examples = other.examples;
  }

  // Check for NaN weights and abort if any are found.
  void NaNCheck(const string &message) const;

  // Check for structural well-formedness (there's an input layer;
  // layers are the right size; indices are in bounds; dense layers
  // have the expected regular structure). Aborts if something is
  // wrong. Doesn't check weight values (see NaNCheck).
  void StructuralCheck() const;

  static Network *Clone(const Network &other);

  // Note: These use local byte order, so the serialized format is not
  // portable.
  // Caller owns new-ly allocated Network object.
  static Network *ReadNetworkBinary(const string &filename,
                                    bool verbose = true);
  void SaveNetworkBinary(const string &filename);

  // TODO: ComputeInvertedIndices(int layer_idx, int chunk_idx)
  // In this new version we don't store the inverted indices, since
  // they are only used in training. Instead we generate them on
  // the fly when we construct the NetworkGPU object.

  // Run the network to fill out the stimulation. The Stimulation
  // must be the right size (i.e. created from this Network) and
  // the input layer should be filled.
  void RunForward(Stimulation *stim) const;
  // Same, but only one layer. We read from stim.values[src_layer] and
  // write to stim.values[src_layer + 1].
  void RunForwardLayer(Stimulation *stim, int src_layer) const;


  // Serialization header. Always starts with MAGIC.
  static constexpr uint32_t MAGIC = FOURCC('T', '7', 'n', 'w');
  // ... and followed by this version identifier. When changing the
  // format in an incompatible way, always increment this.
  static constexpr uint32_t FORMAT_ID = 0x27000770U;

  // layer[0] is the input layer.
  vector<Layer> layers;

  // The number of layers, including the input layer.
  // XXX in the old version, num_layers was the number of "real" layers;
  // one less than this.
  int NumLayers() const {
    return layers.size();
  }

  // Create a dense layer with zero weights (you gotta initialize these).
  static Chunk MakeDenseChunk(int num_nodes,
                              // region of previous layer to depend on
                              int span_start, int span_size,
                              TransferFunction transfer_function);

  // Maybe this should return a Chunk?
  // returns indices, this_num_nodes,
  // num_occurrences_across, num_occurrences_down
  static std::tuple<std::vector<uint32_t>, int, int, int>
  MakeConvolutionArrayIndices(int span_start, int span_size,
                              int num_features,
                              int pattern_width,
                              int pattern_height,
                              int src_width,
                              int src_height,
                              int occurrence_x_stride,
                              int occurrence_y_stride);

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
  explicit Stimulation(const Network &net) {
    const int num_layers = net.layers.size();
    values.resize(num_layers);
    num_nodes.resize(num_layers);
    for (int i = 0; i < num_layers; i++) {
      const Layer &layer = net.layers[i];
      num_nodes.push_back(layer.num_nodes);
      values[i].resize(layer.num_nodes, 0.0f);
    }
  }

  // Empty, useless stimulation, but can be used to initialize
  // vectors, etc.
  Stimulation() {}
  Stimulation(const Stimulation &other) = default;

  int64_t Bytes() const;

  // TODO: would be nice for this to be const, but then we can't have an
  // assignment operator.
  // Size of each layer in the layers vector in corresponding network;
  // includes input.
  // (XXX Note this is just the size of the inner vectors in the values vector;
  // probably we can just get rid of it, or make it a method?)
  std::vector<int> num_nodes;

  // Keep track of what's actually been computed?

  // Here the outer vector has size num_layers; first is the input.
  // Inner vector has size num_nodes[i], and just contains their output values.
  std::vector<std::vector<float>> values;

  void CopyFrom(const Stimulation &other) {
    CHECK_EQ(this->values.size(), other.values.size());
    CHECK_EQ(this->num_nodes.size(), other.num_nodes.size());
    for (int i = 0; i < this->num_nodes.size(); i++) {
      CHECK_EQ(this->num_nodes[i], other.num_nodes[i]);
    }
    this->values = other.values;
  }

  void NaNCheck(const std::string &message) const;
};


struct Errors {
  explicit Errors(const Network &net) {
    const int num_layers = net.layers.size();
    error.resize(num_layers);
    num_nodes.resize(num_layers);
    for (int i = 0; i < num_layers; i++) {
      const Layer &layer = net.layers[i];
      num_nodes.push_back(layer.num_nodes);
      // Note that we don't reserve space in the first layer's
      // error, as this is the input layer and it doesn't receive
      // inputs.
      if (i > 0) {
        error[i].resize(layer.num_nodes, 0.0f);
      }
    }
  }
  // Empty, useless errors, but can be used to initialize vectors etc.
  Errors() {}
  Errors(const Errors &other) = default;

  // The first entry here is unused (it's the size of the input layer,
  // which doesn't get errors), but we keep it like this to be
  // consistent with Network and Stimulation.
  // Note that num_nodes[0] is NOT error[0].size(), because we
  // don't even bother allocating error for the input layer.
  std::vector<int> num_nodes;

  // These are the delta terms in Mitchell. We have num_layers of
  // them, where the error[0] is the input (empty; we don't compute
  // errors for the input; error[1] is the first real layer, and
  // and error[num_layers - 1] is the error for the output.
  std::vector<std::vector<float>> error;

  int64_t Bytes() const;

  void CopyFrom(const Errors &other) {
    CHECK_EQ(this->error.size(), other.error.size());
    CHECK_EQ(this->num_nodes.size(), other.num_nodes.size());
    for (int i = 0; i < this->num_nodes.size(); i++) {
      CHECK_EQ(this->num_nodes[i], other.num_nodes[i]);
      CHECK_EQ(this->error[i].size(), other.error[i].size());
    }
    this->error = other.error;
  }

};


#endif
