
// GPU (OpenCL) implementation of inference and training; goes
// with network.h.

#ifndef _NETWORK_GPU_H
#define _NETWORK_GPU_H

#include <string>
#include <optional>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "network.h"
#include "timer.h"
#include "clutil.h"


// Network that lives entirely on the GPU, but can be copied back to
// the Network object.
struct NetworkGPU {
  // XXX DESTRUCTOR.
  NetworkGPU(CL *cl, Network *net) : cl(cl), net(net) {

    layers.resize(net->layers.size());
    for (int layer = 0; layer < net->layers.size(); layer++) {
      layers[layer].indices =
        MoveMemoryToGPU(cl->context, cl->queue, true,
                        &net->layers[layer].indices);
      layers[layer].weights =
        MoveMemoryToGPU(cl->context, cl->queue, false,
                        &net->layers[layer].weights);
      layers[layer].biases =
        MoveMemoryToGPU(cl->context, cl->queue, false,
                        &net->layers[layer].biases);
    }

    // TODO: Invert here by calling some members of Network.
    inverted_indices.resize(net->inverted_indices.size());
    for (int layer = 0; layer < net->layers.size(); layer++) {
      inverted_indices[layer].start =
        MoveMemoryToGPUConst(cl->context, cl->queue,
                             net->inverted_indices[layer].start);
      inverted_indices[layer].length =
        MoveMemoryToGPUConst(cl->context, cl->queue,
                             net->inverted_indices[layer].length);
      inverted_indices[layer].output_indices =
        MoveMemoryToGPUConst(cl->context, cl->queue,
                             net->inverted_indices[layer].output_indices);
    }

    clFinish(cl->queue);
  }

  // Read the weights and biases (which is the only thing that can change) from
  // GPU back to the Network object. Not thread safe!
  void ReadFromGPU() {
    for (int layer = 0; layer < net->layers.size(); layer++) {
      ReadTo(layers[layer].weights, &net->layers[layer].weights);
      ReadTo(layers[layer].biases, &net->layers[layer].biases);
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

  struct Layer {
    // Const
    cl_mem indices;
    cl_mem weights;
    cl_mem biases;
  };

  struct InvertedIndices {
    // Const
    cl_mem start;
    // Const
    cl_mem length;
    // Const
    cl_mem output_indices;
  };

  std::vector<Layer> layers;
  std::vector<InvertedIndices> inverted_indices;

  CL *cl;
  Network *net;
 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkGPU);
};

// Data on the GPU for a single example in a single training round. Can
// be reused across rounds.
struct TrainingRoundGPU {
  TrainingRoundGPU(CL *cl, const Network &net) : cl(cl), net(&net) {
    for (int i = 0; i < net.num_layers + 1; i++) {
      stimulations.push_back(
          CreateUninitializedGPUMemory<float>(cl->context, net.num_nodes[i]));
    }

    for (int i = 0; i < net.num_layers; i++) {
      errors.push_back(
          CreateUninitializedGPUMemory<float>(cl->context,
                                              net.num_nodes[i + 1]));
    }

    expected =
      CreateUninitializedGPUMemory<float>(cl->context,
                                          net.num_nodes[net.num_layers]);
  }

  void LoadInput(const std::vector<float> &inputs) {
    CHECK_EQ(inputs.size(), net->num_nodes[0]);
    CopyBufferToGPU(cl->queue, inputs, stimulations[0]);
  }

