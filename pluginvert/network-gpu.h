
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
  void ReadFromGPU() {
    CHECK(net->layers.size() == layers.size());
    for (int layer = 0; layer < net->layers.size(); layer++) {
      Layer *cpu_layer = &net->layers[layer];
      GPULayer *gpu_layer = &layers[layer];
      CHECK(cpu_layer->chunks.size() == gpu_layer->chunks.size());
      for (int chunk = 0; chunk < cpu_layer->chunks.size(); chunk++) {
        Chunk *cpu_chunk = &cpu_layer->chunks[chunk];
        GPUChunk *gpu_chunk = &gpu_layer->chunks[chunk];
        if (gpu_chunk->weights != 0)
          ReadTo(gpu_chunk->weights, &cpu_chunk->weights);
        if (gpu_chunk->biases != 0)
          ReadTo(gpu_chunk->biases, &cpu_chunk->biases);
      }
    }
    clFinish(cl->queue);
  }

  // Like CopyBufferFromGPUTo, but don't wait for the command to finish.
  template<class T>
  void ReadTo(cl_mem buf, std::vector<T> *vec) {
    CHECK_SUCCESS(
        clEnqueueReadBuffer(cl->queue, buf, CL_TRUE, 0,
                            sizeof (T) * vec->size(),
                            vec->data(),
                            // No wait-list or event.
                            0, nullptr,
                            nullptr));
  }

  struct GPUChunk {
    // Empty memories are represented as 0 (invalid cl_mem), since opencl
    // doesn't support empty memories. Everything is empty for the token
    // input chunk (which goes unused), but indices can also be empty in
    // normal cases (dense layers).
    // readonly.
    cl_mem indices;
    // read/write.
    cl_mem weights;
    cl_mem biases;

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
  DISALLOW_COPY_AND_ASSIGN(NetworkGPU);
};

// Data on the GPU for a single example in a single training round. Can
// be reused across rounds.
// TODO: Since each training round is actually over a batch of examples,
// it may be much better to support an array of examples here, which could
// allow us to run kernels across multiple examples at once. This could
// be especially fruitful for layers aren't even using the full bandwidth
// of the GPU.
// (Might want to do this after the chunk rewrite though, so that I can
// at least benchmark it.)
struct TrainingRoundGPU {
  TrainingRoundGPU(CL *cl, const Network &net) : cl(cl), net(&net) {
    for (const Layer &layer : net.layers) {
      stimulations.push_back(
          CreateUninitializedGPUMemory<float>(cl->context, layer.num_nodes));
      errors.push_back(
          CreateUninitializedGPUMemory<float>(cl->context, layer.num_nodes));
    }

    expected =
      CreateUninitializedGPUMemory<float>(cl->context,
                                          net.layers.back().num_nodes);
  }

  void LoadInput(const std::vector<float> &inputs) {
    CHECK_EQ(inputs.size(), net->layers[0].num_nodes);
    CopyBufferToGPU(cl->queue, inputs, stimulations[0]);
  }

  void LoadExpected(const std::vector<float> &values) {
    CHECK_EQ(values.size(), net->layers.back().num_nodes);
    CopyBufferToGPU(cl->queue, values, expected);
  }

  void ExportStimulation(Stimulation *stim) {
    CHECK_EQ(stim->values.size(), stimulations.size());
    for (int i = 0; i < stim->values.size(); i++) {
      CopyBufferFromGPUTo(cl->queue, stimulations[i], &stim->values[i]);
    }
  }

  void ExportErrors(Errors *err) {
    CHECK_EQ(err->error.size(), errors.size());
    for (int i = 0; i < err->error.size(); i++) {
      CopyBufferFromGPUTo(cl->queue, errors[i], &err->error[i]);
    }
  }

  // Copy (only) the final layer of the stimulation back to main memory.
  // The output vector must already have the correct size.
  void ExportOutput(std::vector<float> *out) {
    CopyBufferFromGPUTo(cl->queue, stimulations.back(), out);
  }

  // Same size as net->layers. 0th is input, final is the output.
  std::vector<cl_mem> stimulations;
  // Same size as net->layers. 0th is input (unused), final is the output.
  // XXX (this used to not include the (unused) input error)
  std::vector<cl_mem> errors;
  // Size of final stimulation.
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

  CL *cl;
  const Network *net;
 private:
  DISALLOW_COPY_AND_ASSIGN(TrainingRoundGPU);
};


// Forward pass.
struct ForwardLayerCL {

  ForwardLayerCL(CL *cl, const Network &net);
  ~ForwardLayerCL();

  // Run the given layer of the network forward on the given training
  // instance (TODO: expand to multiple instances in parallel).
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
};

// Set the error values from the actual and expected outputs, possibly
// applying some remapping of them.
struct SetOutputErrorCL {

  // Optional remap function takes chunk id, node index within chunk, and
  // value; see setoutputerror.cl.
  SetOutputErrorCL(
      CL *cl, const Network &net,
      const std::optional<std::string> remap_define = std::nullopt);

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
};

struct UpdateWeightsCL {
  UpdateWeightsCL(CL *cl, const Network &net);
  ~UpdateWeightsCL();

  void Update(NetworkGPU *net_gpu, TrainingRoundGPU *train,
              float learning_rate, int layer);

 private:
  struct ChunkKernel {
    cl_program program = 0;
    cl_kernel kernel = 0;
  };

  // Input layer has unused placeholder kernels (0) to keep this
  // parallel to network structure.
  std::vector<std::vector<ChunkKernel>> layer_kernels;

  CL *cl = nullptr;

  std::mutex m;
};

#endif
