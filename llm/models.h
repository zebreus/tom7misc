
#include "llm.h"

struct Models {

  static constexpr ContextParams LLAMA_7B_F16 = {
    .model = "c:\\code\\sf_svn\\llm\\llama2\\7b\\ggml-model-f16.gguf",
    .context_size = 4096,
    // TODO: tune!
    .num_gpu_layers = 0,
    .num_threads = 24,
    .num_threads_batch = 24,
  };

  static constexpr ContextParams LLAMA_70B_Q8 = {
    .model = "llama2\\70b\\ggml-model-q8_0.gguf",
    // Aggressive would be 23.
    .num_gpu_layers = 21,
    .num_threads = 30,
    .num_threads_batch = 22,
  };

  static constexpr ContextParams LLAMA_70B_F16 = {
    .model = "llama2\\70b\\ggml-model-f16.gguf",
    // Aggressive would be 12.
    .num_gpu_layers = 11,
    .num_threads = 30,
    .num_threads_batch = 22,
  };

  static constexpr ContextParams LLAMA_70B_Q4 = {
    .model = "llama2\\7b\\ggml-model-q4_0.gguf",
    // TODO: Tune. But this whole model fits on the GPU.
    .num_gpu_layers = 32,
    .num_threads = 30,
    .num_threads_batch = 22,
  };

};
