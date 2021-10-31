
#include "network-gpu.h"

#include <string>
#include <optional>
#include <vector>
#include <mutex>
#include <cmath>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "util.h"
#include "network.h"
#include "timer.h"
#include "clutil.h"
#include "threadutil.h"

using namespace std;


NetworkGPU::NetworkGPU(CL *cl, Network *net) : cl(cl), net(net) {
  layers.resize(net->layers.size());
  for (int layer = 0; layer < net->layers.size(); layer++) {
    const Layer &cpu_layer = net->layers[layer];
    GPULayer *gpu_layer = &layers[layer];

    auto CopyMemoryAllowEmpty = [&](auto vec, bool readonly) -> cl_mem {
        if (vec.empty()) return 0;
        else return CopyMemoryToGPU(cl->context, cl->queue, vec, readonly);
      };

    gpu_layer->chunks.resize(cpu_layer.chunks.size());
    for (int chunk = 0; chunk < cpu_layer.chunks.size(); chunk++) {
      CHECK(chunk < cpu_layer.chunks.size());
      CHECK(chunk < gpu_layer->chunks.size());
      const Chunk &cpu_chunk = cpu_layer.chunks[chunk];
      GPUChunk *gpu_chunk = &gpu_layer->chunks[chunk];

      // Normal for this to be empty for dense chunks.
      gpu_chunk->indices = CopyMemoryAllowEmpty(cpu_chunk.indices, true);
      // These two should only be empty for the (token) input layer,
      // which we will never use, but we still build the gpu copies
      // for uniformity.
      gpu_chunk->weights = CopyMemoryAllowEmpty(cpu_chunk.weights, false);
      gpu_chunk->biases = CopyMemoryAllowEmpty(cpu_chunk.biases, false);

      // Empty when weight_update is SGD.
      gpu_chunk->weights_aux =
        CopyMemoryAllowEmpty(cpu_chunk.weights_aux, false);
      gpu_chunk->biases_aux =
        CopyMemoryAllowEmpty(cpu_chunk.biases_aux, false);

      InvertedIndices inverted = net->ComputeInvertedIndices(layer, chunk);
      gpu_chunk->ii_start = CopyMemoryAllowEmpty(inverted.start, true);
      gpu_chunk->ii_length = CopyMemoryAllowEmpty(inverted.length, true);
      gpu_chunk->ii_indices =
        CopyMemoryAllowEmpty(inverted.output_indices, true);
    }
  }

  clFinish(cl->queue);
}

NetworkGPU::~NetworkGPU() {
  for (GPULayer &gpu_layer : layers) {
    for (GPUChunk &gpu_chunk : gpu_layer.chunks) {
      if (gpu_chunk.indices != 0)
        CHECK_SUCCESS(clReleaseMemObject(gpu_chunk.indices));
      if (gpu_chunk.weights != 0)
        CHECK_SUCCESS(clReleaseMemObject(gpu_chunk.weights));
      if (gpu_chunk.biases != 0)
        CHECK_SUCCESS(clReleaseMemObject(gpu_chunk.biases));
      if (gpu_chunk.weights_aux != 0)
        CHECK_SUCCESS(clReleaseMemObject(gpu_chunk.weights_aux));
      if (gpu_chunk.biases_aux != 0)
        CHECK_SUCCESS(clReleaseMemObject(gpu_chunk.biases_aux));
      if (gpu_chunk.ii_start != 0)
        CHECK_SUCCESS(clReleaseMemObject(gpu_chunk.ii_start));
      if (gpu_chunk.ii_length != 0)
        CHECK_SUCCESS(clReleaseMemObject(gpu_chunk.ii_length));
      if (gpu_chunk.ii_indices != 0)
        CHECK_SUCCESS(clReleaseMemObject(gpu_chunk.ii_indices));
    }
  }
}


