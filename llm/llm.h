
#ifndef _LLM_LLM_H
#define _LLM_LLM_H

#include <cstdint>
#include <string>
#include <memory>

#include "llama.h"

#include "base/logging.h"
#include "lastn-buffer.h"
#include "nfa.h"

// LLM - This will be the convenience wrapper (TODO)

// Lower-level stuff:

// Context - An instance of a llama context; a loaded model that has
// processed some tokens (maybe none). If so, can be sampled to produce
// another token.

// Sampler - Samples from an LLM context.

// Parameters for creating an LLM context instance.
struct ContextParams {
  std::string model = "../llama/models/65B/ggml-model-q4_0.bin";
  // params.model = "../llama/models/7B/ggml-model-q4_0.bin";
  int context_size = 2048;
  int num_threads = 8;
  // negative means new seed each time
  int64_t seed = -1LL;
};

enum class SampleType {
  GREEDY,
  MIROSTAT_1,
  MIROSTAT_2,
  TEMPERATURE,
};

// Parameters for creating a Sampler.
struct SamplerParams {
  SampleType type = SampleType::MIROSTAT_2;

  int32_t top_k             = 40;    // <= 0 to use vocab size
  float   top_p             = 0.95f; // 1.0 = disabled
  float   tfs_z             = 1.00f; // 1.0 = disabled
  float   typical_p         = 1.00f; // 1.0 = disabled
  float   temp              = 0.80f; // 1.0 = disabled

  // Penalize repeat tokens. We keep a buffer of n recently
  // Observe()d tokens, which are then penalized during sampling.
  int32_t repeat_last_n     = 64;    // last n tokens to penalize
  float   repeat_penalty    = 1.10f; // 1.0 = disabled
  float   frequency_penalty = 0.00f; // 0.0 = disabled
  float   presence_penalty  = 0.00f; // 0.0 = disabled

  // 0 = disabled, 1 = mirostat, 2 = mirostat 2.0
  // XXX determined by sampletype
  // int     mirostat          = 2;
  float   mirostat_tau      = 5.00f; // target entropy
  float   mirostat_eta      = 0.10f; // learning rate
  // This is a constant in llama.cpp.
  int     mirostat_m = 100;
  bool    penalize_nl       = true;


  // logit bias for specific tokens
  std::unordered_map<llama_token, float> logit_bias;

  // A single NFA that the output must conform to.
  // NFA nfa = Top();
  string regex = ".*";
};

struct Context;
struct Sampler;

// An instance of a llama context: A loaded model that has processed
// some tokens (maybe none). Has a fixed context size.
//
// In the future, it would be good for us to be able to load the model
// once and then create LLM objects from that, which could then
// potentially run in parallel (or at least maintain their state
// separately). But this doesn't seem to be possible without changes
// to the llama.cpp model loader. (Fortunately, since it's using mmap
// it seems reasonably efficient to just open the same model file
// multiple times.)
struct Context {
  Context(const ContextParams &params = ContextParams()) {
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
    Reset();
  }

  ~Context() {
    llama_free(lctx);
    lctx = nullptr;
  }

  // Copying not supported!
  Context(const Context &other) = delete;
  Context &operator =(Context other) = delete;

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
    const int ctx_size = llama_n_ctx(lctx);
    CHECK(num_last + (int)batch.size() <= ctx_size)
      << num_last << " + " << batch.size() << " would exceed " << ctx_size;

    CHECK(0 == llama_eval(lctx, batch.data(), batch.size(),
                          num_last, num_threads));

