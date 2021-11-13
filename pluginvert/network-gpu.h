
// GPU (OpenCL) implementation of inference and training; goes
// with network.h.

#ifndef _NETWORK_GPU_H
#define _NETWORK_GPU_H

#include <string>
#include <optional>
#include <vector>
#include <mutex>

#include "base/logging.h"

#include "network.h"
#include "timer.h"
#include "clutil.h"

// PERF: For many of these, we use mutexes to avoid setting a
// kernels arguments from multiple threads. But we could have
// a mutex per layer or per ChunkKernel instead?
// PERF: In fact, it may work to initialize kernel arguments once
// during initialization, and just assume that there is one
// TrainingRound per NetworkGPU. Unclear if there's any overhead
// of setting the args over and over (perhaps it does some dynamic
// recompilation or invalidates caches?)

// Network that has its bulky data (indices and inverted indices,
// weights, biases) stored on GPU for fast inference and training.
// During training the parameters (weights, biases) are updated in the
// GPU copies, which can then be copied back to the associated CPU
// copies and serialized, etc.
//
// Other network data (e.g. choice of transfer function, convolution
// pattern dimensions) is supplied as arguments to kernels below, or
// even baked into the compiled kernels as constants.
struct NetworkGPU {

  // Allocates memory on GPU to parallel the network. Keeps a mutable
  // alias to the network so that it can be updated after training
  // (ReadFromGPU), but does not take ownership.
  NetworkGPU(CL *cl, Network *net);
  ~NetworkGPU();

  // Read the weights and biases (which is the only thing that can
  // change) from GPU back to the Network object. Not thread safe!
  void ReadFromGPU();

  struct GPUChunk {
    // Empty memories are represented as 0 (invalid cl_mem), since opencl
    // doesn't support empty memories. Everything is empty for the token
    // input chunk (which goes unused), but indices can also be empty in
    // normal cases (dense layers; unused _aux).
    // readonly.
    cl_mem indices;
    // read/write.
    cl_mem weights;
    cl_mem biases;
    // read/write
    cl_mem weights_aux;
    cl_mem biases_aux;

    // Inverted index. These are empty (0) for dense and input chunks.
    // See Network::ComputeInvertedIndices.
    cl_mem ii_start;
    cl_mem ii_length;
    cl_mem ii_indices;
  };

  struct GPULayer {
    std::vector<GPUChunk> chunks;
  };

  // Owned.
  std::vector<GPULayer> layers;

  // Not owned!
  CL *cl = nullptr;
  Network *net = nullptr;
 private:
  // Like CopyBufferFromGPUTo, but don't wait for the command to finish.
  // Also allows cl_mem to be 0, standing for an empty memory.
  template<class T>
  void ReadToZeroOk(cl_mem buf, std::vector<T> *vec) {
    if (buf == 0)
      return;
    CHECK_SUCCESS(
        clEnqueueReadBuffer(cl->queue, buf, CL_TRUE, 0,
                            sizeof (T) * vec->size(),
                            vec->data(),
                            // No wait-list or event.
                            0, nullptr,
                            nullptr));
  }

  DISALLOW_COPY_AND_ASSIGN(NetworkGPU);
};

// Data on the GPU for a single training round: A fixed-size array of
// training examples (and their errors, etc.). Can be reused across
// rounds.
//
// The goal of storing these in an array is to enable kernels to run
// over many examples at once. This is especially helpful for small
// chunks, which can't use the full bandwidth of the GPU when run
// individually.
struct TrainingRoundGPU {
  TrainingRoundGPU(int num_examples, CL *cl, const Network &net) :
    num_examples(num_examples), cl(cl), net(&net) {
    for (const Layer &layer : net.layers) {
      stimulations.push_back(
          CreateUninitializedGPUMemory<float>(cl->context,
                                              layer.num_nodes * num_examples));
      errors.push_back(
          CreateUninitializedGPUMemory<float>(cl->context,
                                              layer.num_nodes * num_examples));
    }

    expected =
      CreateUninitializedGPUMemory<float>(cl->context,
                                          net.layers.back().num_nodes *
                                          num_examples);
  }