void NetworkGPU::ReadFromGPU() {
  CHECK(net->layers.size() == layers.size());
  for (int layer = 0; layer < net->layers.size(); layer++) {
    Layer *cpu_layer = &net->layers[layer];
    GPULayer *gpu_layer = &layers[layer];
    CHECK(cpu_layer->chunks.size() == gpu_layer->chunks.size());
    for (int chunk = 0; chunk < cpu_layer->chunks.size(); chunk++) {
      Chunk *cpu_chunk = &cpu_layer->chunks[chunk];
      GPUChunk *gpu_chunk = &gpu_layer->chunks[chunk];
      ReadToZeroOk(gpu_chunk->weights, &cpu_chunk->weights);
      ReadToZeroOk(gpu_chunk->biases, &cpu_chunk->biases);
      ReadToZeroOk(gpu_chunk->weights_aux, &cpu_chunk->weights_aux);
      ReadToZeroOk(gpu_chunk->biases_aux, &cpu_chunk->biases_aux);
    }
  }
  clFinish(cl->queue);
}


static string ForwardKernelName(ChunkType ct) {
  switch (ct) {
  case CHUNK_DENSE: return "ForwardChunkDense";
  case CHUNK_SPARSE: return "ForwardChunkSparse";
  case CHUNK_CONVOLUTION_ARRAY: return "ForwardChunkConvolutional";
  case CHUNK_INPUT:
    LOG(FATAL) << "Can't run INPUT chunks forward.";
  default:
    CHECK(false) << "Unsupported chunk type "
                 << ChunkTypeName(ct);
  }
  return "";
}


ForwardLayerCL::ForwardLayerCL(CL *cl, const Network &net) : cl(cl) {
  // Compile the appropriate kernel with baked in constants for
  // each chunk in the network.
  string base_src = Util::ReadFile("forwardchunk.cl");
  for (int layer = 0; layer < net.layers.size(); layer++) {
    if (layer == 0) {
      // No forward kernels for input layer.
      CHECK(net.layers[layer].chunks.size() == 1);
      layer_kernels.push_back({ChunkKernel()});
      continue;
    }

    std::vector<ChunkKernel> chunk_kernels;
    for (const Chunk &chunk : net.layers[layer].chunks) {
      string kernel_src =
        Network::TransferFunctionDefines(chunk.transfer_function);

      StringAppendF(&kernel_src,
                    "\n"
                    "#define INDICES_PER_NODE %d\n"
                    "#define NUM_FEATURES %d\n"
                    "#define SPAN_START %d\n"
                    "#define SPAN_SIZE %d\n",
                    chunk.indices_per_node,
                    chunk.num_features,
                    chunk.span_start,
                    chunk.span_size);

      const string kernel_name = ForwardKernelName(chunk.type);
      kernel_src += base_src;
      auto [program, kernel] = cl->BuildOneKernel(kernel_src, kernel_name);
      CHECK(program != 0 && kernel != 0);

      ChunkKernel ck;
      ck.program = program;
      ck.kernel = kernel;
      chunk_kernels.push_back(ck);
    }
    layer_kernels.push_back(std::move(chunk_kernels));
  }
}

