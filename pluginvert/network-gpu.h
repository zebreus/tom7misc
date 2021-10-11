
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
    // ...
  };

  CL *cl = nullptr;

  // Input layer has unused placeholder kernels (0) to keep this
  // parallel to network structure.
  std::vector<std::vector<ChunkKernel>> layer_kernels;

  std::mutex m;
};

#if 0

// Optional and unprincipled L2-like regularization.
// Decays every weight by a constant multiplicative factor.
struct DecayWeightsCL {

  DecayWeightsCL(CL *cl, const Network &net, float decay_factor) : cl(cl) {
    string base_src = Util::ReadFile("decayweights.cl");

    string kernel_src;
    StringAppendF(&kernel_src, "\n#define DECAY_FACTOR %.9ff\n", decay_factor);
    kernel_src += base_src;
    auto p = cl->BuildOneKernel(kernel_src, "DecayWeights");
    program = p.first;
    kernel = p.second;
  }

  ~DecayWeightsCL() {
    CHECK_SUCCESS(clReleaseKernel(kernel));
    CHECK_SUCCESS(clReleaseProgram(program));
  }

  struct Context {
    Context(DecayWeightsCL *parent, NetworkGPU *net_gpu, int layer) :
      parent(parent), net_gpu(net_gpu), layer(layer) {
      layer_weights = net_gpu->layers[layer].weights;
    }

    void Decay(int layer) {
      CL *cl = parent->cl;

      // PERF: Should actually be able to run in parallel across the entire
      // network if we weren't sharing a single kernel. Every weight
      // just scaled independently.
      WriteMutexLock ml(&parent->m);

      const int num_nodes = net_gpu->net->num_nodes[layer + 1];
      const int ipn = net_gpu->net->layers[layer].indices_per_node;

      auto kernel = parent->kernel;
      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                   (void *)&layer_weights));

      size_t global_work_offset[] = { 0 };
      // Total number of weights (could use 2D but why bother?)
      size_t global_work_size[] = { (size_t)(num_nodes * ipn) };
      CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, kernel,
                                           // work dimensions
                                           1,
                                           // global work offset
                                           global_work_offset,
                                           // global work size
                                           global_work_size,
                                           // local work size
                                           nullptr,
                                           // no wait list
                                           0, nullptr,
                                           // no event
                                           nullptr));
      clFinish(cl->queue);
    }

    cl_mem layer_weights;
    DecayWeightsCL *parent = nullptr;
    NetworkGPU *net_gpu;
    const int layer;
  };


  CL *cl = nullptr;
  cl_program program;
  cl_kernel kernel;
  std::shared_mutex m;
};

struct UpdateWeightsCL {
  using string = std::string;

  UpdateWeightsCL(CL *cl, const Network &net) : cl(cl) {
    // Note that this one doesn't depend on the transfer function/derivative.
    // Also unlike others, the argument values actually change
    // in the convolution case.

    string base_src = Util::ReadFile("updateweights.cl");
    for (int layer = 0; layer < net.layers.size(); layer++) {
      const int indices_per_node = net.layers[layer].indices_per_node;

      const LayerType layer_type = net.layers[layer].type;

      const int num_occurrences =
        net.layers[layer].num_occurrences_across *
        net.layers[layer].num_occurrences_down;

      string kernel_src;
      StringAppendF(&kernel_src,
                    "\n"
                    "#define INDICES_PER_NODE %d\n"
                    "#define NUM_OCCURRENCES %d\n"
                    "#define NUM_FEATURES %d\n",
                    indices_per_node,
                    num_occurrences,
                    net.layers[layer].num_features);

      string kernel_name;
      switch (layer_type) {
      case LAYER_DENSE:
        kernel_name = "UpdateWeightsDense";
        break;
      case LAYER_SPARSE:
        kernel_name = "UpdateWeightsSparse";
        break;
      case LAYER_CONVOLUTION_ARRAY:
        kernel_name = "UpdateWeightsConvolutional";
        break;
      default:
        CHECK(false) << "Unsupported layer type "
                     << LayerTypeName(layer_type);
      }

      kernel_src += base_src;
      auto [program, kernel] =
        cl->BuildOneKernel(kernel_src, kernel_name);

      // XXX don't save debugging stuff
      layer_kernels.emplace_back(program, kernel, layer_type,
                                 num_occurrences,
                                 indices_per_node);
    }
  }

