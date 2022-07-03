
// CPU version of network.
// Idea is to keep this pretty general and eventually promote it to
// cc-lib, but for now I've been cloning and making improvements
// with each ML project.

#ifndef _NETWORK_H
#define _NETWORK_H

#include <vector>
#include <string>
#include <cstdint>

#include "base/logging.h"

// Maybe stuff that uses this should be in network-util or something.
struct ArcFour;

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

  // PERF: Perhaps should support native 1:1 COPY and/or PERMUTATION
  // chunks, which imply fixed?

  NUM_CHUNK_TYPES,
};

// Transfer function for a chunk.
enum TransferFunction {
  // Classic. Output range is (0,1).
  SIGMOID = 0,
  RELU = 1,
  LEAKY_RELU = 2,
  IDENTITY = 3,
  // Output range is (-1, 1).
  TANH = 4,

  GRAD1 = 5,

  NUM_TRANSFER_FUNCTIONS,
};

// How to perform weight updates; these only affect training.
enum WeightUpdate {
  // Step along the gradient according to the learning rate.
  // No auxiliary data.
  SGD = 0,
  // Update is based on per-weight estimates of the first and second
  // moments of the gradient. Requires storing these two float
  // parameters along side weights, tripling the storage requirements
  // during training.
  ADAM = 1,
  // Variant of Adam that uses an additive moving average instead of
  // an exponential one. This changes the learning rate less abruptly,
  // which can help with divergence. Same auxiliary data as ADAM
  // with the same meaning, so it is possible to switch between these
  // during training.
  YOGI = 2,

  NUM_WEIGHT_UPDATES,
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
const char *WeightUpdateName(WeightUpdate wu);

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

  // Approach used for weight updates during training; affects
  // what is stored in the _aux fields.
  WeightUpdate weight_update = SGD;

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
  // For SPARSE chunks,
  // indices_per_node * num_nodes, flat, node-major
  //
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
  // For DENSE chunks, empty.
  // For INPUT chunks, ignored.
  //
  // These can be in any order, but it is significantly faster if
  // they are sorted from low to high.
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

  // For weight_update = SGD, empty.
  // For ADAM or YOGI twice the size of weights or biases vectors.
  // Interleaved m and v parameters for memory locality.
  std::vector<float> weights_aux;
  std::vector<float> biases_aux;

  // These are presentational (e.g. used when rendering in the
  // training view), but width * height * channels must equal num_nodes.
  int width = 0;
  int height = 0;
  int channels = 1;

  RenderStyle style = RENDERSTYLE_FLAT;

  // If true, the chunk's weights and biases should not be updated
  // during training. This can be useful for custom architectures
  // where we want to preserve some internal structure, especially
  // for copies and permutations.
  bool fixed = false;
};

struct Layer {
  // Number of nodes in this layer's output.
  // Must be the sum of the num_nodes for each chunk, since it is
  // their concatenation that forms the layer.
  int num_nodes = 0;

  // For the 0th layer, this must be exactly one chunk of type
  // CHUNK_INPUT.
  std::vector<Chunk> chunks;
};

static constexpr inline
uint32_t MakeFOURCC(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  return (a << 24) | (b << 16) | (c << 8) | d;
}

struct InvertedIndices;

struct Network {
  template<class T> using vector = std::vector<T>;
  using string = std::string;

  // Create network from the given layers (see documentation below).
  // Does structural checks, aborting if something is amiss. So this
  // should be created with valid layers.
  explicit Network(vector<Layer> layers);

  // Constants containing implementations of the different transfer
  // functions. These are provided for the sake of layer-specific
  // generated code, like the OpenCL. Each #defines FORWARD(p) and
  // DERIVATIVE(fx) as C/C++/OpenCL. FORWARD is as you'd expect.
  // DERIVATIVE is given in terms of the *output* of the transfer
  // function, because this is the most natural/efficient for
  // sigmoid and tanh, and can be done (a bit less naturally) for ReLU.
  static const char *const SIGMOID_FN;
  static const char *const RELU_FN;
  static const char *const LEAKY_RELU_FN;
  static const char *const IDENTITY_FN;
  static const char *const TANH_FN;
  static const char *const GRAD1_FN;

  // Return one of the above constants (or abort for an unknown
  // transfer function).
  static string TransferFunctionDefines(TransferFunction tf);

  // Size of network in RAM.
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

  // Note: These use local byte order, so the serialized format is not
  // portable.
  // Caller owns new-ly allocated Network object.
  static Network *ReadFromFile(const string &filename,
                               bool verbose = true);
  static Network *ParseSerialized(const std::vector<uint8_t> &bytes,
                                  bool verbose = true);
  void SaveToFile(const string &filename);
  std::vector<uint8_t> Serialize() const;

  // Run the network to fill out the stimulation. The Stimulation
  // must be the right size (i.e. created from this Network) and
  // the input layer should be filled.
  void RunForward(Stimulation *stim) const;
  // Same, but only one layer. We read from stim.values[src_layer] and
  // write to stim.values[src_layer + 1].
  void RunForwardLayer(Stimulation *stim, int src_layer) const;


  // Serialization header. Always starts with MAGIC.
  static constexpr uint32_t MAGIC = MakeFOURCC('T', '7', 'n', 'w');
  // ... and followed by this version identifier. When changing the
  // format in an incompatible way, always increment this.
  static constexpr uint32_t FORMAT_ID = 0x27000772U;