void ForwardLayerCL::RunForward(
    NetworkGPU *net_gpu, TrainingRoundGPU *train, int src_layer) {
  const int dst_layer = src_layer + 1;

  // TODO: Do we really want to share the same command queue across
  // threads? Presumably clFinish can't tell "this thread's
  // commands" apart from others, so we may be prematurely
  // waiting/running other thread's work.

  CHECK_LT(dst_layer, train->stimulations.size());

  cl_mem src_values = train->stimulations[src_layer];
  cl_mem dst_values = train->stimulations[dst_layer];
  CHECK(src_values != 0);
  CHECK(dst_values != 0);

  // TODO: could keep net_gpu and net as members?
  const Network &net = *net_gpu->net;

  // We do the chunks in serial.
  // (This is where it would be nice to be running many training
  // examples at once.)
  const Layer &layer = net.layers[dst_layer];
  int out_idx = 0;
  for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
    const Chunk &chunk = layer.chunks[chunk_idx];
    CHECK(dst_layer < layer_kernels.size());
    CHECK(chunk_idx < layer_kernels[dst_layer].size());
    const ChunkKernel &ck = layer_kernels[dst_layer][chunk_idx];

    CHECK(chunk_idx < net_gpu->layers[dst_layer].chunks.size());
    NetworkGPU::GPUChunk &gpu_chunk =
      net_gpu->layers[dst_layer].chunks[chunk_idx];

    cl_mem indices = gpu_chunk.indices;
    CHECK(chunk.type == CHUNK_DENSE || indices != 0);
    cl_mem weights = gpu_chunk.weights;
    CHECK(weights != 0);
    cl_mem biases = gpu_chunk.biases;
    CHECK(biases != 0);

    // PERF: This is a good approach (saves constant addition in kernel)
    // with one training example, but I probably want to pass the full
    // layer and offset in the kernel (can be compile-time constant)
    // in order to enable parallelism over training examples.
    cl_mem dst_sub_values = SliceGPUMemory<float>(dst_values,
                                                  out_idx,
                                                  chunk.num_nodes);

    // Can't have multiple threads setting a kernel's argument at one time.
    {
      MutexLock ml(&m);

      // All the kernels take the same args.
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 0, sizeof (cl_mem),
                                   (void *)&src_values));
      // (Note that indices can be 0, an invalid cl_mem, which
      // we use to represent an empty memory. The memory will not
      // be accessed in this case (dense kernel). Not totally sure
      // that it is defined behavior to pass 0.)
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 1, sizeof (cl_mem),
                                   (void *)&indices));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 2, sizeof (cl_mem),
                                   (void *)&weights));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 3, sizeof (cl_mem),
                                   (void *)&biases));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 4, sizeof (cl_mem),
                                   (void *)&dst_sub_values));

      size_t global_work_offset[] = { 0 };
      size_t global_work_size[] = { (size_t)(chunk.num_nodes) };

      CHECK(ck.kernel != 0);
      CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, ck.kernel,
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

    // Release refcount to sub-buffer.
    CHECK_SUCCESS(clReleaseMemObject(dst_sub_values));

    out_idx += chunk.num_nodes;
  }
  CHECK(out_idx == layer.num_nodes);
}

ForwardLayerCL::~ForwardLayerCL() {
  for (std::vector<ChunkKernel> &ckv : layer_kernels) {
    for (ChunkKernel &ck : ckv) {
      // These will be 0 for the input layer.
      if (ck.kernel != 0) CHECK_SUCCESS(clReleaseKernel(ck.kernel));
      if (ck.program != 0) CHECK_SUCCESS(clReleaseProgram(ck.program));
    }
  }
  layer_kernels.clear();
}



SetOutputErrorCL::SetOutputErrorCL(
    CL *cl, const Network &net,
    const std::optional<std::string> remap_define) : cl(cl) {
  // This only runs on one layer, the output. But we do need to have the
  // transfer function's derivative.
  string base_src = Util::ReadFile("setoutputerror.cl");

  const Layer &layer = net.layers.back();
  for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
    const Chunk &chunk = layer.chunks[chunk_idx];
    const TransferFunction transfer_function =
      chunk.transfer_function;

    string kernel_src =
      Network::TransferFunctionDefines(transfer_function);

    // Add remapping function or fill in identity if disabled.
    kernel_src += "\n";
    if (remap_define.has_value()) {
      kernel_src += remap_define.value();
    } else {
      kernel_src += "#define REMAP(c, i, x) x";
    }
    kernel_src += "\n";

    StringAppendF(&kernel_src, "\n#define CHUNK_IDX %d\n", chunk_idx);

    kernel_src += base_src;

    auto pk = cl->BuildOneKernel(kernel_src, "SetOutputError");
    kernels.push_back(ChunkKernel{.program = pk.first,
                                  .kernel = pk.second});
  }
}