  // Load one example's input at the given index.
  void LoadInput(int idx, const std::vector<float> &inputs) {
    CHECK(idx >= 0 && idx < num_examples);
    CHECK_EQ(inputs.size(), net->layers[0].num_nodes);
    CopyOffsetBufferToGPU(idx, inputs, stimulations[0]);
    clFinish(cl->queue);
  }

  // Load all the examples.
  void LoadInputs(const std::vector<float> &inputs) {
    CHECK_EQ(inputs.size(), net->layers[0].num_nodes * num_examples);
    CopyBufferToGPU(cl->queue, inputs, stimulations[0]);
    clFinish(cl->queue);
  }

  void LoadExpected(int idx, const std::vector<float> &values) {
    CHECK(idx >= 0 && idx < num_examples);
    CHECK_EQ(values.size(), net->layers.back().num_nodes);
    CopyOffsetBufferToGPU(idx, values, expected);
    clFinish(cl->queue);
  }

  // Load all the examples.
  void LoadExpecteds(const std::vector<float> &values) {
    CHECK_EQ(values.size(), net->layers.back().num_nodes * num_examples);
    CopyBufferToGPU(cl->queue, values, expected);
    clFinish(cl->queue);
  }

  void ExportStimulation(int idx, Stimulation *stim) {
    CHECK(idx >= 0 && idx < num_examples);
    CHECK_EQ(stim->values.size(), stimulations.size());
    for (int i = 0; i < stim->values.size(); i++) {
      CopyOffsetBufferFromGPUTo(idx, stimulations[i], &stim->values[i]);
    }
    clFinish(cl->queue);
  }

  void ExportErrors(int idx, Errors *err) {
    CHECK(idx >= 0 && idx < num_examples);
    CHECK_EQ(err->error.size(), errors.size());
    for (int i = 0; i < err->error.size(); i++) {
      CopyOffsetBufferFromGPUTo(idx, errors[i], &err->error[i]);
    }
    clFinish(cl->queue);
  }

  // Copy (only) the final layer of the stimulation back to main memory.
  // The output vector must already have the correct size.
  void ExportOutput(int idx, std::vector<float> *out) {
    CHECK(idx >= 0 && idx < num_examples);
    CopyOffsetBufferFromGPUTo(idx, stimulations.back(), out);
    clFinish(cl->queue);
  }

  // Same but for all examples.
  void ExportOutputs(std::vector<float> *out) {
    CopyBufferFromGPUTo(cl->queue, stimulations.back(), out);
  }

  // Same size as net->layers. 0th is input, final is the output.
  // Each memory is the layer's size * num_examples.
  std::vector<cl_mem> stimulations;
  // Same size as net->layers. 0th is input (unused), final is the output.
  // Each memory is the layer's size * num_examples.
  // (Note: Prior to chunks, this used to exclude the (unused) input error)
  std::vector<cl_mem> errors;
  // Size of final stimulation * num_examples.
  cl_mem expected;

  ~TrainingRoundGPU() {
    for (cl_mem m : stimulations) {
      CHECK_SUCCESS(clReleaseMemObject(m));
    }
    for (cl_mem m : errors) {
      CHECK_SUCCESS(clReleaseMemObject(m));
    }
    CHECK_SUCCESS(clReleaseMemObject(expected));
  }

  const int num_examples = 0;
  CL *cl = nullptr;
  const Network *net = nullptr;
 private:

  // Assuming buf is num_examples * vec.size() floats, copy the
  // vector's data to the indexed example in the buffer. Doesn't
  // wait for command to finish.
  void CopyOffsetBufferToGPU(int idx,
                             const std::vector<float> &vec, cl_mem buf) {
    CHECK(!vec.empty());
    const int offset = sizeof (float) * vec.size() * idx;
    CHECK_SUCCESS(clEnqueueWriteBuffer(
                      cl->queue, buf, CL_TRUE,
                      offset, sizeof (float) * vec.size(), vec.data(),
                      // No wait-list or event.
                      0, nullptr, nullptr));
  }

