
#ifndef _LLM_LLM_H
#define _LLM_LLM_H

#include <cstdint>
#include <string>
#include <memory>

#include "llama.h"

#include "base/logging.h"
#include "lastn-buffer.h"
#include "nfa.h"
#include "pcg.h"

// LLM - Convenice wrapper that combines a model, context, and sampler.

// Lower-level stuff:

// Context - An instance of a llama context; a loaded model that has
// processed some tokens (maybe none). If so, can be sampled to produce
// another token.

// Sampler - Samples from an LLM context.

// Parameters for creating an LLM context instance.
struct ContextParams {
  // std::string model = "../llama/models/65B/ggml-model-q4_0.bin";
  std::string model = "d:\\llama2\\llama-2-7b\\ggml-model-f16.gguf";
  // params.model = "../llama/models/7B/ggml-model-q4_0.bin";
  int context_size = 4096;
  int num_threads = 8;
};

enum class SampleType {
  GREEDY,
  MIROSTAT_1,
  MIROSTAT_2,
  TEMPERATURE,
  MIN_P,
};

// Parameters for creating a Sampler.
struct SamplerParams {
  SampleType type = SampleType::MIROSTAT_2;

  // negative means new seed each time
  int64_t seed = -1LL;

  int32_t top_k             = 40;    // <= 0 to use vocab size
  float   top_p             = 0.95f; // 1.0 = disabled
  float   tfs_z             = 1.00f; // 1.0 = disabled
  float   typical_p         = 1.00f; // 1.0 = disabled
  float   temp              = 0.80f; // 1.0 = disabled

  // for min_p sampling.
  float   min_p             = 0.05f; // >= 0.05 probability tokens only.

  // Penalize repeat tokens. We keep a buffer of n recently
  // Observe()d tokens, which are then penalized during sampling.
  int32_t repeat_last_n     = 64;    // last n tokens to penalize
  float   repeat_penalty    = 1.10f; // 1.0 = disabled
  float   frequency_penalty = 0.00f; // 0.0 = disabled
  float   presence_penalty  = 0.00f; // 0.0 = disabled

  float   mirostat_tau      = 5.00f; // target entropy
  float   mirostat_eta      = 0.10f; // learning rate
  // This is a constant in llama.cpp.
  int     mirostat_m = 100;
  bool    penalize_nl       = true;

  // logit bias for specific tokens
  std::unordered_map<llama_token, float> logit_bias;

  // A single NFA that the output must conform to.
  // NFA nfa = Top();
  std::string regex = ".*";
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
//
// Looks like llama now does distinguish between model and context (yay)
// but this needs to be rewritten to make use of that.
struct Context {
  static constexpr bool VERBOSE = false;

  explicit Context(const ContextParams &params = ContextParams());
  ~Context();

  // Copying not supported!
  Context(const Context &other) = delete;
  Context &operator =(const Context &other) = delete;

  int NumLast() const { return num_last; }
  int ContextSize() const { return llama_n_ctx(lctx); }
  int VocabSize() const { return llama_n_vocab(model); }

  // add_bos should be passed for (only) the very first text
  // ("beginning of stream" token). This also allows the tokenizer to
  // insert a leading space.
  std::vector<llama_token> Tokenize(const std::string &text,
                                    bool add_bos);

  // Accept the batch of tokens together. Some processing happens
  // in parallel (e.g. embeddings). Processing in batch increases
  // memory requirements.
  void TakeTokenBatch(const std::vector<llama_token> &batch,
                      const std::function<void(int, int)> &progress = {});

  void TakeTokenSmallBatch(const std::vector<llama_token> &toks,
                           const std::function<void(int, int)> &progress = {});

  // Accept the single token.
  void TakeToken(llama_token t) {
    std::vector<llama_token> batch = {t};
    TakeTokenBatch(batch);
  }

  std::string TokenString(llama_token token) const;

  llama_token NewlineToken() const { return llama_token_nl(model); }
  llama_token EOSToken() const { return llama_token_eos(model); }

