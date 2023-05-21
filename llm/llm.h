
#include <cstdint>
#include <string>
#include <memory>

#include "llama.h"

#include "base/logging.h"

// This is an instance of a llama context, which is a loaded model
// that has processed some tokens (maybe none). In the future, it
// would be good for us to be able to load the model once and then
// create LLM objects from that, which could then potentially run in
// parallel (or at least maintain their state separately). But this
// doesn't seem to be possible without changes to the llama.cpp model
// loader.
struct LLMParams {
  std::string model = "../llama/models/65B/ggml-model-q4_0.bin";
  // params.model = "../llama/models/7B/ggml-model-q4_0.bin";
  int context_size = 2048;
  int num_threads = 8;
  // negative means new seed each time
  int64_t seed = -1LL;

  // sampling parameters. All this could be part of the sampling
  // interface, not part of the LLM struct...
  int32_t top_k             = 40;    // <= 0 to use vocab size
  float   top_p             = 0.95f; // 1.0 = disabled
  float   tfs_z             = 1.00f; // 1.0 = disabled
  float   typical_p         = 1.00f; // 1.0 = disabled
  float   temp              = 0.80f; // 1.0 = disabled
  float   repeat_penalty    = 1.10f; // 1.0 = disabled
  int32_t repeat_last_n     = 64;    // last n tokens to penalize (0 = disable penalty, -1 = context size)
  float   frequency_penalty = 0.00f; // 0.0 = disabled
  float   presence_penalty  = 0.00f; // 0.0 = disabled
  // 0 = disabled, 1 = mirostat, 2 = mirostat 2.0
  int     mirostat          = 2;
  float   mirostat_tau      = 5.00f; // target entropy
  float   mirostat_eta      = 0.10f; // learning rate
  bool    penalize_nl       = true;

  // logit bias for specific tokens
  std::unordered_map<llama_token, float> logit_bias;
};

struct LLM {
  using Params = LLMParams;

  enum class SampleType {
    GREEDY,
    MIROSTAT_1,
    MIROSTAT_2,
    TEMPERATURE,
  };

  LLM(const Params &params = Params()) {
    num_threads = params.num_threads;
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

    logit_bias = params.logit_bias;

    last_n_tokens.resize(params.context_size);
    Reset();
  }

  ~LLM() {
    llama_free(lctx);
    lctx = nullptr;
  }

  // Copying not supported!
  LLM(const LLM &other) = delete;
  LLM &operator =(LLM other) = delete;

  // add_bos should be passed for the very first text ("beginning of stream"
  // token).
  std::vector<llama_token> Tokenize(const std::string &text, bool add_bos) {
    // initialize to prompt numer of chars, since n_tokens <= n_prompt_chars
    std::vector<llama_token> res(text.size() + (int) add_bos);
    const int n = llama_tokenize(
        lctx, text.c_str(), res.data(), res.size(), add_bos);
    assert(n >= 0);
    res.resize(n);
    return res;
  }

  // Accept the batch of tokens together. Some processing happens
  // in parallel (e.g. embeddings). Processing in batch increases
  // memory requirements.
  void TakeTokenBatch(const std::vector<llama_token> &batch) {
    CHECK(0 == llama_eval(lctx, batch.data(), batch.size(),
                          num_last, num_threads));

    for (llama_token t : batch) {
      last_n_tokens.erase(last_n_tokens.begin());
      last_n_tokens.push_back(t);
    }
    // keep track of the number of tokens of valid history, but
    // saturate once we get to the maximum buffer size.
    num_last += (int)batch.size();
    num_last = std::min(num_last, (int)last_n_tokens.size());
  }

  // Accept the single token.
  void TakeToken(llama_token t) {
    std::vector<llama_token> batch = {t};
    TakeTokenBatch(batch);
  }

  // Insert a prompt. If anything has already happened, consider
  // resetting first.
  void DoPrompt(std::string prompt) {
    // Add a space in front of the first character to match OG llama
    // tokenizer behavior
    prompt.insert(0, 1, ' ');

    // tokenize the prompt
    auto embd_inp = Tokenize(prompt, true);

    const int n_ctx = llama_n_ctx(lctx);

    CHECK((int)embd_inp.size() <= n_ctx - 4) << "Prompt too long ("
                                             << (int)embd_inp.size()
                                             << " tokens)";
    TakeTokenBatch(embd_inp);
  }

  void InsertString(const string &s) {
    TakeTokenBatch(Tokenize(s, false));
  }