    num_last += (int)batch.size();
  }

  // Accept the single token.
  void TakeToken(llama_token t) {
    std::vector<llama_token> batch = {t};
    TakeTokenBatch(batch);
  }

  int VocabSize() const {
    return llama_n_vocab(lctx);
  }

  std::string TokenString(llama_token t) {
    return llama_token_to_str(lctx, t);
  }

  void Reset() {
    // All we need to do is declare that the context contains no tokens.
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

    // size_t size() const { return ldta.size(); }

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
    friend class Context;
    // Only move. (XXX test)
    Candidates(const Candidates &other) = delete;
    Candidates(const &other) = delete;
    // Create a new candidates object from the current state of
    // the context. Use Context::GetCandidates().
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

  // Returns a copy of the candidates (token/logit pairs) from the last
  // call to eval. Unlike llama.cpp, no processing has been performed
  // on these.
  std::unique_ptr<Candidates> GetCandidates() {
    return std::unique_ptr<Candidates>(new Candidates(lctx));
  }


  // State of the transformer, i.e. some tokens have been evaluated,
  // and it's ready to generate the next one. The contents should be
  // treated as opaque and only loaded back into the same Context
  // instance, as they need to agree with context internals.
  struct State {
    std::vector<uint8_t> llama_state;
    // For the context. Doesn't necessarily have to agree with the
    // sampler.
    int num_last = 0;
  };

  // XXX this probably moves to the wrapper layer, and saves both
  // the context and the sampler.
  State SaveState() const {
    State state;
    const size_t n_state_size_max = llama_get_state_size(lctx);
    state.llama_state.resize(n_state_size_max);
    const size_t n_state_size_cur =
      llama_copy_state_data(lctx, state.llama_state.data());
    state.llama_state.resize(n_state_size_cur);
    state.llama_state.shrink_to_fit();
    state.num_last = num_last;
    return state;
  }

  void LoadState(const State &state) {
    Reset();
    size_t bytes_read =
      llama_set_state_data(lctx,
                           // XXX I think this is morally const, but
                           // should check.
                           const_cast<uint8_t *>(
                               state.llama_state.data()));
    CHECK(bytes_read == state.llama_state.size());
    num_last = state.num_last;
  }

  // private:
  // Number of tokens that have been evaluated.
  // Should be in [0, llama_n_ctx()).
  int num_last = 0;
  int num_threads = 8;
  llama_context *lctx = nullptr;
};

// Object that samples an LLM context. There are a bunch of different
// sampling approaches in llama.cpp, with parameters.
//
// This is where the rng should go, as we only need randomness for
// sampling. But since llama.cpp bakes it into the context, we just
// use the one in the context. This means that we forego the ability
// to reproduce samples after a save/restore, but we already don't
// have determinism (because of threaded execution of floating point
// ops) so that's not a big deal.
//
// Has value semantics so that save/restore can be trivial.
struct Sampler {
  using Params = SamplerParams;
  using Candidates = Context::Candidates;

  // Degenerate sampler; can't be used.
  Sampler() : context(nullptr), vocab_size(0), last_n_tokens(0, 0) {}

  // We only need two things from the context: The RNG and the
  // vocabulary. Would be better to decouple them.
  Sampler(Context *context,
          const SamplerParams &params = SamplerParams()) :
    context(context),
    params(params),
    vocab_size(context->VocabSize()),
    last_n_tokens(params.repeat_last_n, 0) {
    Reset();
  }

  static const char *SampleTypeString(SampleType type) {
    switch (type) {
    default: return "???";
    case SampleType::GREEDY: return "GREEDY";
    case SampleType::MIROSTAT_1: return "MIROSTAT_1";
    case SampleType::MIROSTAT_2: return "MIROSTAT_2";
    case SampleType::TEMPERATURE: return "TEMPERATURE";
    }
  }

