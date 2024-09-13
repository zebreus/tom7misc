
#ifndef _LLM_MODELS_H
#define _LLM_MODELS_H

#include <string_view>

#include "llm.h"

struct Models {

  static constexpr ContextParams LLAMA_70B_Q8 = {
    .model = "c:\\code\\sf_svn\\llm\\llama2\\70b\\ggml-model-Q8_0.gguf",
    .context_size = 4096,
    // Aggressive would be 23.
    .num_gpu_layers = 21,
    .num_threads = 30,
    .num_threads_batch = 22,
  };

  static constexpr ContextParams LLAMA_70B_F16 = {
    .model = "c:\\code\\sf_svn\\llm\\llama2\\70b\\ggml-model-f16.gguf",
    .context_size = 4096,
    // Aggressive would be 12.
    .num_gpu_layers = 11,
    .num_threads = 30,
    .num_threads_batch = 22,
  };

  static constexpr ContextParams LLAMA_7B_F16 = {
    .model = "c:\\code\\sf_svn\\llm\\llama2\\7b\\ggml-model-f16.gguf",
    .context_size = 4096,
    // At this point the whole model fits on the GPU.
    // When this is the case, we get better performance
    // with a lower number of threads.
    .num_gpu_layers = 32,
    .num_threads = 11,
    .num_threads_batch = 14,
  };

  static constexpr ContextParams LLAMA_7B_Q8 = {
    .model = "c:\\code\\sf_svn\\llm\\llama2\\7b\\ggml-model-Q8_0.gguf",
    .context_size = 4096,
    .num_gpu_layers = 32,
    .num_threads = 4,
    .num_threads_batch = 6,
  };

  static constexpr ContextParams LLAMA_7B_Q4 = {
    .model = "c:\\code\\sf_svn\\llm\\llama2\\7b\\ggml-model-Q4_0.gguf",
    .context_size = 4096,
    .num_gpu_layers = 32,
    .num_threads = 2,
    .num_threads_batch = 7,
  };

  static constexpr ContextParams LLAMA_7B_Q2 = {
    .model = "c:\\code\\sf_svn\\llm\\llama2\\7b\\ggml-model-Q2_K.gguf",
    .context_size = 4096,
    .num_gpu_layers = 32,
    .num_threads = 4,
    .num_threads_batch = 6,
  };

  // TODO: llama v1 models, codellama

  static constexpr ContextParams LLAMA3_8B_F16 = {
    .model = "f:\\llama3\\8b\\ggml-model-f16.gguf",
    .context_size = 8192,
    .num_gpu_layers = 32,
    .num_threads = 16,
    .num_threads_batch = 12,
  };

  static constexpr ContextParams LLAMA3_8B_Q8 = {
    .model = "f:\\llama3\\8b\\ggml-model-Q8_0.gguf",
    .context_size = 8192,
    .num_gpu_layers = 33,
    .num_threads = 4,
    .num_threads_batch = 6,
  };

  static constexpr ContextParams LLAMA3_70B_Q5_K_M = {
    .model = "f:\\llama3\\70b\\ggml-model-Q5_K_M.gguf",
    .context_size = 8192,
    // For single-token inference, 37 or 38 layers seems
    // to be best (and 32 cores!). But for batch inference,
    // it's 10x slower if we exceed 35 layers.
    .num_gpu_layers = 34,
    .num_threads = 28,
    .num_threads_batch = 20,
  };

  // TODO: Not tuned! Just copied from llama2.
  static constexpr ContextParams LLAMA3_70B_F16 = {
    .model = "f:\\llama3\\70b\\ggml-model-f16.gguf",
    .context_size = 8192,
    // Aggressive would be 12.
    .num_gpu_layers = 10,
    .num_threads = 30,
    .num_threads_batch = 22,
  };


};

#endif