  std::string TokenString(llama_token t) {
    return llama_token_to_str(lctx, t);
  }

  // Generate until we we see the given delimiter. Return the string up
  // until the beginning of the delimiter (note that there may have been
  // more characters generated after that). If max_length is
  // non-negative, return the empty string if we don't see the target
  // delimiter before accumulating that many characters. (Note: The returned
  // string can actually exceed max_length if the delimiter is a substring
  // of some token.)
  std::string GenerateUntil(const string &delimiter,
                            SampleType sample_type = SampleType::MIROSTAT_2,
                            int max_length = -1) {
    std::string got;
    for (;;) {
      std::unique_ptr<LLM::Candidates> candidates = GetCandidates();
      int id = SampleToken(sample_type, std::move(candidates));
      // Commit the token.
      TakeToken(id);
      got += TokenString(id);

      // PERF don't need to search the whole string each time.
      auto pos = got.find(delimiter);
      if (pos != string::npos) {
        return got.substr(0, pos);
      }
      if (max_length >= 0 && (int)got.size() > max_length) {
        return "";
      }
    }
  }

  void Reset() {
    // (Or use BOS token?)
    std::fill(last_n_tokens.begin(), last_n_tokens.end(), 0);
    mirostat_mu = 2.0f * mirostat_tau;
    num_last = 0;
  }

  struct Candidates {
    // Contains eligible tokens with log-odds.
    // If the sorted flag is set, they must be sorted by log odds,
    // descending.
    // The data pointer in here is owned by the Candidates object.
    llama_token_data_array ltda;

    // Cheap "iterator" (for ranged for loops).
    llama_token_data *begin() { return ltda.data; }
    llama_token_data *end() { return ltda.data + ltda.size; }
    const llama_token_data *begin() const { return ltda.data; }
    const llama_token_data *end() const { return ltda.data + ltda.size; }

    ~Candidates() {
      delete []ltda.data;
      ltda.size = 0;
    }

    Candidates(Candidates &&other) {
      ltda = other.ltda;
      other.ltda.data = nullptr;
      other.ltda.size = 0;
    }

  private:
    friend class LLM;
    // Create a new candidates object from the current state of
    // the context. Use LLM::GetCandidates().
    Candidates(llama_context *lctx) {
      const int n_vocab = llama_n_vocab(lctx);
      ltda.data = new llama_token_data[n_vocab];
      ltda.size = n_vocab;
      ltda.sorted = false;
      // this is size n_vocab (just the last token) if
      // params.logits_all is false (XXX assert!)
      const float *logits = llama_get_logits(lctx);
      for (llama_token token_id = 0; token_id < n_vocab; token_id++) {
        ltda.data[token_id] =
          llama_token_data{token_id, logits[token_id], 0.0f};
      }
    }

  };

  // XXX this modifies the logits in place; would be better if it
  // only modified the candidates object (which would be possible).
  std::unique_ptr<Candidates> GetCandidates() {
    // XXX configurable. These are defaults from common.h.
    const int32_t repeat_last_n   = 64;
    const float   repeat_penalty  = 1.10f;
    const float   alpha_presence  = 0.0f;
    const float   alpha_frequency = 0.0f;
    const bool    penalize_nl     = true;

    // const int n_vocab = llama_n_vocab(lctx);
    const int n_ctx = llama_n_ctx(lctx);

    // Apply penalties. This code likes having random access to the
    // tokens, so we do this on the raw logits array (before any
    // reordering).
    float *logits = llama_get_logits(lctx);

    // Apply params.logit_bias map
    for (const auto &[tok, bias] : logit_bias) {
      logits[tok] += bias;
    }

    const int nl_id = llama_token_nl();
    const float nl_logit = logits[nl_id];
    // Note that llama.cpp (example?) originally used the full
    // last_n_tokens here, which seems wrong. Just considering
    // num_last. -tom7
    const int last_n_repeat = std::min(std::min(num_last, repeat_last_n),
                                       n_ctx);

    // Now create a proper candidates set from the logits that we
    // modified in place.
    std::unique_ptr<Candidates> cand(new Candidates(lctx));

    llama_sample_repetition_penalty(
        lctx, &cand->ltda,
        last_n_tokens.data() + last_n_tokens.size() - last_n_repeat,
        last_n_repeat, repeat_penalty);
    llama_sample_frequency_and_presence_penalties(
        lctx, &cand->ltda,
        last_n_tokens.data() + last_n_tokens.size() - last_n_repeat,
        last_n_repeat, alpha_frequency, alpha_presence);
    // Restore newline logit if we don't allow penalizing it.
    if (!penalize_nl) {
      // Note that in llama.cpp this modified the original logit
      // array, not the candidates copy we just made, so it had
      // no effect...
      for (llama_token_data &tok : *cand) {
        if (tok.id == nl_id) tok.logit = nl_logit;
      }
    }

    return cand;
  }