  // Samples a token from the logits. This does not accept the token
  // (although it does currently update sampler state for the mirostat
  // algorithm).
  // You probably want to call Penalize on the candidates first.
  // Consumes the candidates.
  llama_token SampleToken(std::unique_ptr<Candidates> cand) {
    llama_context *lctx = context->lctx;

    switch (params.type) {
    default:
    case SampleType::GREEDY:
      return llama_sample_token_greedy(lctx, &cand->ltda);
    case SampleType::MIROSTAT_1: {
      llama_sample_temperature(lctx, &cand->ltda, params.temp);
      return llama_sample_token_mirostat(
          lctx, &cand->ltda, params.mirostat_tau, params.mirostat_eta,
          params.mirostat_m, &mirostat_mu);
    }
    case SampleType::MIROSTAT_2:
      llama_sample_temperature(lctx, &cand->ltda, params.temp);
      return llama_sample_token_mirostat_v2(
          lctx, &cand->ltda, params.mirostat_tau,
          params.mirostat_eta, &mirostat_mu);
    case SampleType::TEMPERATURE:
      llama_sample_top_k(lctx, &cand->ltda, params.top_k, 1);
      llama_sample_tail_free(lctx, &cand->ltda, params.tfs_z, 1);
      llama_sample_typical(lctx, &cand->ltda, params.typical_p, 1);
      llama_sample_top_p(lctx, &cand->ltda, params.top_p, 1);
      llama_sample_temperature(lctx, &cand->ltda, params.temp);
      return llama_sample_token(lctx, &cand->ltda);
    }
  }

  // Some samplers depend on the history of the text.
  // Typically, when we take a token for the context, we take that same
  // token for the corresponding sampler.
  void Observe(llama_token id) {
    last_n_tokens.push_front(id);
    num_last++;
    if (num_last == last_n_tokens.size()) num_last = last_n_tokens.size();

    // advance matcher state.
    std::string s = context->TokenString(id);
    for (uint8_t c : s) {
      matcher.Advance(c);
    }
  }

  void ObserveBatch(const std::vector<llama_token> &ids) {
    for (llama_token id : ids) Observe(id);
  }

  void Reset() {
    ResetRegEx();
    // 0 token is <unk>, which I think means invalid.
    // (Or use BOS token?)
    std::fill(last_n_tokens.begin(), last_n_tokens.end(), 0);
    mirostat_mu = 2.0f * params.mirostat_tau;
    num_last = 0;
  }

  // Reset the regex
  void ResetRegEx() {
    nfa = RemoveEpsilon<256>(Parse(params.regex));
    ResetNFA();
  }

  // Set the regex (and reset the matcher).
  void SetRegEx(const std::string &re) {
    params.regex = re;
    ResetRegEx();
  }

  // Note that this does not update the regex string, so Reset
  // will not remember what happened here.
  void SetNFA(NFA<256> nfa_arg) {
    nfa = std::move(nfa_arg);
    ResetNFA();
  }

  void ResetNFA() {
    matcher = NFAMatcher<256>(nfa);
  }

  // Filter to those tokens that would leave the matcher in a
  // non-stuck state (note that "stuck" detection in NFA is currently
  // incomplete). Doesn't advance the matcher. Returns true if
  // there are tokens that can still be sampled.
  bool FilterByNFA(Candidates *cands) const {
    // TODO: I think we can actually just shrink the candidates
    // array, which would have other advantages.
    static constexpr float IMPOSSIBLE = -1.0e28f;
    bool any_left = false;
    for (llama_token_data &cand : *cands) {
      CHECK(cand.id >= 0 && cand.id < vocab_size);
      std::string s = context->TokenString(cand.id);
      // Some special tokens (EOS, BOS) are empty. We should not
      // predict them when conforming to a regex!
      // (But this is kind of a hack...)
      if (s.empty()) {
        cand.logit = IMPOSSIBLE;
        continue;
      }

      NFAMatcher<256> mcopy = matcher;
      for (uint8_t c : s)
        mcopy.Advance(c);
      if (mcopy.Stuck()) {
        cand.logit = IMPOSSIBLE;
      } else {
        any_left = true;
      }
    }
    return any_left;
  }

  bool Stuck() const {
    return matcher.Stuck();
  }