  // Assuming src is num_examples * dst->size() floats, copy the
  // data for the indexed example to the destination.
  // Does not wait for command to finish.
  void CopyOffsetBufferFromGPUTo(int idx,
                                 cl_mem src,
                                 std::vector<float> *dst) {
    // Empty not supported by cl copy nor TrainingRound.
    CHECK(!dst->empty());
    const int offset = sizeof (float) * dst->size() * idx;
    CHECK_SUCCESS(
        clEnqueueReadBuffer(cl->queue, src, CL_TRUE,
                            offset, sizeof (float) * dst->size(),
                            dst->data(),
                            // No wait-list or event.
                            0, nullptr,
                            nullptr));
  }

  DISALLOW_COPY_AND_ASSIGN(TrainingRoundGPU);
};


// Forward pass.
struct ForwardLayerCL {
  ForwardLayerCL(CL *cl, const Network &net);
  ~ForwardLayerCL();

  // Run the given layer of the network forward on each of the given
  // training instances, managing the parallelism internally.
  void RunForward(
      NetworkGPU *net_gpu, TrainingRoundGPU *train, int src_layer);

 private:
  // The kernel (and associated program) objects for a specific chunk.
  // Note that we do not compile a chunk for the input layer, so these
  // can be 0 in the general case.
  struct ChunkKernel {
    cl_program program = 0;
    cl_kernel kernel = 0;
  };

  CL *cl = nullptr;
  // Owned. Indexed by layer index and then chunk index. Parallel to
  // Net::layers.
  std::vector<std::vector<ChunkKernel>> layer_kernels;

  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(ForwardLayerCL);
};

// Set the error values from the actual and expected outputs, possibly
// applying some remapping of them.
struct SetOutputErrorCL {

  // Optional remap function takes chunk id, node index within chunk, and
  // value; see setoutputerror.cl.
  SetOutputErrorCL(
      CL *cl, const Network &net,
      const std::optional<std::string> remap_define = std::nullopt);

  // Runs on all the examples in the round.
  void SetOutputError(NetworkGPU *net_gpu, TrainingRoundGPU *train);

  ~SetOutputErrorCL() {
    for (ChunkKernel &ck : kernels) {
      clReleaseKernel(ck.kernel);
      clReleaseProgram(ck.program);
    }
  }

  struct ChunkKernel {
    cl_program program = 0;
    cl_kernel kernel = 0;
  };

 private:
  CL *cl = nullptr;

  // One for each chunk in the final layer.
  // Owned.
  std::vector<ChunkKernel> kernels;

  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(SetOutputErrorCL);
};

// Propagate errors backwards. Note that errors flow from "dst" to "src".
// There are two passes in here but this is hidden from the caller.
struct BackwardLayerCL {
  // See backwardsecondpass.cl. TODO: Make these configurable when
  // creating this. They could have different values on a per-chunk
  // basis, though it's not clear why we'd ever do that.
  static constexpr bool CLIP_ERROR = true;
  static constexpr float LARGE_ERROR = 1000000.0f;

  BackwardLayerCL(CL *cl, const Network &net);
  ~BackwardLayerCL();

  // Propagate errors from dst_layer to dst_layer-1. Runs both passes.
  // Runs on all examples in the round.
  // PERF: If some prefix of the layers consist only of fixed chunks,
  // then we don't need to propagate errors because we won't use them.
  // This could be a big performance improvement if iteratively growing
  // a model by adding layers at the end.
  void BackwardLayer(NetworkGPU *net_gpu,
                     TrainingRoundGPU *training_round,
                     int dst_layer);

 private:
  // For this phase there are two passes, so two kernels.
  struct ChunkKernel {
    cl_program program1 = 0;
    cl_kernel kernel1 = 0;