void SetOutputErrorCL::SetOutputError(
    NetworkGPU *net_gpu, TrainingRoundGPU *train) {
  // TODO: Could keep alias to this?
  const Network *net = net_gpu->net;

  // Full buffers from which we create sub-buffers below.
  cl_mem actual_outputs = train->stimulations.back();
  cl_mem expected = train->expected;
  cl_mem output_error = train->errors.back();

  int out_idx = 0;
  const Layer &layer = net->layers.back();
  for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
    CHECK(chunk_idx < kernels.size());
    const Chunk &chunk = layer.chunks[chunk_idx];
    const ChunkKernel &ck = kernels[chunk_idx];

    cl_mem sub_actual_outputs =
      SliceGPUMemory<float>(actual_outputs, out_idx, chunk.num_nodes);
    cl_mem sub_expected =
      SliceGPUMemory<float>(expected, out_idx, chunk.num_nodes);
    cl_mem sub_output_error =
      SliceGPUMemory<float>(output_error, out_idx, chunk.num_nodes);

    {
      MutexLock ml(&m);

      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 0, sizeof (cl_mem),
                                   (void *)&sub_actual_outputs));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 1, sizeof (cl_mem),
                                   (void *)&sub_expected));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 2, sizeof (cl_mem),
                                   (void *)&sub_output_error));

      size_t global_work_offset[] = { 0 };
      size_t global_work_size[] = { (size_t)chunk.num_nodes };

      CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, ck.kernel,
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

    CHECK_SUCCESS(clReleaseMemObject(sub_actual_outputs));
    CHECK_SUCCESS(clReleaseMemObject(sub_expected));
    CHECK_SUCCESS(clReleaseMemObject(sub_output_error));

    out_idx += chunk.num_nodes;
  }
  CHECK(out_idx == layer.num_nodes);
}


static std::string Backward1KernelName(ChunkType ct) {
  switch (ct) {
  case CHUNK_DENSE: return "BackwardChunkDense";
  case CHUNK_SPARSE: return "BackwardChunkSparse";
  case CHUNK_CONVOLUTION_ARRAY: return "BackwardChunkConvolutional";
  default:
    CHECK(false) << "Unsupported chunk type for BackwardLayer1 "
                 << ChunkTypeName(ct);
  }
  return "ERROR";
}