  // Samples a token from the logits. This does not accept the token
  // (although it does currently update sampler state).
  // Consumes the candidates.
  llama_token SampleToken(SampleType type,
                          std::unique_ptr<Candidates> cand) {
    // XXX make configurable
    const int32_t top_k             = 40;    // <= 0 to use vocab size
    const float   top_p             = 0.95f; // 1.0 = disabled
    const float   tfs_z             = 1.00f; // 1.0 = disabled
    const float   typical_p         = 1.00f; // 1.0 = disabled
    const float   temp              = 0.80f; // 1.0 = disabled
    const float   mirostat_eta      = 0.10f; // learning rate

    switch (type) {
    default:
    case SampleType::GREEDY:
      return llama_sample_token_greedy(lctx, &cand->ltda);
    case SampleType::MIROSTAT_1: {
      static constexpr int mirostat_m = 100;
      llama_sample_temperature(lctx, &cand->ltda, temp);
      return llama_sample_token_mirostat(
          lctx, &cand->ltda, mirostat_tau, mirostat_eta,
          mirostat_m, &mirostat_mu);
    }
    case SampleType::MIROSTAT_2:
      llama_sample_temperature(lctx, &cand->ltda, temp);
      return llama_sample_token_mirostat_v2(
          lctx, &cand->ltda, mirostat_tau,
          mirostat_eta, &mirostat_mu);
    case SampleType::TEMPERATURE:
      llama_sample_top_k(lctx, &cand->ltda, top_k, 1);
      llama_sample_tail_free(lctx, &cand->ltda, tfs_z, 1);
      llama_sample_typical(lctx, &cand->ltda, typical_p, 1);
      llama_sample_top_p(lctx, &cand->ltda, top_p, 1);
      llama_sample_temperature(lctx, &cand->ltda, temp);
      return llama_sample_token(lctx, &cand->ltda);
    }
  }

  // State of the transformer, i.e. some tokens have been evaluated,
  // and it's ready to generate the next one. The contents should be
  // treated as opaque and only loaded back into the same LLM
  // instance, as they need to agree with context internals.
  struct State {
    std::vector<uint8_t> llama_state;
    std::vector<llama_token> last_n_tokens;
    int num_last = 0;
    float mirostat_mu = 10.0f;
  };

  State SaveState() const {
    State state;
    const size_t n_state_size_max = llama_get_state_size(lctx);
    state.llama_state.resize(n_state_size_max);
    const size_t n_state_size_cur =
      llama_copy_state_data(lctx, state.llama_state.data());
    state.llama_state.resize(n_state_size_cur);
    state.llama_state.shrink_to_fit();

    state.last_n_tokens = last_n_tokens;
    state.num_last = num_last;
    state.mirostat_mu = mirostat_mu;
    return state;
  }

  void LoadState(const State &state) {
    Reset();
    size_t bytes_read =
      llama_set_state_data(lctx,
                           (const uint8_t *)state.llama_state.data());
    CHECK(bytes_read == state.llama_state.size());
    last_n_tokens = state.last_n_tokens;
    num_last = state.num_last;
    mirostat_mu = state.mirostat_mu;
  }

  // private:
  int num_threads = 8;
  // logit bias for specific tokens
  std::unordered_map<llama_token, float> logit_bias;

  llama_context *lctx = nullptr;
  // TODO: replace with ring-buffer
  // Contains the tokens recently evaluated, up to the maximum.
  // Most recent token at the end. Always the same size as the
  // internal context (padded with 0 before that).
  std::vector<llama_token> last_n_tokens;
  // Size of the tail of the ring buffer that contains actual observed
  // tokens. This is bounded by the number of tokens that have been
  // previously evaluated in the context (and is the same unless we
  // reset).
  int num_last = 0;
  // Perhaps group sampling params? Or just make these part
  // of a Sampler object?
  const float mirostat_tau = 5.0f;
  float mirostat_mu = 10.0f;
};