  void Reset() {
    // There are no tokens.
    num_last = 0;
    // There have been no predictions.
    last_batch_size = 0;
    // kv cache should be empty.
    llama_kv_cache_seq_rm(
        lctx,
        // any sequence
        -1,
        // clear entire thing
        -1, -1);
  }

  llama_context *GetLlamaContext() const { return lctx; }

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
    Candidates &operator=(const Candidates &other) = delete;
    // Create a new candidates object from the current state of
    // the context. Use Context::GetCandidates().
    Candidates(llama_model *model, llama_context *lctx,
               int last_batch_size);
  };

  // Returns a copy of the candidates (token/logit pairs) from the last
  // call to eval. Unlike llama.cpp, no processing has been performed
  // on these.
  std::unique_ptr<Candidates> GetCandidates() {
    CHECK(last_batch_size > 0) << "No calls to TakeToken*?";
    return std::unique_ptr<Candidates>(
        new Candidates(model, lctx, last_batch_size));
  }


  // State of the transformer, i.e. some tokens have been evaluated,
  // and it's ready to generate the next one. The contents should be
  // treated as opaque and only loaded back into the same Context
  // instance, as they need to agree with context internals.
  //
  // Currently this only allows REWINDING to a previous state in
  // the current context. I need to rework this.
  struct State {
    // std::vector<uint8_t> llama_state;
    // For the context. Doesn't necessarily have to agree with the
    // sampler.
    int num_last = 0;
    int last_batch_size = 0;
  };

  // TODO: Rework this to be more like "checkpoint" and "rewind".
  State SaveState() const;
  void LoadState(const State &state);

private:
  friend class Sampler;
  friend class LLM;

  // Number of tokens that have been evaluated.
  // Should be in [0, llama_n_ctx()).
  int num_last = 0;
  // Size of the last batch evaluated, so we can get the right
  // logits in GetCandidates.
  int last_batch_size = 0;
  int num_threads = 8;
  llama_model *model = nullptr;
  llama_context *lctx = nullptr;
};

// Object that samples an LLM context. There are a bunch of different
// sampling approaches in llama.cpp, with parameters.
//
// I'm in the process of moving rng to here.
//
// Has value semantics so that save/restore can be trivial.
struct Sampler {
  using Params = SamplerParams;
  using Candidates = Context::Candidates;

  uint64_t start_seed = 0;
  PCG32 rng;

  // Degenerate sampler; can't be used.
  Sampler() : context(nullptr), vocab_size(0), last_n_tokens(0, 0) {}

  Sampler(Context *context,
          const SamplerParams &params = SamplerParams());

  static const char *SampleTypeString(SampleType type);

  // Samples a token from the logits. This does not accept the token
  // (although it does currently update sampler state for the mirostat
  // algorithm).
  // You probably want to call Penalize on the candidates first.
  // Consumes the candidates.
  llama_token SampleToken(std::unique_ptr<Candidates> cand);

private:
  // PERF!
  inline float RandFloat() {
    const uint32_t uu = rng.Rand32();
    return (float)((uu   & 0x7FFFFFFF) /
                   (double)0x7FFFFFFF);
  }

  llama_token SampleDistribution(llama_token_data_array *dist);

  llama_token SampleTokenMirostat(struct llama_context *ctx,
                                  llama_token_data_array *candidates,
                                  float tau, float eta, int m, float *mu);
  llama_token SampleTokenMirostatV2(struct llama_context *ctx,
                                    llama_token_data_array *candidates,
                                    float tau, float eta, float *mu);

public:

  // Some samplers depend on the history of the text.
  // Typically, when we take a token for the context, we take that same
  // token for the corresponding sampler.
  void Observe(llama_token id);
  void ObserveBatch(const std::vector<llama_token> &ids);

  void Reset();

  // Reset the regex
  void ResetRegEx();

  // Set the regex (and reset the matcher).
  void SetRegEx(const std::string &re);

  // Set the NFA directly. Note that this does not update the regex
  // string, so Reset will not remember what happened here.
  void SetNFA(NFA<256> nfa_arg);

  void ResetNFA();