  // Apply penalties to candidates.
  void Penalize(Candidates *cands) const {
    cands->ltda.sorted = false;

    // Save nl logit so that we can restore it if
    const int nl_id = llama_token_nl();
    float old_nl_logit = 0.0f;

    // PERF: The candidates start in order, so we could (awkwardly)
    // assume that here if we wanted to be a little faster.
    for (llama_token_data &cand : *cands) {
      CHECK(cand.id >= 0 && cand.id < vocab_size);

      auto it = params.logit_bias.find(cand.id);
      if (it != params.logit_bias.end()) cand.logit += it->second;
      if (cand.id == nl_id) old_nl_logit = cand.logit;
    }

    // Note that llama.cpp (example?) originally used the full
    // last_n_tokens here, which seems wrong. Just considering
    // num_last. -tom7
    const int last_do = std::min(num_last, params.repeat_last_n);

    std::vector<int> prev_count;
    // PERF rather than compute this each time, we could keep track of
    // token counts and update in observe.
    auto ComputeCount = [this, last_do, &prev_count]() {
        if (prev_count.empty()) {
          prev_count.resize(vocab_size, 0);
          for (int i = 0; i < last_do; i++) {
            prev_count[last_n_tokens[i]]++;
          }
        }
      };

    // From llama_sample_repetition_penalty, but working on a candidate
    // array.
    if (last_do > 0 && params.repeat_penalty != 1.0f) {
      ComputeCount();

      // Now, for each token, penalize if it has appeared.
      for (llama_token_data &cand : *cands) {
        if (prev_count[cand.id] > 0) {
          if (cand.logit <= 0) {
            cand.logit *= params.repeat_penalty;
          } else {
            cand.logit /= params.repeat_penalty;
          }
        }
      }
    }


    // From llama_sample_frequency_and_presence_penalties.
    if (last_do > 0 && (params.frequency_penalty != 0.0f ||
                        params.presence_penalty != 0.0f)) {
      ComputeCount();

      for (llama_token_data &cand : *cands) {
        int count = prev_count[cand.id];
        if (count > 0) {
          cand.logit -= float(count) * params.frequency_penalty +
             params.presence_penalty;
        }
      }
    }

    // Restore newline logit if we don't allow penalizing it.
    if (!params.penalize_nl) {
      // Note that in llama.cpp this modified the original logit
      // array, not the candidates copy we just made, so it had
      // no effect...
      for (llama_token_data &cand : *cands) {
        if (cand.id == nl_id) cand.logit = old_nl_logit;
      }
    }
  }

private:
  Context *context = nullptr;
  // Would generally be ok to change these on the fly, but not e.g. the
  // last_n_tokens size.
  Params params;
  // This should probably be like a pointer to a vocab object?
  int vocab_size = 0;
  // Contains the tokens recently evaluated, up to the maximum.
  // Most recent token at position 0. So [0...num_last) are the
  // most recent tokens we've seen, in reverse order, up to
  // the size of this buffer.
  LastNBuffer<llama_token> last_n_tokens;
  // Size of the front of the ring buffer that contains actual observed
  // tokens. (Could be part of some wrapper of "last up to n buffer".)
  int num_last = 0;

  // State for various sampler types.
  float mirostat_mu = 10.0f;

  NFA<256> nfa;
  NFAMatcher<256> matcher;
};

struct LLM {
  using Candidates = Context::Candidates;

  LLM(const ContextParams &context_params,
      const SamplerParams &sampler_params) : context(context_params),
                                             sampler(&context,
                                                     sampler_params) {
  }

  int VocabSize() { return context.VocabSize(); }
  std::string TokenString(llama_token id) { return context.TokenString(id); }

  int Sample() {
    std::unique_ptr<Context::Candidates> candidates = context.GetCandidates();
    sampler.Penalize(candidates.get());
    sampler.FilterByNFA(candidates.get());
    return sampler.SampleToken(std::move(candidates));
  }

  std::string SampleAndTake() {
    int id = Sample();
    // Commit the token.
    TakeTokenBatch({id});
    return context.TokenString(id);
  }

  void TakeToken(llama_token id) { TakeTokenBatch({id}); }

  void TakeTokenBatch(const std::vector<llama_token> &batch) {
    context.TakeTokenBatch(batch);
    sampler.ObserveBatch(batch);
  }

