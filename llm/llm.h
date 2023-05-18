
#include <cstdint>
#include <string>

#include "llama.h"

#include "base/logging.h"

// This is an instance of a llama context, which is a loaded model
// that has processed some tokens (maybe none). In the future, it
// would be good for us to be able to load the model once and then
// create LLM objects from that, which could then potentially run in
// parallel (or at least maintain their state separately). But this
// doesn't seem to be possible without changes to the llama.cpp model
// loader.
struct LLM {
  struct Params {
    std::string model = "../llama/models/65B/ggml-model-q4_0.bin";
    // params.model = "../llama/models/7B/ggml-model-q4_0.bin";
    int mirostat = 2;
    int context_size = 2048;
    int num_threads = 8;
    // negative means new seed each time
    int64_t seed = -1;
  };

  LLM(const Params &params = Params{}) {
    const int seed = params.seed >= 0 ? params.seed : time(nullptr);

    llama_context_params lparams = llama_context_default_params();

    lparams.n_ctx        = params.context_size;
    // lparams.n_gpu_layers = params.n_gpu_layers;
    lparams.seed         = seed;
    // lparams.f16_kv       = params.memory_f16;
    lparams.use_mmap = true;
    // TODO: Experiment with this
    lparams.use_mlock = false;
    // For special-purpose uses.
    lparams.logits_all = false;
    lparams.embedding = false;

    lctx = llama_init_from_file(params.model.c_str(), lparams);

    CHECK(lctx != nullptr) << params.model;
  }



private:
  llama_context *lctx;
};