  void LoadExpected(const std::vector<float> &values) {
    CHECK_EQ(values.size(), net->num_nodes[net->num_layers]);
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
  void ExportOutput(std::vector<float> *out) {
    CopyBufferFromGPUTo(cl->queue, stimulations.back(), out);
  }

  // num_nodes + 1 layers. 0th is input, final is the output.
  std::vector<cl_mem> stimulations;
  // num_nodes layers.
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

struct ForwardLayerCL {
  using string = std::string;

  ForwardLayerCL(CL *cl, const Network &net) : cl(cl) {
    string base_src = Util::ReadFile("forwardlayer.cl");
    for (int layer = 0; layer < net.layers.size(); layer++) {
      const TransferFunction transfer_function =
        net.layers[layer].transfer_function;
      const int indices_per_node = net.layers[layer].indices_per_node;
      const int num_features = net.layers[layer].num_features;

      string kernel_name;
      switch (net.layers[layer].type) {
      case LAYER_DENSE:
        kernel_name = "ForwardLayerDense";
        break;
      case LAYER_SPARSE:
        kernel_name = "ForwardLayerSparse";
        break;
      case LAYER_CONVOLUTION_ARRAY:
        kernel_name = "ForwardLayerConvolutional";
        break;
      default:
        CHECK(false) << "Unsupported layer type "
                     << LayerTypeName(net.layers[layer].type);
      }

      string kernel_src =
        Network::TransferFunctionDefines(transfer_function);

      StringAppendF(&kernel_src,
                    "\n"
                    "#define INDICES_PER_NODE %d\n"
                    "#define NUM_FEATURES %d\n",
                    indices_per_node,
                    num_features);

      kernel_src += base_src;
      auto [program, kernel] = cl->BuildOneKernel(kernel_src, kernel_name);
      layer_kernels.emplace_back(program, kernel,
                                 // XXX don't save debugging stuff
                                 indices_per_node, transfer_function);
    }
  }

  struct ForwardContext {
    ForwardContext(ForwardLayerCL *parent, NetworkGPU *net_gpu, int layer) :
      parent(parent), net_gpu(net_gpu), layer(layer) {
      indices = net_gpu->layers[layer].indices;
      weights = net_gpu->layers[layer].weights;
      biases = net_gpu->layers[layer].biases;
    }

    // TODO: Do we really want to share the same command queue across
    // threads? Presumably clFinish can't tell "this thread's
    // commands" apart from others, so we may be prematurely
    // waiting/running other thread's work.
    void Forward(TrainingRoundGPU *train) {
      CHECK_LT(layer + 1, train->stimulations.size());

      CL *cl = parent->cl;

      cl_mem src_values = train->stimulations[layer];
      cl_mem dst_values = train->stimulations[layer + 1];

      Network *net = net_gpu->net;

      auto [program, kernel, kernel_ipn, kernel_tf] =
        parent->layer_kernels[layer];

      // Sanity check we have the right kernel
      CHECK(kernel_ipn == net->layers[layer].indices_per_node);
      CHECK(kernel_tf == net->layers[layer].transfer_function);

      // Can't have multiple threads setting a kernel's argument at one time.
      {
        WriteMutexLock ml(&parent->m);

        // All the kernels take the same args.
        CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                     (void *)&src_values));
        CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                     (void *)&indices));
        CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                     (void *)&weights));
        CHECK_SUCCESS(clSetKernelArg(kernel, 3, sizeof (cl_mem),
                                     (void *)&biases));
        CHECK_SUCCESS(clSetKernelArg(kernel, 4, sizeof (cl_mem),
                                     (void *)&dst_values));


        size_t global_work_offset[] = { 0 };
        size_t global_work_size[] = { (size_t)(net->num_nodes[layer + 1]) };
        // Printf("Run FL Kernel.\n");
        Timer kernel_timer;
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
        kernel_ms += kernel_timer.MS();
      }
    }

    ~ForwardContext() {
    }

    // Not owned.
    cl_mem indices;
    cl_mem weights;
    cl_mem biases;
    ForwardLayerCL *parent = nullptr;
    NetworkGPU *net_gpu = nullptr;
    const int layer;
    double kernel_ms = 0.0;
  };

  ~ForwardLayerCL() {
    for (auto &[p, k, ipn_unused, tf_unused] : layer_kernels) {
      CHECK_SUCCESS(clReleaseKernel(k));
      CHECK_SUCCESS(clReleaseProgram(p));
    }
  }

  CL *cl = nullptr;
  // Owned. Indexed by layer id.
  std::vector<std::tuple<cl_program, cl_kernel, int, TransferFunction>>
    layer_kernels;

  std::shared_mutex m;
};