  // layer[0] is the input layer.
  vector<Layer> layers;

  // The number of layers, including the input layer.
  int NumLayers() const {
    return layers.size();
  }

  template<typename ...Chunks>
  static Layer LayerFromChunks(Chunks... arg_chunks) {
    std::vector<Chunk> chunks = {{arg_chunks...}};
    int num_nodes = 0;
    for (const Chunk &chunk : chunks) num_nodes += chunk.num_nodes;
    return Layer{.num_nodes = num_nodes, .chunks = std::move(chunks)};
  }

  // Create a dense layer with zero weights (you gotta initialize these).
  static Chunk MakeDenseChunk(int num_nodes,
                              // region of previous layer to depend on
                              int span_start, int span_size,
                              TransferFunction transfer_function,
                              WeightUpdate weight_update);

  // XXX: This should return a Chunk?
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

  // For the common case of a 1D convolution.
  static Chunk Make1DConvolutionChunk(int span_start, int span_size,
                                      int num_features, int pattern_width,
                                      int x_stride,
                                      TransferFunction transfer_function,
                                      WeightUpdate weight_update);

  // And similarly for 2D convolutions.
  static Chunk Make2DConvolutionChunk(int span_start,
                                      int span_width, int span_height,
                                      int num_features,
                                      int pattern_width, int pattern_height,
                                      int x_stride, int y_stride,
                                      TransferFunction transfer_function,
                                      WeightUpdate weight_update);

  // Copies the given span, so num_nodes = span_size. The chunk is
  // fixed. This is currently a sparse chunk with IDENTITY transfer,
  // but it may become more efficient in the future.
  static Chunk MakeCopyChunk(int span_start, int span_size);

  // Generate a sparse chunk that samples from one or more spans.
  // For each span, ipn indices will be uniformly sampled, and all
  // the indices are sorted at the end.
  // The spans must not overlap.
  // The chunk's indices_per_node will be the sum of all the ipns,
  // and the span will be the smallest span that includes all the
  // spans.
  struct SparseSpan {
    int span_start = 0;
    int span_size = 0;
    int ipn = 0;
  };
  static Chunk MakeRandomSparseChunk(
      ArcFour *rc,
      int num_nodes,
      const std::vector<SparseSpan> &spans,
      TransferFunction transfer_function,
      WeightUpdate weight_update);

  // Computes the inverted indices for the given layer (the index
  // refers to the destination layer of the relevant gap) and chunk
  // within it. This maps the input span (from source layer) to the
  // nodes within the chunk where that node is used. These are only
  // used in training, in the backward pass. Returns empty indices
  // for the input layer. This is somewhat expensive so it should
  // be done once and saved.
  InvertedIndices ComputeInvertedIndices(int layer_idx,
                                         int chunk_idx) const;

  // Rounds trained. This matters when restarting from disk, because
  // for example the learning rate depends on the round.
  int64_t rounds = 0;
  // Total number of training examples processed.
  int64_t examples = 0;
};

// Inverted index for a chunk.
struct InvertedIndices {
  // For a given node, where do I output to in the next layer?
  // Note that nodes don't all have the same number of outputs.
  // This is a packed structure to facilitate GPU operations.
  //
  // Two parallel arrays, which are the size of the chunk's
  // input span (so length[0] is the number of uses of the
  // first node in the span). But, empty for dense chunks.
  // For a given node, where do my output indices start in
  // the indices array, and how many are there?
  std::vector<uint32_t> start;
  std::vector<uint32_t> length;

  // Packed array of indices.
  // For all chunk types, this is just the inverse of the corresponding
  // indices array.
  //
  // For dense chunks, we store nothing.
  //
  // For sparse chunks, this will be of size chunk.indices_per_node *
  // num_nodes, but any given node on this layer may be used more or
  // fewer times, including zero.
  //
  // The value in the output_indices array is an index into that
  // chunk's nodes (so 0 is the first node in the chunk), and all
  // the values will be less than chunk.num_nodes.
  //
  // If the chunk is a convolution array, we still have
  // chunk.indices[cidx] == z. But the indices array only
  // stores indices for "one feature", since they are the same for
  // each. Thus this has size
  //    chunk.indices_per_node * chunk.num_nodes /
  //    chunk.num_features
  // The cidx in this vector is an index into indices[] as above; some
  // cell within the pattern in a specific occurrence. It stands for
  // num_features edges, each with its own weight.
  std::vector<uint32_t> output_indices;
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
      // Note that we reserve space in the first layer's error, even
      // though this is the input and we don't normally backpropagate
      // to the input layer.
      error[i].resize(layer.num_nodes, 0.0f);
    }
  }
  // Empty, useless errors, but can be used to initialize vectors etc.
  Errors() {}
  Errors(const Errors &other) = default;

  // XXX just use the size of the corresponding vector!
  // The first entry here is not typically used, as it corresponds to
  // the input layer, but we keep it like this to be consistent with
  // Network and Stimulation. There are also cases where we abuse this
  // structure (e.g. SummaryStatisticsCL).
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

// Randomize the weights in a network, like to initialize it for
// training. TODO: Maybe should be in network-util or whatever.
// TODO: Should parameterize this, probably!
void RandomizeNetwork(ArcFour *rc, Network *net, int max_parallelism = 2);

#endif