BackwardLayerCL::BackwardLayerCL(CL *cl, const Network &net) : cl(cl) {
  string base1_src = Util::ReadFile("backwardchunk.cl");

  // Dummy kernels for input layer, which can't be a destination layer.
  CHECK(net.layers[0].chunks.size() == 1);
  layer_kernels.push_back(std::vector<ChunkKernel>{ChunkKernel()});

  for (int dst_layer_idx = 1;
       dst_layer_idx < net.layers.size();
       dst_layer_idx++) {
    const Layer &dst_layer = net.layers[dst_layer_idx];

    // First pass is chunk-by-chunk in the destination layer.
    std::vector<ChunkKernel> chunk_kernels;
    int out_idx = 0;
    for (int chunk_idx = 0; chunk_idx < dst_layer.chunks.size(); chunk_idx++) {
      const Chunk &chunk = dst_layer.chunks[chunk_idx];
      string kernel_src =
        StringPrintf("#define CHUNK_START %d\n"
                     "#define SPAN_START %d\n"
                     "#define SPAN_SIZE %d\n"
                     "#define DST_INDICES_PER_NODE %d\n"
                     "#define DST_NUM_NODES %d\n"
                     "#define DST_NUM_FEATURES %d\n"
                     "#define SRC_SPAN_IS_ZERO %d\n",
                     out_idx,
                     chunk.span_start,
                     chunk.span_size,
                     chunk.indices_per_node,
                     chunk.num_nodes,
                     chunk.num_features,
                     // PERF: set this to true for first chunk,
                     // or (better) for all chunks writing into
                     // a span that hasn't yet been written by
                     // a previous chunk
                     false);

      kernel_src += base1_src;

      auto [program, kernel] =
        cl->BuildOneKernel(kernel_src, Backward1KernelName(chunk.type));

      ChunkKernel ck{
        .program1 = program,
        .kernel1 = kernel
        // program2, kernel2 filled in below.
      };
      chunk_kernels.push_back(ck);

      out_idx += chunk.num_nodes;
    }
    CHECK(out_idx == dst_layer.num_nodes);
    layer_kernels.push_back(std::move(chunk_kernels));
  }

  // And the second pass. This is associated with the source layer.
  string base2_src = Util::ReadFile("backwardsecondpass.cl");

  // We don't actually need this for the first layer (as we don't
  // propagate error to the input normally) nor the last (it
  // cannot be a source layer) but we generate it for each chunk
  // nonetheless, for uniformity.
  for (int layer_idx = 0;
       layer_idx < net.layers.size();
       layer_idx++) {
    const Layer &layer = net.layers[layer_idx];
    // We modify these in place so we expect them to have already been
    // created above.
    CHECK(layer_idx < layer_kernels.size());

    int out_idx = 0;
    for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
      const Chunk &chunk = layer.chunks[chunk_idx];
      CHECK(chunk_idx < layer_kernels[layer_idx].size());

      string kernel_src =
        Network::TransferFunctionDefines(chunk.transfer_function);

      StringAppendF(&kernel_src,
                    "#define CHUNK_START %d\n"
                    "#define CLIP_ERROR %s\n"
                    "#define LARGE_ERROR %0.8ff\n",
                    out_idx,
                    CLIP_ERROR ? "true" : "false",
                    LARGE_ERROR);

      kernel_src += base2_src;

      // One kernel for all chunk types (but it does depend on
      // the chunk's transfer function).
      auto [program, kernel] =
        cl->BuildOneKernel(kernel_src, "BackwardSecondPass");
      layer_kernels[layer_idx][chunk_idx].program2 = program;
      layer_kernels[layer_idx][chunk_idx].kernel2 = kernel;

      out_idx += chunk.num_nodes;
    }
    CHECK(out_idx == layer.num_nodes);
  }
}

BackwardLayerCL::~BackwardLayerCL() {
  for (auto &v : layer_kernels) {
    for (auto &ck : v) {
      if (ck.kernel1 != 0) CHECK_SUCCESS(clReleaseKernel(ck.kernel1));
      if (ck.program1 != 0) CHECK_SUCCESS(clReleaseProgram(ck.program1));
      if (ck.kernel2 != 0) CHECK_SUCCESS(clReleaseKernel(ck.kernel2));
      if (ck.program2 != 0) CHECK_SUCCESS(clReleaseProgram(ck.program2));
    }
  }
}