  void InsertString(const string &s) {
    TakeTokenBatch(context.Tokenize(s, false));
  }

  // Generate until we we see the given delimiter. Return the string up
  // until the beginning of the delimiter (note that there may have been
  // more characters generated after that). If max_length is
  // non-negative, return the empty string if we don't see the target
  // delimiter before accumulating that many characters. (Note: The returned
  // string can actually exceed max_length if the delimiter is a substring
  // of some token.)
  std::string GenerateUntil(const string &delimiter,
                            int max_length = -1) {
    const auto &[got, success] =
      GenerateUntilEx(delimiter, max_length);
    if (success) return got;
    else return "";
  }

  // As above, but: If we exceed the max length, return the string so
  // far. Second component of pair is true we successfully saw the
  // delimiter. If force_delimiter is true, then once we reach the max
  // length, we insert the delimiter string into the Context state
  // (but still return false for success).
  std::pair<std::string, bool> GenerateUntilEx(
      const string &delimiter,
      int max_length = -1,
      bool force_delimiter = false) {
    std::string got;
    for (;;) {
      std::unique_ptr<Context::Candidates> candidates = context.GetCandidates();
      sampler.Penalize(candidates.get());
      sampler.FilterByNFA(candidates.get());
      int id = sampler.SampleToken(std::move(candidates));
      // Commit the token.
      TakeTokenBatch({id});
      got += context.TokenString(id);

      // PERF don't need to search the whole string each time.
      auto pos = got.find(delimiter);
      if (pos != string::npos) {
        return make_pair(got.substr(0, pos), true);
      }
      if (max_length >= 0 && (int)got.size() > max_length) {
        InsertString(delimiter);
        return make_pair(got, false);
      }
    }
  }

  void Reset() {
    context.Reset();
    sampler.Reset();
  }

  // Insert a prompt. If anything has already happened, consider
  // resetting first.
  void DoPrompt(std::string prompt) {
    // Add a space in front of the first character to match OG llama
    // tokenizer behavior
    prompt.insert(0, 1, ' ');

    // tokenize the prompt
    auto embd_inp = context.Tokenize(prompt, true);

    const int n_ctx = llama_n_ctx(context.lctx);

    CHECK((int)embd_inp.size() <= n_ctx - 4) << "Prompt too long ("
                                             << (int)embd_inp.size()
                                             << " tokens)";
    TakeTokenBatch(embd_inp);
  }

  struct State {
    Context::State context_state;
    Sampler sampler_copy;
  };

  State SaveState() const {
    State state;
    state.context_state = context.SaveState();
    state.sampler_copy = sampler;
    return state;
  }

  void LoadState(const State &state) {
    context.LoadState(state.context_state);
    sampler = state.sampler_copy;
  }

  // Print up 'maximum' (or all of them, if -1) candidates,
  // using ANSI color codes.
  void AnsiPrintCandidates(const Candidates &candidates,
                           int maximum) {
    auto IsAscii = [](const std::string &s) {
        for (char c : s) {
          if (c < ' ' || c > '~') return false;
        }
        return true;
      };

    std::vector<std::pair<string, float>> toks;
    for (const llama_token_data &tok : candidates) {
      if (tok.id == llama_token_nl()) {
        toks.emplace_back("\\n", tok.logit);
      } else {
        string s = context.TokenString(tok.id);
        if (IsAscii(s)) {
          toks.emplace_back(s, tok.logit);
        } else {
          toks.emplace_back("??", tok.logit);
        }
      }
    }

    std::sort(toks.begin(), toks.end(),
              [](const std::pair<string, float> &a,
                 const std::pair<string, float> &b) {
                return a.second > b.second;
              });
    for (int i = 0;
         (maximum < 0 || i < maximum) && i < (int)toks.size();
         i++) {
      printf("  [%s] %.9f\n", toks[i].first.c_str(), toks[i].second);
    }
  }

  Context context;
  Sampler sampler;
};

#endif