// Set the error values; this is almost just a memcpy.
struct SetOutputErrorCL {
  using string = std::string;

  SetOutputErrorCL(
      CL *cl, const Network &net,
      const std::optional<std::string> remap_define = std::nullopt) : cl(cl) {
    // This only runs on one layer, the output. But we do need to have the
    // transfer function's derivative.
    string base_src = Util::ReadFile("setoutputerror.cl");

    const TransferFunction transfer_function =
      net.layers.back().transfer_function;

    string kernel_src =
      Network::TransferFunctionDefines(transfer_function);

    // Add remapping function or fill in identity if disabled.
    kernel_src += "\n";
    if (remap_define.has_value()) {
      kernel_src += remap_define.value();
    } else {
      kernel_src += "#define REMAP(i, x) x";
    }
    kernel_src += "\n";

    kernel_src += base_src;

    auto pk = cl->BuildOneKernel(kernel_src, "SetOutputError");
    program = pk.first;
    kernel = pk.second;
  }

  struct Context {
    Context(SetOutputErrorCL *parent, NetworkGPU *net_gpu) :
      parent(parent), net_gpu(net_gpu) {}

    void SetOutputError(TrainingRoundGPU *train) {
      CL *cl = parent->cl;

      const Network *net = net_gpu->net;

      cl_kernel kernel = parent->kernel;

      // All three memories here have num_nodes floats.
      int num_nodes = net->num_nodes[net->num_layers];
      cl_mem actual_outputs = train->stimulations.back();
      cl_mem expected = train->expected;
      cl_mem output_error = train->errors.back();

      // Can't have multiple threads setting a kernel's argument at one time.
      {
        WriteMutexLock ml(&parent->m);

        CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                     (void *)&actual_outputs));
        CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                     (void *)&expected));
        CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                     (void *)&output_error));

        size_t global_work_offset[] = { 0 };
        size_t global_work_size[] = { (size_t)(num_nodes) };

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
    }

   private:
    SetOutputErrorCL *parent = nullptr;
    NetworkGPU *net_gpu = nullptr;
  };

  ~SetOutputErrorCL() {
    clReleaseKernel(kernel);
    clReleaseProgram(program);
  }

 private:
  CL *cl = nullptr;
  // Owned:
  cl_program program;
  cl_kernel kernel;

  std::shared_mutex m;

  DISALLOW_COPY_AND_ASSIGN(SetOutputErrorCL);
};

// Propagate errors backwards. Note that errors flow from "dst" to "src".
struct BackwardLayerCL {
  using string = std::string;

  BackwardLayerCL(CL *cl, const Network &net) : cl(cl) {
    string base_src = Util::ReadFile("backwardlayer.cl");
    for (int src_layer = 0; src_layer < net.layers.size() - 1; src_layer++) {
      int dst_layer = src_layer + 1;
      const TransferFunction transfer_function =
        net.layers[src_layer].transfer_function;
      const int dst_indices_per_node = net.layers[dst_layer].indices_per_node;
      // The dest layer, but num_nodes offset by 1.
      const int dst_num_nodes = net.num_nodes[dst_layer + 1];
      const int dst_num_features = net.layers[dst_layer].num_features;
      const LayerType dst_layer_type = net.layers[dst_layer].type;

      string kernel_src =
        Network::TransferFunctionDefines(transfer_function);

      StringAppendF(&kernel_src,
                    "\n"
                    "#define DST_INDICES_PER_NODE %d\n"
                    "#define DST_NUM_NODES %d\n"
                    "#define DST_NUM_FEATURES %d\n",
                    dst_indices_per_node,
                    dst_num_nodes,
                    dst_num_features);

      kernel_src += base_src;

      string kernel_name;
      switch (dst_layer_type) {
      case LAYER_DENSE:
        kernel_name = "BackwardLayerDense";
        break;
      case LAYER_SPARSE:
        kernel_name = "BackwardLayerSparse";
        break;
      case LAYER_CONVOLUTION_ARRAY:
        kernel_name = "BackwardLayerConvolutional";
        break;
      default:
        CHECK(false) << "Unsupported layer type "
                     << LayerTypeName(dst_layer_type);
      }

      auto [program, kernel] =
        cl->BuildOneKernel(kernel_src, kernel_name);

      layer_kernels.emplace_back(program, kernel,
                                 // XXX don't save debugging stuff
                                 dst_indices_per_node, transfer_function);
    }
  }