void BackwardLayerCL::BackwardLayer(NetworkGPU *net_gpu,
                                    TrainingRoundGPU *train,
                                    int dst_layer) {
  const Network &net = *net_gpu->net;
  CHECK(dst_layer > 0);
  CHECK(dst_layer < net.layers.size());

  const int src_layer = dst_layer - 1;

  // Full destination error.
  cl_mem dst_error = train->errors[dst_layer];
  // Full source error.
  cl_mem src_error = train->errors[dst_layer - 1];

  // In the general case we are accumulating the weighted error sum
  // (+=) rather than writing it once (=), so unlike the other
  // kernels we need to start by clearing the src_error.
  // PERF we could consider doing this only for chunks where we
  // have SRC_SPAN_IS_ZERO false. This would probably interfere
  // with running on multiple training rounds at once. It probably
  // works to skip when ALL the chunks are SRC_SPAN_IS_ZERO, which
  // would happen in the common case that there is just one chunk.
  cl_float zero = 0.0f;
  CHECK_SUCCESS(
      clEnqueueFillBuffer(cl->queue,
                          src_error,
                          // pattern and its size in bytes
                          &zero, sizeof (cl_float),
                          // offset and size to fill (in BYTES)
                          0, (size_t)(net.layers[src_layer].num_nodes *
                                      sizeof (cl_float)),
                          // no wait list or event
                          0, nullptr, nullptr));
  // This needs to be done before the kernel runs below. PERF that
  // if we are running many examples in parallel, we'll be synchronizing
  // on each other's writes, and possibly starving the kernel below.
  // This would be a good place to use wait lists!
  clFinish(cl->queue);

  const Layer &layer = net.layers[dst_layer];
  for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
    const Chunk &chunk = layer.chunks[chunk_idx];
    NetworkGPU::GPUChunk &gpu_chunk =
      net_gpu->layers[dst_layer].chunks[chunk_idx];

    cl_mem ii_start = gpu_chunk.ii_start;
    cl_mem ii_length = gpu_chunk.ii_length;
    cl_mem ii_indices = gpu_chunk.ii_indices;
    cl_mem dst_weights = gpu_chunk.weights;

    CHECK(dst_layer < layer_kernels.size() &&
          chunk_idx < layer_kernels[dst_layer].size());
    const ChunkKernel &ck = layer_kernels[dst_layer][chunk_idx];

    {
      MutexLock ml(&m);

      CHECK_SUCCESS(clSetKernelArg(ck.kernel1, 0, sizeof (cl_mem),
                                   (void *)&ii_start));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel1, 1, sizeof (cl_mem),
                                   (void *)&ii_length));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel1, 2, sizeof (cl_mem),
                                   (void *)&ii_indices));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel1, 3, sizeof (cl_mem),
                                   (void *)&dst_weights));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel1, 4, sizeof (cl_mem),
                                   (void *)&dst_error));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel1, 5, sizeof (cl_mem),
                                   (void *)&src_error));

      size_t global_work_offset[] = { 0 };
      size_t global_work_size[] = { (size_t)chunk.span_size };
      Timer kernel_timer;
      CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, ck.kernel1,
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
}

DecayWeightsCL::DecayWeightsCL(CL *cl, const Network &net,
                               float decay_factor) : cl(cl) {
  string base_src = Util::ReadFile("decayweights.cl");

  string kernel_src;
  StringAppendF(&kernel_src, "\n#define DECAY_FACTOR %.9ff\n",
                decay_factor);
  kernel_src += base_src;
  auto p = cl->BuildOneKernel(kernel_src, "DecayWeights");
  program = p.first;
  kernel = p.second;
}

DecayWeightsCL::~DecayWeightsCL() {
  CHECK_SUCCESS(clReleaseKernel(kernel));
  CHECK_SUCCESS(clReleaseProgram(program));
}

