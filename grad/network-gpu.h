
// GPU (OpenCL) implementation of inference and training; goes
// with network.h.
//
// I ruined this one with hacks for the "grad" project.

#ifndef _NETWORK_GPU_H
#define _NETWORK_GPU_H

#include <string>
#include <optional>
#include <vector>
#include <mutex>
#include <cstdint>
#include <memory>

#include "base/logging.h"

#include "network.h"
#include "clutil.h"

#include "grad-util.h"

// PERF: For many of these, we use mutexes to avoid setting a
// kernels arguments from multiple threads. But we could have
// a mutex per layer or per ChunkKernel instead?
// PERF: In fact, it may work to initialize kernel arguments once
// during initialization, and just assume that there is one
// TrainingRoundGPU per NetworkGPU. Unclear if there's any overhead
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

  void SetVerbose(bool v) { verbose = v; };
  bool Verbose() const { return verbose; }

  // Read the weights and biases (and _aux) buffers from GPU back to the
  // Network object. Not thread safe!
  void ReadFromGPU();
  // Write only the weights and biases (and _aux) from CPU to GPU.
  void WriteToGPU();

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
  bool verbose = true;

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

  // Same, but from the CPU-side vector to the GPU buffer.
  template<class T>
  void CopyFromZeroOk(const std::vector<T> &vec, cl_mem buf) {
    if (buf == 0)
      return;
    CHECK(!vec.empty()) << "Only when buf == 0";
    CHECK_SUCCESS(clEnqueueWriteBuffer(cl->queue, buf, CL_TRUE, 0,
                                       sizeof (T) * vec.size(),
                                       vec.data(),
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
    num_examples(num_examples),
    input_size(net.layers[0].num_nodes),
    output_size(net.layers.back().num_nodes),
    cl(cl), net(&net) {
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
                                          output_size * num_examples);

    for (const auto &[name, tf] :
           std::initializer_list<
           std::pair<std::string, TransferFunction>>{
           {"grad1", GRAD1},
             // {"downshift2", DOWNSHIFT2},
         }) {
      std::vector<uint16_t> fwd =
        GradUtil::GetFunctionFromFile(
            StringPrintf("forward-%s.png", name.c_str()));
      std::vector<float> der =
        GradUtil::GetDerivativeFromFile(
            StringPrintf("deriv-%s.png", name.c_str()));

      forward_tables[tf] =
        CreateUninitializedGPUMemory<uint16_t>(cl->context, 65536);
      deriv_tables[tf] =
        CreateUninitializedGPUMemory<float>(cl->context, 65536);

      CopyBufferToGPU(cl->queue, fwd, forward_tables[tf]);
      CopyBufferToGPU(cl->queue, der, deriv_tables[tf]);
    }

    clFinish(cl->queue);
  }

  // Get the tabled data if the transfer function needs it; otherwise 0.
  cl_mem GetForwardTable(TransferFunction tf) {
    auto it = forward_tables.find(tf);
    if (it == forward_tables.end()) return 0;
    else return it->second;
  }

  cl_mem GetDerivTable(TransferFunction tf) {
    auto it = deriv_tables.find(tf);
    if (it == deriv_tables.end()) return 0;
    else return it->second;
  }

  // Load one example's input at the given index.
  void LoadInput(int idx, const std::vector<float> &inputs) {
    CHECK(idx >= 0 && idx < num_examples);
    CHECK_EQ(inputs.size(), input_size);
    CopyOffsetBufferToGPU(idx, inputs, stimulations[0]);
    clFinish(cl->queue);
  }

  // Load all the examples.
  void LoadInputs(const std::vector<float> &inputs) {
    CHECK_EQ(inputs.size(), input_size * num_examples);
    CopyBufferToGPU(cl->queue, inputs, stimulations[0]);
    clFinish(cl->queue);
  }

  void LoadExpected(int idx, const std::vector<float> &values) {
    CHECK(idx >= 0 && idx < num_examples);
    CHECK_EQ(values.size(), output_size);
    CopyOffsetBufferToGPU(idx, values, expected);
    clFinish(cl->queue);
  }

  // Load all the examples.
  void LoadExpecteds(const std::vector<float> &values) {
    CHECK_EQ(values.size(), output_size * num_examples);
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
    CHECK_EQ(err->error.size(), errors.size())
      << "arg " << err->error.size()
      << " training " << errors.size();
    for (int i = 0; i < err->error.size(); i++) {
      CopyOffsetBufferFromGPUTo(idx, errors[i], &err->error[i]);
    }
    clFinish(cl->queue);
  }

  // Copy (only) the final layer of the stimulation back to main memory.
  // The output vector must already have the correct size.
  void ExportOutput(int idx, std::vector<float> *out) {
    CHECK(idx >= 0 && idx < num_examples);
    CHECK(out->size() == output_size);
    CopyOffsetBufferFromGPUTo(idx, stimulations.back(), out);
    clFinish(cl->queue);
  }

  // Same but for all examples.
  void ExportOutputs(std::vector<float> *out) {
    CHECK(out->size() == num_examples * output_size);
    CopyBufferFromGPUTo(cl->queue, stimulations.back(), out);
  }

  // Same size as net->layers. 0th is input, final is the output.
  // Each memory is the layer's size * num_examples.
  std::vector<cl_mem> stimulations;

  // Same size as net->layers. 0th is input (typically unused),
  // final is the output.
  // Each memory is the layer's size * num_examples.
  std::vector<cl_mem> errors;
  // Size of final stimulation * num_examples.
  cl_mem expected;

  // HAX for grad project.
  // 65536 * half, for tabled transfer functions
  std::map<TransferFunction, cl_mem> forward_tables;
  // 65536 * float, for tabled transfer functions
  std::map<TransferFunction, cl_mem> deriv_tables;

  ~TrainingRoundGPU() {
    for (cl_mem m : stimulations) {
      CHECK_SUCCESS(clReleaseMemObject(m));
    }
    for (cl_mem m : errors) {
      CHECK_SUCCESS(clReleaseMemObject(m));
    }
    CHECK_SUCCESS(clReleaseMemObject(expected));

    for (auto &[k, m] : forward_tables)
      CHECK_SUCCESS(clReleaseMemObject(m));
    for (auto &[k, m] : deriv_tables)
      CHECK_SUCCESS(clReleaseMemObject(m));
  }

  const int num_examples = 0;
  const int64_t input_size = 0, output_size = 0;
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
  ForwardLayerCL(CL *cl, NetworkGPU *net_gpu);
  ~ForwardLayerCL();

  // Run the given layer of the network forward on each of the given
  // training instances, managing the parallelism internally.
  void RunForward(TrainingRoundGPU *train, int src_layer);
  // Run only for some 0 <= num_examples_prefix <= train->num_examples.
  // XXX worth keeping this?
  void RunForwardPrefix(TrainingRoundGPU *train, int src_layer, int num_examples);

 private:
  // The kernel (and associated program) objects for a specific chunk.
  // Note that we do not compile a chunk for the input layer, so these
  // can be 0 in the general case.
  struct ChunkKernel {
    cl_program program = 0;
    cl_kernel kernel = 0;
  };

  CL *cl = nullptr;
  NetworkGPU *net_gpu = nullptr;
  // Owned. Indexed by layer index and then chunk index. Parallel to
  // Net::layers.
  std::vector<std::vector<ChunkKernel>> layer_kernels;

  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(ForwardLayerCL);
};

// TODO: Rename this to like, TrainingParams.
// Network-wide configuration. The defaults are reasonable.
// (Due to a gcc bug, this cannot be nested within UpdateWeightsCL
// and used as a default argument.)
struct UpdateConfig {
  // The learning rate for a round is
  //    base_learning_rate / sqrt(1.0 + round_num * dampening)
  // Base learning rate should be in (0, 1].
  // The larger dampening is, the more quickly we reduce the
  // learning rate.
  double base_learning_rate = 0.01f;
  double learning_rate_dampening = 1.0f;

  // Update uses scratch space to improve paralellism (a lot).
  // This is the maximum number of floats to allocate between both
  // weights and biases; internally we figure out how to best
  // apportion this budget. Unless you need the GPU for other stuff,
  // increase this until it says "everything fits :)" or fails to
  // allocate the memory.
  //
  // TODO: Make sure some tests set it very low to exercise those
  // code paths!
  int64_t max_num_scratch = 1LL << 31;

  // Parameters for ADAM and YOGI.
  // 1e-6 is traditional here, but some recommend much larger
  // values for sparse problems (even 1.0!). Since this is used
  // in the denominator, larger values might help control
  // runaway gradients, but at the cost of slower convergence.
  float adam_epsilon = 1.0e-3;
  // Weights for the exponential moving average of the first and
  // second moments. These are not usually configured.
  float adam_b1 = 0.9f;
  float adam_b2 = 0.999f;

  // If true, clips each gradient component to [-1,1] before
  // applying any update.
  bool clipping = false;
  // If true, ensures that the resulting weights after update
  // are always within [-constrain_max, constrain_max] (which
  // also prevents them from being infinite or nan).
  bool constrain = true;
  float weight_constrain_max = 16.0f;
  float bias_constrain_max = 16384.0f;

  // If true, then propagated error is always in [-error_max, error_max].
  bool clip_error = false;
  float error_max = 1000.0;

  // When updating a node in a convolutional layer, scale the update
  // by 1/(occurrences^conv_update_exponent), where 0.0 yields no scaling
  // (perhaps the most principled, as each occurrence is contributing
  // error), 0.5 is 1/sqrt(occ), a compromise that has worked in the
  // past, and 1.0 is 1/occ, as though there is just one occurrence.
  float conv_update_exponent = 5.0f;

  std::string ToString() const;
};

// Set the error values from the actual and expected outputs, possibly
// applying some remapping of them.
struct SetOutputErrorCL {
  using UpdateConfig = ::UpdateConfig;

  // Optional remap function takes chunk id, node index within chunk, and
  // value; see setoutputerror.cl.
  SetOutputErrorCL(
      CL *cl, NetworkGPU *net_gpu,
      UpdateConfig config = {},
      const std::optional<std::string> remap_define = std::nullopt);

  // Runs on all the examples in the round.
  void SetOutputError(TrainingRoundGPU *train);

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
  NetworkGPU *net_gpu = nullptr;
  const UpdateConfig config;

  // One for each chunk in the final layer.
  // Owned.
  std::vector<ChunkKernel> kernels;

  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(SetOutputErrorCL);
};

// Propagate errors backwards. Note that errors flow from "dst" to "src".
// There are two passes in here but this is hidden from the caller.
struct BackwardLayerCL {
  using UpdateConfig = ::UpdateConfig;

  BackwardLayerCL(CL *cl, NetworkGPU *net, UpdateConfig config = {});
  ~BackwardLayerCL();

  // Propagate errors from dst_layer to dst_layer-1. Runs both passes.
  // Runs on all examples in the round.
  // PERF: If some prefix of the layers consist only of fixed chunks,
  // then we don't need to propagate errors because we won't use them.
  // This could be a big performance improvement if iteratively growing
  // a model by adding layers at the end.
  void BackwardLayer(TrainingRoundGPU *training_round,
                     int dst_layer);

  // Used internally to schedule chunks. This is only exposed for testing.
  static std::pair<std::vector<int>, std::vector<bool>> OptimizeChunkSchedule(
      const std::vector<Chunk> &chunks, bool verbose = false);

 private:
  // Order in which to process the chunks for each layer. The compiled
  // kernels are only correct if they are scheduled in this order,
  // which we optimize to avoid += if possible.
  std::vector<std::vector<int>> chunk_schedule;

  // For this phase there are two passes, so two kernels.
  struct ChunkKernel {
    cl_program program1 = 0;
    cl_kernel kernel1 = 0;

    cl_program program2 = 0;
    cl_kernel kernel2 = 0;
  };

  CL *cl = nullptr;
  NetworkGPU *net_gpu = nullptr;
  const UpdateConfig config;

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
  DecayWeightsCL(CL *cl, NetworkGPU *net_gpu, float decay_factor);
  ~DecayWeightsCL();

  void Decay(int layer_idx);

 private:
  // TODO: follow the learning rate schedule instead of a constant
  // decay. Perhaps this can just be merged into updateweights.
  const float decay_factor;
  CL *cl = nullptr;
  NetworkGPU *net_gpu = nullptr;

  cl_program program;
  cl_kernel kernel;
  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(DecayWeightsCL);
};


struct UpdateWeightsCL {
  using UpdateConfig = ::UpdateConfig;

  // The number of examples per round is needed as a compile-time
  // constant.
  UpdateWeightsCL(CL *cl, NetworkGPU *net_gpu,
                  int examples_per_round,
                  UpdateConfig config = {});
  ~UpdateWeightsCL();

  // Run on all examples in the round.
  // The number of training examples must match the configured amount.
  void Update(TrainingRoundGPU *train, int layer);

  // For debugging: Get the compiled program (as PTX assembly) for the
  // given layer and chunk, which must be in range. Probably only
  // works for NVIDIA cards.
  std::string GetProgram(int layer_idx, int chunk_idx) const;

 private:
  const int examples_per_round = 0;
  const UpdateConfig config;

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
    // fit in the scratch space at once for this chunk. At
    // most examples_per_round.
    int w = 0;
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
  NetworkGPU *net_gpu = nullptr;

  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(UpdateWeightsCL);
};

// Replaces example 0's errors with the mean value over all
// examples for that node, and example 1's errors with the variance.
// This messes up the errors, so this training round shouldn't be used
// for e.g. UpdateWeights after this point!
// (Requires training->num_examples >= 2.)
struct SummaryStatisticsCL {

  SummaryStatisticsCL(CL *cl, NetworkGPU *net_gpu);
  ~SummaryStatisticsCL();

  void Compute(TrainingRoundGPU *training, int layer_idx);

 private:
  CL *cl = nullptr;
  NetworkGPU *net_gpu = nullptr;

  cl_program program;
  cl_kernel kernel;
  std::mutex m;

  DISALLOW_COPY_AND_ASSIGN(SummaryStatisticsCL);
};


#endif