  struct Context {
    Context(BackwardLayerCL *parent, NetworkGPU *net_gpu, int dst_layer) :
      parent(parent), net_gpu(net_gpu), dst_layer(dst_layer) {

      const int gap = dst_layer;
      // const int src_layer = dst_layer - 1;

      starts = net_gpu->inverted_indices[gap].start;
      lengths = net_gpu->inverted_indices[gap].length;
      inverted_index = net_gpu->inverted_indices[gap].output_indices;
      dst_weights = net_gpu->layers[dst_layer].weights;
    }

    void Backward(TrainingRoundGPU *train) {
      CL *cl = parent->cl;
      const Network *net = net_gpu->net;
      const int gap = dst_layer;
      const int src_layer = dst_layer - 1;

      auto [program_, kernel, dst_ipn, tf] =
        parent->layer_kernels[src_layer];
      // Sanity check that this was compiled with the right ipn / tf.
      CHECK(dst_ipn == net->layers[dst_layer].indices_per_node);
      CHECK(tf == net->layers[src_layer].transfer_function);

      cl_mem src_output = train->stimulations[src_layer + 1];
      cl_mem dst_error = train->errors[dst_layer];

      // This is the source layer, but num_nodes is offset by one
      // since it includes the size of the input layer as element 0.
      int src_num_nodes = net->num_nodes[src_layer + 1];
      cl_mem src_error = train->errors[src_layer];

      CHECK_EQ(src_num_nodes, net->inverted_indices[gap].start.size());

      // Can't have multiple threads setting a kernel's argument at one time.
      {
        WriteMutexLock ml(&parent->m);

        CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                     (void *)&starts));
        CHECK_SUCCESS(clSetKernelArg(kernel, 1, sizeof (cl_mem),
                                     (void *)&lengths));
        CHECK_SUCCESS(clSetKernelArg(kernel, 2, sizeof (cl_mem),
                                     (void *)&inverted_index));
        CHECK_SUCCESS(clSetKernelArg(kernel, 3, sizeof (cl_mem),
                                     (void *)&dst_weights));
        CHECK_SUCCESS(clSetKernelArg(kernel, 4, sizeof (cl_mem),
                                     (void *)&src_output));
        CHECK_SUCCESS(clSetKernelArg(kernel, 5, sizeof (cl_mem),
                                     (void *)&dst_error));
        CHECK_SUCCESS(clSetKernelArg(kernel, 6, sizeof (cl_mem),
                                     (void *)&src_error));

        size_t global_work_offset[] = { 0 };
        size_t global_work_size[] = { (size_t)src_num_nodes };
        Timer kernel_timer;
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
        kernel_ms += kernel_timer.MS();
      }
    }

    ~Context() {}

    cl_mem starts, lengths, inverted_index, dst_weights;
    BackwardLayerCL *parent = nullptr;
    NetworkGPU *net_gpu = nullptr;
    const int dst_layer;
    double kernel_ms = 0.0;
  };

  ~BackwardLayerCL() {
    for (auto &[p, k, deleteme1, deleteme2] : layer_kernels) {
      CHECK_SUCCESS(clReleaseKernel(k));
      CHECK_SUCCESS(clReleaseProgram(p));
    }
  }

  CL *cl = nullptr;
  // Indexed by source layer index.
  // Note: Unlike others, these have the layer's parameters
  // baked in.
  std::vector<std::tuple<cl_program, cl_kernel, int, TransferFunction>>
    layer_kernels;

  std::shared_mutex m;
};

// Optional and unprincipled L2-like regularization.
// Decays every weight by a constant multiplicative factor.
struct DecayWeightsCL {
  using string = std::string;

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