void DecayWeightsCL::Decay(NetworkGPU *net_gpu, int layer_idx) {
  const Network &net = *net_gpu->net;
  // PERF: Should actually be able to run in parallel across the entire
  // network if we weren't sharing a single kernel. Every weight
  // just scaled independently.
  CHECK(layer_idx >= 0 && layer_idx < net.layers.size());
  for (int chunk_idx = 0;
       chunk_idx < net.layers[layer_idx].chunks.size();
       chunk_idx++) {
    const Chunk &cpu_chunk = net.layers[layer_idx].chunks[chunk_idx];
    if (cpu_chunk.fixed) continue;

    NetworkGPU::GPUChunk &gpu_chunk =
      net_gpu->layers[layer_idx].chunks[chunk_idx];
    const int num_weights =
      net.layers[layer_idx].chunks[chunk_idx].weights.size();

    {
      MutexLock ml(&m);
      CHECK_SUCCESS(clSetKernelArg(kernel, 0, sizeof (cl_mem),
                                   (void *)&gpu_chunk.weights));

      size_t global_work_offset[] = { 0 };
      // Total number of weights in this chunk.
      size_t global_work_size[] = { (size_t)num_weights };
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
}

static string UpdateWeightsKernelName(ChunkType ct) {
  switch (ct) {
    case CHUNK_DENSE: return "UpdateWeightsDense";
    case CHUNK_SPARSE: return "UpdateWeightsSparse";
    case CHUNK_CONVOLUTION_ARRAY: return "UpdateWeightsConvolutional";
    case CHUNK_INPUT:
      CHECK(false) << "Can't update weights for input layer.";
      return "";
    default:
      CHECK(false) << "Unsupported chunk type "
                   << ChunkTypeName(ct);
      return "";
  }
}


UpdateWeightsCL::UpdateWeightsCL(CL *cl, const Network &net) : cl(cl) {
  // Note that this one doesn't depend on the transfer function/derivative.

  // Also unlike others, we actually invoke the kernel differently in
  // the convolution case.

  const string base_src = Util::ReadFile("updateweights.cl");

  // Not possible on the input layer, but we have placeholder
  // ChunkKernels for parallelism with other layer arrays.
  CHECK(net.layers[0].chunks.size() == 1);
  layer_kernels.push_back(std::vector<ChunkKernel>{ChunkKernel()});

  for (int layer_idx = 1; layer_idx < net.layers.size(); layer_idx++) {

    std::vector<ChunkKernel> chunk_kernels;
    int out_idx = 0;
    for (int chunk_idx = 0;
         chunk_idx < net.layers[layer_idx].chunks.size();
         chunk_idx++) {
      const Chunk &chunk = net.layers[layer_idx].chunks[chunk_idx];

      const int num_occurrences =
        chunk.num_occurrences_across *
        chunk.num_occurrences_down;

      // PERF: Don't even compile a kernel if the chunk is fixed.

      string kernel_src;
      switch (chunk.weight_update) {
      case SGD:
        kernel_src += "#define WEIGHT_UPDATE_SGD 1\n";
        break;
      case ADAM:
        kernel_src += "#define WEIGHT_UPDATE_ADAM 1\n";
        break;
      default:
        LOG(FATAL) << "Unsupported weight update type " <<
          WeightUpdateName(chunk.weight_update);
        break;
      }

      StringAppendF(&kernel_src,
                    // TODO: make configurable, but
                    // find a better way to specify this tri-state
                    // (noclip, clip, constrain)
                    "#define NOCLIP false\n"
                    "#define CONSTRAIN true\n"
                    "#define CONSTRAIN_WEIGHT_MAX 16.0f\n"
                    "#define CONSTRAIN_BIAS_MAX 16384.0f\n"

                    "#define CHUNK_START %d\n"
                    "#define SPAN_START %d\n"
                    "#define SPAN_SIZE %d\n"
                    "#define INDICES_PER_NODE %d\n"
                    "#define NUM_OCCURRENCES %d\n"
                    "#define NUM_FEATURES %d\n",
                    out_idx,
                    chunk.span_start,
                    chunk.span_size,
                    chunk.indices_per_node,
                    num_occurrences,
                    chunk.num_features);

      const string kernel_name = UpdateWeightsKernelName(chunk.type);

      kernel_src += base_src;
      auto pk = cl->BuildOneKernel(kernel_src, kernel_name);

      chunk_kernels.push_back(ChunkKernel{.program = pk.first,
                                          .kernel = pk.second});

      out_idx += chunk.num_nodes;
    }
    CHECK(out_idx == net.layers[layer_idx].num_nodes);

    layer_kernels.push_back(std::move(chunk_kernels));
  }
}

UpdateWeightsCL::~UpdateWeightsCL() {
  for (auto &vec : layer_kernels) {
    for (auto &ck : vec) {
      if (ck.kernel != 0) CHECK_SUCCESS(clReleaseKernel(ck.kernel));
      if (ck.program != 0) CHECK_SUCCESS(clReleaseProgram(ck.program));
    }
  }
}

void UpdateWeightsCL::Update(NetworkGPU *net_gpu, TrainingRoundGPU *train,
                             float learning_rate, int layer_idx) {
  // new chunks
  CHECK(layer_idx > 0) << "Can't update the weights for the input layer, "
    "which would not be useful anyway since there aren't any.";

  // the errors for the layer we're updating
  cl_mem layer_error = train->errors[layer_idx];
  // the output values for the previous layer.
  cl_mem prev_layer_output = train->stimulations[layer_idx - 1];

  const Network &net = *net_gpu->net;

  const Layer &layer = net.layers[layer_idx];
  NetworkGPU::GPULayer &gpu_layer = net_gpu->layers[layer_idx];

  // XXX overflow is possible here.
  // PERF: After the round is suitably high, we should be ignoring this.
  const cl_int round_number = net.rounds;

  for (int chunk_idx = 0; chunk_idx < layer.chunks.size(); chunk_idx++) {
    const Chunk &chunk = layer.chunks[chunk_idx];
    // For fixed chunks, just skip the update step.
    if (chunk.fixed) continue;

    NetworkGPU::GPUChunk &gpu_chunk = gpu_layer.chunks[chunk_idx];
    ChunkKernel &ck = layer_kernels[layer_idx][chunk_idx];

    // For convolution layers, scale the learning rate down,
    // as there will be lots of occurrences. Scaling linearly
    // is probably the most principled (each occurrence yields an
    // error and thus an update), but sqrt seems to work better
    // (the errors often cancel each other out).
    // (Should perhaps reconsider this in ADAM mode?)
    const int num_occurrences = chunk.num_occurrences_across *
      chunk.num_occurrences_down;
    const float effective_learning_rate =
      (chunk.type == CHUNK_CONVOLUTION_ARRAY) ?
      learning_rate * (1.0 / sqrt(num_occurrences)) :
      learning_rate;

    // also tried...
    // pow((float)num_occurrences, 0.25f))

    {
      // PERF: Could run the chunks in parallel. But we can't run training
      // examples in parallel because of concurrent writes to the network's
      // weights.
      MutexLock ml(&m);


      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 0, sizeof (cl_int),
                                   (void *)&round_number));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 1, sizeof (cl_float),
                                   (void *)&effective_learning_rate));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 2, sizeof (cl_mem),
                                   (void *)&prev_layer_output));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 3, sizeof (cl_mem),
                                   (void *)&layer_error));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 4, sizeof (cl_mem),
                                   (void *)&gpu_chunk.indices));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 5, sizeof (cl_mem),
                                   (void *)&gpu_chunk.weights));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 6, sizeof (cl_mem),
                                   (void *)&gpu_chunk.biases));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 7, sizeof (cl_mem),
                                   (void *)&gpu_chunk.weights_aux));
      CHECK_SUCCESS(clSetKernelArg(ck.kernel, 8, sizeof (cl_mem),
                                   (void *)&gpu_chunk.biases_aux));

      // Arguments are the same for the different chunk types,
      // but for convolution array it's actually a 2D kernel
      // over features and weights.

      if (chunk.type == CHUNK_CONVOLUTION_ARRAY) {
        size_t global_work_offset[] = { 0, 0 };
        size_t global_work_size[] = { (size_t)chunk.num_features,
                                      (size_t)chunk.indices_per_node };
        CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, ck.kernel,
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
        size_t global_work_size[] = { (size_t)chunk.num_nodes };
        CHECK_SUCCESS(clEnqueueNDRangeKernel(cl->queue, ck.kernel,
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
  }
}