    cl_program program2 = 0;
    cl_kernel kernel2 = 0;
  };

  CL *cl = nullptr;

  // Input layer has unused placeholder kernels (0) to keep this
  // parallel to network structure.
  std::vector<std::vector<ChunkKernel>> layer_kernels;

  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(BackwardLayerCL);
};

// Optional and unprincipled L2-like regularization.
// Decays every weight by a constant multiplicative factor.
struct DecayWeightsCL {

  // Decay factor should be a number slightly less than 1, like 0.99999f.
  DecayWeightsCL(CL *cl, const Network &net, float decay_factor);
  ~DecayWeightsCL();

  void Decay(NetworkGPU *net_gpu, int layer_idx);

 private:
  CL *cl = nullptr;
  cl_program program;
  cl_kernel kernel;
  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(DecayWeightsCL);
};


// TODO: First fix the adam bug.
// Do this by always allocating an extra memory (scratch space)
// for the weight increments (in constructor), and += all the
// increments into there. This is actually pretty easy because
// the current chunk_weights buffer is write-only anyway.
// Do adam update as a second pass. Make
// sure this still passes learning tests (hopefully it is better?).
//
// Next, follow the plan noted in the cc code to produce the weight
// increments with some amount of parallelism. The constructor can
// have some memory budget and choose the multiplier W per chunk to
// use that budget (also, W should probably divide the number of
// examples?)
struct UpdateWeightsCL {
  // The number of examples per round is needed as a compile-time
  // constant.
  UpdateWeightsCL(CL *cl, const Network &net,
                  int examples_per_round,
                  float adam_epsilon = DEFAULT_ADAM_EPSILON);
  ~UpdateWeightsCL();

  // TODO: Make configurable.
  static constexpr bool CLIPPING = false;
  static constexpr bool CONSTRAIN = true;
  static constexpr float WEIGHT_CONSTRAIN_MAX = 16.0f;
  static constexpr float BIAS_CONSTRAIN_MAX = 16384.0f;
  // 1e-6 is traditional here, but some recommend much larger
  // values for sparse problems (even 1.0!). Since this is used
  // in the denominator, larger values might help control
  // runaway gradients, but at the cost of slower convergence.
  static constexpr float DEFAULT_ADAM_EPSILON = 1.0e-6;

  // Weights for the exponential moving average of the first and
  // second moments.
  static constexpr float ADAM_B1 = 0.9f;
  static constexpr float ADAM_B2 = 0.999f;



  // Run on all examples in the round.
  // learning_rate here is something like 0.01f (internally scaled
  // by number of examples etc.)
  // The number of training examples must match the configured amount.
  void Update(NetworkGPU *net_gpu, TrainingRoundGPU *train,
              float learning_rate, int layer);

 private:
  const int examples_per_round = 0;
  const float adam_epsilon = DEFAULT_ADAM_EPSILON;
  struct ChunkKernel {
    cl_program program1 = 0;
    cl_kernel kernel1 = 0;

    // summing pass
    cl_program program_sum = 0;
    cl_kernel kernel_sum = 0;

    // second pass
    cl_program program2 = 0;
    cl_kernel kernel2 = 0;

    // The W ('width') is the number of examples that we can
    // fit in the scratch space at once for this chunk.
    int w = 0;

    // cl_mem weight_grad_tmp = 0;
    // cl_mem bias_grad_tmp = 0;
  };

  // Shared scratch space. This is at least the size of the weights
  // (resp. biases) array for every chunk in the corresponding network.
  cl_mem weight_grad_tmp = 0;
  cl_mem bias_grad_tmp = 0;
  // Number of elements (not bytes) in the memories above.
  int64_t num_weight_grad = 0;
  int64_t num_bias_grad = 0;

  // Input layer has unused placeholder kernels (0) to keep this
  // parallel to network structure.
  std::vector<std::vector<ChunkKernel>> layer_kernels;

  CL *cl = nullptr;

  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(UpdateWeightsCL);
};

#endif