  // Filter to those tokens that would leave the matcher in a
  // non-stuck state (note that "stuck" detection in NFA is currently
  // incomplete). Doesn't advance the matcher. Returns true if
  // there are tokens that can still be sampled.
  bool FilterByNFA(Candidates *cands) const;

  // True if the matcher is currently in an accepting state.
  bool Accepting() const { return matcher.IsMatching(); }
  // True if the matcher is in a stuck state where it could not
  // accept any sequence of tokens at this point.
  bool Stuck() const { return matcher.Stuck(); }

  // Reseed the RNG.
  void NewRNG();

  // Apply penalties to candidates.
  void Penalize(Candidates *cands) const;

  // For debugging. Prints the last n tokens that the *sampler* has seen,
  // in forward order.
  void PrintLast() {
    printf("num_last: %d. ", num_last);
    for (int i = num_last - 1; i >= 0; i--) {
      auto t = last_n_tokens[i];
      printf("[@%d,%d=%s]", i, t, context->TokenString(t).c_str());
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

public: // XXX
  NFA<256> nfa;
  NFAMatcher<256> matcher;
};

struct LLM {
  static constexpr bool VERBOSE = false;
  using Candidates = Context::Candidates;

  // XXX Maybe call this internally? Reference count?
  static void Init() {
    // Maybe should consider numa optimizations?
    llama_backend_init(false);
  }
  static void Shutdown() {
    llama_backend_free();
  }

  LLM(const ContextParams &context_params,
      const SamplerParams &sampler_params);

  int VocabSize() { return context.VocabSize(); }
  std::string TokenString(llama_token id) { return context.TokenString(id); }

  // Sample one token, without taking it.
  llama_token Sample();

  // Sample one token and take it. Returns the token's string.
  std::string SampleAndTake();

  void TakeToken(llama_token id) { TakeTokenBatch({id}, false); }
  void TakeTokenBatch(const std::vector<llama_token> &batch,
                      bool progress_bar = false);

  void InsertString(const std::string &s, bool progress_bar = false) {
    // printf("InsertString [%s]\n", s.c_str());
    TakeTokenBatch(context.Tokenize(s, false), progress_bar);
  }

  // Generate until we we see the given delimiter. Return the string up
  // until the beginning of the delimiter (note that there may have been
  // more characters generated after that). If max_length is
  // non-negative, return the empty string if we don't see the target
  // delimiter before accumulating that many characters. (Note: The returned
  // string can actually exceed max_length if the delimiter is a substring
  // of some token.)
  //
  // XXX: Should use NFA for this instead.
  std::string GenerateUntil(const std::string &delimiter,
                            int max_length = -1);

  // As above, but: If we exceed the max length, return the string so
  // far. Second component of pair is true we successfully saw the
  // delimiter. If force_delimiter is true, then once we reach the max
  // length, we insert the delimiter string into the Context state
  // (but still return false for success).
  //
  // XXX: Should use NFA for this instead.
  std::pair<std::string, bool> GenerateUntilEx(
      const std::string &delimiter,
      int max_length = -1,
      bool force_delimiter = false);

  // Generate tokens until the NFA is accepting or stuck.
  // Note that tokens may cross over an accepting state, so
  // using this may require some knowledge of the vocabulary.
  std::string GenerateUntilDone();

  void Reset() {
    context.Reset();
    sampler.Reset();
  }

  // Insert a prompt. If anything has already happened, consider
  // resetting first.
  void DoPrompt(const std::string &prompt, bool progress_bar = true);

  struct State {
    Context::State context_state;
    // XXX this does not work, because the matcher has a pointer
    // to the state in it. We could have an explicit copy.
    Sampler sampler_copy;
  };

  // Note: This only supports rewinding, currently.
  State SaveState() const;
  void LoadState(const State &state);

  // Reseed the RNG.
  void NewRNG() { sampler.NewRNG(); }

  // Print up 'maximum' (or all of them, if -1) candidates,
  // using ANSI color codes.
  void AnsiPrintCandidates(const Candidates &candidates,
                           int maximum);

  // Debugging only.
  void PrintKV() const;

  Context context;
  Sampler sampler;
};

#endif