  struct Context {
    Context(UpdateWeightsCL *parent, NetworkGPU *net_gpu, int layer) :
      parent(parent), net_gpu(net_gpu), layer(layer) {

      layer_indices = net_gpu->layers[layer].indices;
      layer_weights = net_gpu->layers[layer].weights;
      layer_biases = net_gpu->layers[layer].biases;
    }

    void Update(float learning_rate, TrainingRoundGPU *train, int layer) {
      CL *cl = parent->cl;

      // Really can't run these in parallel because of concurrent writes to net.
      WriteMutexLock ml(&parent->m);

      cl_mem layer_error = train->errors[layer];
      cl_mem layer_values = train->stimulations[layer];

      auto [program_, kernel, layer_type, num_occurrences, ipn] =
        parent->layer_kernels[layer];
      // Sanity check that this was compiled with the right constants.
      CHECK(ipn == net_gpu->net->layers[layer].indices_per_node);
      CHECK(layer_type == net_gpu->net->layers[layer].type);

      // For convolution layers, scale the learning rate down,
      // as there will be lots of occurrences. Scaling linearly
      // is probably the most principled (each occurrence yields an
      // error and thus an update), but sqrt seems to work better
      // (the errors often cancel each other out).
      const float effective_learning_rate =
        (layer_type == LAYER_CONVOLUTION_ARRAY) ?
        learning_rate * (1.0f / sqrtf((float)num_occurrences)) :
        learning_rate;

      const int num_nodes = net_gpu->net->num_nodes[layer + 1];

      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_float),
                                   (void *)&effective_learning_rate));
      CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                   (void *)&layer_error));
      CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                   (void *)&layer_indices));
      CHECK_SUCCESS(clSetKernelArg(kernel, 3, sizeof (cl_mem),
                                   (void *)&layer_values));
      CHECK_SUCCESS(clSetKernelArg(kernel, 4, sizeof (cl_mem),
                                   (void *)&layer_weights));
      CHECK_SUCCESS(clSetKernelArg(kernel, 5, sizeof (cl_mem),
                                   (void *)&layer_biases));

      // Arguments are the same for the different layer types,
      // but for convolution array it's actually a 2D kernel
      // over features and weights.

      if (layer_type == LAYER_CONVOLUTION_ARRAY) {
        const int num_features = net_gpu->net->layers[layer].num_features;
        size_t global_work_offset[] = { 0, 0 };
        size_t global_work_size[] = { (size_t)num_features,
                                      (size_t)ipn };
        CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, kernel,
                                             // work dimensions
                                             2,
                                             // global work offset
                                             global_work_offset,
                                             // global work size
                                             global_work_size,
                                             // local work size
                                             nullptr,
                                             // no wait list
                                             0, nullptr,
                                             // no event
                                             nullptr));
      } else {
        size_t global_work_offset[] = { 0 };
        size_t global_work_size[] = { (size_t)num_nodes };
        CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, kernel,
                                             // work dimensions
                                             1,
                                             // global work offset
                                             global_work_offset,
                                             // global work size
                                             global_work_size,
                                             // local work size
                                             nullptr,
                                             // no wait list
                                             0, nullptr,
                                             // no event
                                             nullptr));
      }
      clFinish(cl->queue);
    }

    void Finish() {
      CL *cl = parent->cl;
      clFinish(cl->queue);
    }

    cl_mem layer_indices, layer_weights, layer_biases;
    UpdateWeightsCL *parent = nullptr;
    NetworkGPU *net_gpu;
    const int layer;
  };


  ~UpdateWeightsCL() {
    for (auto &[p, k, type_unused, occ_unused, ipn_unused] : layer_kernels) {
      CHECK_SUCCESS(clReleaseKernel(k));
      CHECK_SUCCESS(clReleaseProgram(p));
    }
  }

  CL *cl = nullptr;
  // Owned. Indexed by layer id.
  // (layer type, num_occurrences, indices_per_node)
  std::vector<std::tuple<cl_program, cl_kernel, LayerType, int, int>>
  layer_kernels;

  std::shared_mutex m;
};

#endif

#endif
