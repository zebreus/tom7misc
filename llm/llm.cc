
#include "llm.h"

#include <cstdint>
#include <string>
#include <memory>

#include "llama.h"

// TODO: Fix upstream?
#undef LOG
#include "common/common.h"
#undef LOG

#include "base/logging.h"
#include "lastn-buffer.h"
#include "nfa.h"
#include "pcg.h"

#include "timer.h"
#include "ansi.h"
#include "periodically.h"

static void LogLLM(enum ggml_log_level level,
                   const char * text,
                   void * user_data) {
  // Ignored. Could print if if verbose is on?
}

Context::Context(const ContextParams &params) {
  llama_log_set(LogLLM, nullptr);

  struct ProgressData {
    ProgressData() : per(1.0) {}
    Periodically per;
    std::string name;
    Timer timer;
  };
  ProgressData progress_data;
  progress_data.name = params.model;
  auto Progress = [](float f, void *void_data) {
      ProgressData *data = (ProgressData*)void_data;
      if (data->per.ShouldRun()) {
        printf(ANSI_UP
               "%s\n", ANSI::ProgressBar(f * 100, 100,
                                         data->name,
                                         data->timer.Seconds()).c_str());
      }
    };
  auto Done = [&progress_data]() {
      printf(ANSI_UP
             "Loaded " AWHITE("%s") " in %s\n",
             progress_data.name.c_str(),
             ANSI::Time(progress_data.timer.Seconds()).c_str());
    };



  // PERF need to tell llama about this now
  num_threads = params.num_threads;

  llama_model_params mparams = llama_model_default_params();
  // TODO progress_callback?
  mparams.use_mmap = true;
  // TODO: Experiment with this
  mparams.use_mlock = false;

  mparams.progress_callback = +Progress;
  mparams.progress_callback_user_data = (void*)&progress_data;


  llama_context_params lparams = llama_context_default_params();
  // lparams.n_ctx        = params.context_size;
  // "from model"
  lparams.n_ctx = 0;

  // Note: We have our own seed because we do our own sampling.
  // XXX make this deterministic, then!
  lparams.seed         = -1;

  // For special-purpose uses.
  lparams.logits_all = false;
  lparams.embedding = false;

  model = llama_load_model_from_file(params.model.c_str(), mparams);
  CHECK(model != nullptr) << params.model;

  lctx = llama_new_context_with_model(model, lparams);
  CHECK(lctx != nullptr) << params.model;
  Reset();
  Done();
}

Context::~Context() {
  llama_free_model(model);
  model = nullptr;
  llama_free(lctx);
  lctx = nullptr;
}

std::vector<llama_token> Context::Tokenize(
    const std::string &text, bool add_bos) {
  // initialize to prompt number of chars, since n_tokens <= n_prompt_chars
  // (For llama2, adding 1, since tokenizing just "\n" yields two tokens??)
  const int max_tokens = text.size() + 1 + (int)add_bos;
  std::vector<llama_token> res(max_tokens);
  const int n = llama_tokenize(
      model, text.c_str(), text.size(), res.data(), res.size(), add_bos,
      // TODO: what is "special"?
      false,
      // Don't add leading space unless we're at the beginning.
      add_bos);
  CHECK(n >= 0) << "Tokenizing [" << text << "] (res size " <<
    res.size() << ") got n=" << n;
  CHECK(n <= max_tokens) << "got n=" << n << " but max_tokens=" << max_tokens;
  res.resize(n);
  return res;
}

// PERF tune
static constexpr int MAX_TOKENS_PER_BATCH = 64;

void Context::TakeTokenBatch(const std::vector<llama_token> &batch,
                             const std::function<void(int, int)> &progress) {
  if (batch.size() < MAX_TOKENS_PER_BATCH)
    return TakeTokenSmallBatch(batch, progress);

  // PERF
  for (int start_idx = 0;
       start_idx < (int)batch.size();
       start_idx += MAX_TOKENS_PER_BATCH) {
    std::vector<llama_token> small_batch;
    small_batch.reserve(MAX_TOKENS_PER_BATCH);
    for (int i = 0;
         start_idx + i < (int)batch.size() && i < MAX_TOKENS_PER_BATCH;
         i++) {
      small_batch.push_back(batch[start_idx + i]);
    }
    if (VERBOSE) {
      fprintf(stderr, "Batch of size %d.\n", (int)small_batch.size());
    }
    auto prog = [&progress, start_idx, &batch](int n, int d) {
        if (progress) progress(start_idx + n, batch.size());
      };

    TakeTokenSmallBatch(small_batch, prog);
  }
}

void Context::TakeTokenSmallBatch(
    const std::vector<llama_token> &toks,
    const std::function<void(int, int)> &progress) {
  if (VERBOSE) {
    printf("TakeTokenSmallBatch(" APURPLE("%d") ")\n", (int)toks.size());
    for (llama_token t : toks) {
      printf("  %d=%s\n", t, TokenString(t).c_str());
    }
  }

  CHECK(!toks.empty());

  CHECK(toks.size() <= MAX_TOKENS_PER_BATCH);
  const int ctx_size = llama_n_ctx(lctx);
  CHECK(num_last + (int)toks.size() <= ctx_size)
    << num_last << " + " << toks.size() << " would exceed " << ctx_size;

  // PERF: We can use the token data directly, but llama_batch
  // has a non-const pointer in it (because some utilities modify
  // a batch). Cleanest integration is to copy.
  llama_batch batch =
    llama_batch_init(toks.size(),
                     // Storing tokens, not embeddings.
                     0,
                     // One sequence.
                     1);

  for (int i = 0; i < (int)toks.size(); i++) {
    llama_batch_add(batch,
                    toks[i],
                    // position
                    num_last + i,
                    // sequence ids. just one sequence supported
                    // for now.
                    { 0 },
                    // no intermediate logits
                    false);
  }

  CHECK(batch.n_tokens != 0);

  // Keep track of the size of the most recent batch,
  // so that we can get logits for it in GetCandidates.
  last_batch_size = batch.n_tokens;

  // PERF: Don't need to get logits if this is part of a large
  // batch that's been split, and we are not on the last one.
  //
  // Only get logits for last token.
  batch.logits[batch.n_tokens - 1] = true;

  // TODO: Any way to get finer-grained progress here?
  if (progress) progress(0, toks.size());
  CHECK(0 == llama_decode(lctx, batch));
  if (progress) progress(toks.size(), toks.size());

  // Notes on kv:
  //   a kv cell knows its position, but is associated with
  //   potentially many sequence ids. (It also has a "delta"
  //   so that it can be shifted. both the position and delta
  //   are moved together)
  //
  // So I think the kv cache is basically storing the sequences.
  //
  // Here's some decent documentation:
  // https://github.com/ggerganov/llama.cpp/pull/3228

  num_last += (int)toks.size();

  llama_batch_free(batch);
}

std::string Context::TokenString(llama_token token) const {
  // Guess 20-character buffer (fits in short string optimization);
  // above 22 we need to allocate. (llama.cpp uses 8)
  std::string result(20, 0);
  const int n_chars = llama_token_to_piece(
      model, token, result.data(), result.size());
  if (n_chars < 0) {
    result.resize(-n_chars);
    int check =
      llama_token_to_piece(model, token,
                           result.data(), result.size());
    CHECK(check == -n_chars);
  } else {
    result.resize(n_chars);
  }

  return std::string(result.data(), result.size());
}

Context::Candidates::Candidates(llama_model *model, llama_context *lctx,
                                int last_batch_size) {
  const int n_vocab = llama_n_vocab(model);
  ltda.data = new llama_token_data[n_vocab];
  ltda.size = n_vocab;
  ltda.sorted = false;
  // this is size n_vocab (just the last token) if
  // params.logits_all is false (XXX assert!)
  const float *logits =
    llama_get_logits_ith(lctx, last_batch_size - 1);
  CHECK(logits != nullptr);
  for (llama_token token_id = 0; token_id < n_vocab; token_id++) {
    float f = logits[token_id];
    // printf("  logits[%d] = %.6f\n", token_id, f);
    ltda.data[token_id] =
      llama_token_data{token_id, f, 0.0f};
  }
}

Context::State Context::SaveState() const {
  State state;
  // We used to serialize the context here, but it seems to
  // not work correctly with the new batched eval? On the
  // other hand, we can now rewind really cheaply.

  /*
    const size_t n_state_size_max = llama_get_state_size(lctx);
    state.llama_state.resize(n_state_size_max);
    const size_t n_state_size_cur =
    llama_copy_state_data(lctx, state.llama_state.data());
    state.llama_state.resize(n_state_size_cur);
    state.llama_state.shrink_to_fit();
  */

  state.num_last = num_last;
  state.last_batch_size = last_batch_size;
  return state;
}

void Context::LoadState(const State &state) {
  // Reset();
  /*
    size_t bytes_read =
    llama_set_state_data(lctx,
    // XXX I think this is morally const, but
    // should check.
    const_cast<uint8_t *>(
    state.llama_state.data()));
    CHECK(bytes_read == state.llama_state.size());
  */
  num_last = state.num_last;
  last_batch_size = state.last_batch_size;

  // XXX at best this is inefficient, but probably just wrong?
  // I think we want to clear after context.n_last, maybe?
  // llama_kv_cache_clear(context.lctx);

  llama_kv_cache_seq_rm(
      lctx,
      // any sequence
      -1,
      // clear from num_last to inf
      num_last, -1);
}

Sampler::Sampler(Context *context,
                 const SamplerParams &params) :
  // The only thing we need from the context is the vocabulary.
  // Probably better if we can avoid keeping a reference.
  context(context),
  params(params),
  vocab_size(context->VocabSize()),
  last_n_tokens(params.repeat_last_n, 0) {
  Reset();
}

const char *Sampler::SampleTypeString(SampleType type) {
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
llama_token Sampler::SampleToken(std::unique_ptr<Candidates> cand) {
  llama_context *lctx = context->lctx;

  switch (params.type) {
  default:
  case SampleType::GREEDY:
    // This doesn't use rng, so we can just call the native
    // sampler.
    return llama_sample_token_greedy(lctx, &cand->ltda);

  case SampleType::MIROSTAT_1:
    llama_sample_temp(lctx, &cand->ltda, params.temp);
    // TODO: Use local rng, which probably means duplicating the
    // code from llama
    return SampleTokenMirostat(
        lctx, &cand->ltda, params.mirostat_tau, params.mirostat_eta,
        params.mirostat_m, &mirostat_mu);

  case SampleType::MIROSTAT_2:
    llama_sample_temp(lctx, &cand->ltda, params.temp);
    // TODO: Use local rng here too.
    return SampleTokenMirostatV2(
        lctx, &cand->ltda, params.mirostat_tau,
        params.mirostat_eta, &mirostat_mu);

  case SampleType::TEMPERATURE:
    llama_sample_top_k(lctx, &cand->ltda, params.top_k, 1);
    llama_sample_tail_free(lctx, &cand->ltda, params.tfs_z, 1);
    llama_sample_typical(lctx, &cand->ltda, params.typical_p, 1);
    llama_sample_top_p(lctx, &cand->ltda, params.top_p, 1);
    llama_sample_temp(lctx, &cand->ltda, params.temp);
    return SampleDistribution(&cand->ltda);
  }
}

// This is a copy of llama_sample_token_mirostat from llama.cpp,
// but using our local RNG. I changed:
//  - Call SampleDistribution instead of llama_sample_token
//  - Remove timing which needs context internals.
llama_token Sampler::SampleTokenMirostat(
    struct llama_context *ctx,
    llama_token_data_array *candidates,
    float tau, float eta, int m, float *mu) {
  GGML_ASSERT(ctx);

  auto N = float(llama_n_vocab(llama_get_model(ctx)));

  llama_sample_softmax(nullptr, candidates);

  // Estimate s_hat using the most probable m tokens
  float s_hat = 0.0;
  float sum_ti_bi = 0.0;
  float sum_ti_sq = 0.0;
  for (size_t i = 0; i < size_t(m - 1) && i < candidates->size - 1; ++i) {
    float t_i = logf(float(i + 2) / float(i + 1));
    float b_i = logf(candidates->data[i].p / candidates->data[i + 1].p);
    sum_ti_bi += t_i * b_i;
    sum_ti_sq += t_i * t_i;
  }
  s_hat = sum_ti_bi / sum_ti_sq;

  // Compute k from the estimated s_hat and target surprise value
  float epsilon_hat = s_hat - 1;
  float k = powf((epsilon_hat * powf(2, *mu)) /
                 (1 - powf(N, -epsilon_hat)), 1 / s_hat);

  // Sample the next word X using top-k sampling
  llama_sample_top_k(nullptr, candidates, int(k), 1);
  // Port note: This is the only thing I changed.
  llama_token X = SampleDistribution(candidates);

  // Compute error as the difference between observed surprise and
  // target surprise value
  size_t X_idx =
    std::distance(candidates->data,
                  std::find_if(candidates->data,
                               candidates->data + candidates->size,
                               [&](const llama_token_data & candidate) {
                                 return candidate.id == X;
                               }));
  float observed_surprise = -log2f(candidates->data[X_idx].p);
  float e = observed_surprise - tau;

  // Update mu using the learning rate and error
  *mu = *mu - eta * e;

  return X;
}

// As above.
llama_token Sampler::SampleTokenMirostatV2(
    struct llama_context *ctx,
    llama_token_data_array *candidates,
    float tau, float eta, float *mu) {

  llama_sample_softmax(ctx, candidates);

  // Truncate the words with surprise values greater than mu
  candidates->size =
    std::distance(candidates->data,
                  std::find_if(candidates->data,
                               candidates->data + candidates->size,
                               [&](const llama_token_data & candidate) {
                                 return -log2f(candidate.p) > *mu;
                               }));

  if (candidates->size == 0) {
    candidates->size = 1;
  }

  // Normalize the probabilities of the remaining words
  llama_sample_softmax(ctx, candidates);

  // Sample the next word X from the remaining words
  llama_token X = SampleDistribution(candidates);

  // Compute error as the difference between observed surprise and
  // target surprise value
  size_t X_idx =
    std::distance(candidates->data,
                  std::find_if(candidates->data,
                               candidates->data + candidates->size,
                               [&](const llama_token_data & candidate) {
                                 return candidate.id == X;
                               }));
  float observed_surprise = -log2f(candidates->data[X_idx].p);
  float e = observed_surprise - tau;

  // Update mu using the learning rate and error
  *mu = *mu - eta * e;

  return X;
}


llama_token Sampler::SampleDistribution(llama_token_data_array *dist) {
  // PERF we need to normalize probabilities, but not sort them.
  llama_sample_softmax(nullptr, dist);

  // Now probabilities sum to 1. So index into that.
  for (;;) {
    float fidx = RandFloat();
    for (int i = 0; i < (int)dist->size; i++) {
      float bucket = dist->data[i].p;
      if (fidx < bucket) {
        // Note: llama records timing info here.
        return dist->data[i].id;
      }
      fidx -= bucket;
    }
    // Mathematically this should always succeed on the first pass,
    // but it is possible that rounding causes it to fail (e.g.
    // when subtracting the bucket). Easiest is to just try again.
  }
}

void Sampler::Observe(llama_token id) {
  last_n_tokens.push_front(id);
  num_last++;
  if (num_last == last_n_tokens.size())
    num_last = last_n_tokens.size();

  // advance matcher state.
  std::string s = context->TokenString(id);
  for (uint8_t c : s) {
    // printf("Before advance [%c]:", c);
    // for (int n : matcher.GetStates()) printf(" %d", n);
    // printf("\n");
    matcher.Advance(c);
    // printf("     after [%c]:", c);
    // for (int n : matcher.GetStates()) printf(" %d", n);
    // printf("\n");
  }
}

void Sampler::ObserveBatch(const std::vector<llama_token> &ids) {
  for (llama_token id : ids) Observe(id);
}

void Sampler::Reset() {
  uint64_t start_seed =
    params.seed < 0 ? (uint64_t)time(nullptr) : (uint64_t)params.seed;

  rng = PCG32(start_seed);
  ResetRegEx();
  // 0 token is <unk>, which I think means invalid.
  // (Or use BOS token?)
  std::fill(last_n_tokens.begin(), last_n_tokens.end(), 0);
  mirostat_mu = 2.0f * params.mirostat_tau;
  num_last = 0;
}

void Sampler::ResetRegEx() {
  nfa = RemoveEpsilon<256>(Parse(params.regex));
  ResetNFA();
}

void Sampler::SetRegEx(const std::string &re) {
  params.regex = re;
  ResetRegEx();
}

void Sampler::SetNFA(NFA<256> nfa_arg) {
  nfa = std::move(nfa_arg);
  ResetNFA();
}

void Sampler::ResetNFA() {
  matcher = NFAMatcher<256>(nfa);
}

bool Sampler::FilterByNFA(Candidates *cands) const {
  // TODO: I think we can actually just shrink the candidates
  // array, which would have other advantages.
  static constexpr float IMPOSSIBLE = -1.0e28f;
  bool any_left = false;
  for (llama_token_data &cand : *cands) {
    CHECK(cand.id >= 0 && cand.id < vocab_size);
    std::string s = context->TokenString(cand.id);
    /*
      printf("Cand " AWHITE("%s") " with score %.4f..\n",
      s.c_str(), cand.logit);
    */

    // Some special tokens (EOS, BOS) are empty. We should not
    // predict them when conforming to a regex!
    // (But this is kind of a hack...)
    if (s.empty()) {
      cand.logit = IMPOSSIBLE;
      continue;
    }


    NFAMatcher<256> mcopy = matcher;
    for (uint8_t c : s) {
      mcopy.Advance(c);
      /*
        printf("..'" AYELLOW("%02x") "=%c'..", c, c);
        if (mcopy.Stuck()) {
        printf(ARED("STUCK"));
        }
      */

      // XXX check stuck in here!

    }
    if (mcopy.Stuck()) {
      cand.logit = IMPOSSIBLE;
    } else {
      any_left = true;
    }
  }
  // printf("XXX exit\n");
  // CHECK(false);
  return any_left;
}

// Apply penalties to candidates.
void Sampler::Penalize(Candidates *cands) const {
  cands->ltda.sorted = false;

  // Save nl logit so that we can restore it if
  const int nl_id = llama_token_nl(context->model);
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

void Sampler::NewRNG() {
  // TODO: There should be some good portable randomness we could use.
  Timer t;
  rng = PCG32(time(nullptr) + rng.Rand64());
}

LLM::LLM(const ContextParams &context_params,
         const SamplerParams &sampler_params) : context(context_params),
                                                sampler(&context,
                                                        sampler_params) {
}

llama_token LLM::Sample() {
  std::unique_ptr<Context::Candidates> candidates = context.GetCandidates();
  if (VERBOSE) {
    printf(ABLUE("Original:") "\n");
    AnsiPrintCandidates(*candidates, 10);
  }
  sampler.Penalize(candidates.get());
  if (VERBOSE) {
    printf(ACYAN("Penalized:") "\n");
    AnsiPrintCandidates(*candidates, 10);
  }
  sampler.FilterByNFA(candidates.get());
  if (VERBOSE) {
    printf(APURPLE("Penalized, filtered:") "\n");
    AnsiPrintCandidates(*candidates, 10);
  }
  return sampler.SampleToken(std::move(candidates));
}

std::string LLM::SampleAndTake() {
  llama_token id = Sample();
  // Commit the token.
  TakeTokenBatch({id});
  return context.TokenString(id);
}

void LLM::TakeTokenBatch(const std::vector<llama_token> &batch,
                         bool progress_bar) {
  /*
  printf("TakeTokenBatch:");
  for (llama_token t : batch) {
    printf(" %d=[%s]", t, context.TokenString(t).c_str());
  }
  printf("\n");
  */
  std::function<void(int, int)> progress;
  Timer take_timer;
  Periodically per(1.0);
  bool progress_bar_ran = false;
  if (progress_bar) {
    progress = [&take_timer, &per, &progress_bar_ran](int n, int d) {
        if (per.ShouldRun()) {
          if (!progress_bar_ran) {
            // Make sure we're on a new line so we don't overwrite
            // anything with the ansi "up".
            printf("\n");
            progress_bar_ran = true;
          }
          printf(ANSI_UP
                 "%s\n",
                 ANSI::ProgressBar(n, d,
                                   "TakeTokenBatch",
                                   take_timer.Seconds()).c_str());
        }
      };
  }

  context.TakeTokenBatch(batch, progress);
  sampler.ObserveBatch(batch);

  if (progress_bar_ran) {
    // Or just remove it?
    printf(ANSI_UP
           "TakeTokenBatch in %s\n",
           ANSI::Time(take_timer.Seconds()).c_str());
  }
}

std::string LLM::GenerateUntil(const std::string &delimiter,
                               int max_length) {
  const auto &[got, success] =
    GenerateUntilEx(delimiter, max_length);
  if (success) return got;
  else return "";
}

std::pair<std::string, bool> LLM::GenerateUntilEx(
    const std::string &delimiter,
    int max_length,
    bool force_delimiter) {
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
    if (pos != std::string::npos) {
      return make_pair(got.substr(0, pos), true);
    }
    if (max_length >= 0 && (int)got.size() > max_length) {
      InsertString(delimiter);
      return make_pair(got, false);
    }
  }
}

std::string LLM::GenerateUntilDone() {
  std::string s;
  while (!sampler.Accepting() && !sampler.Stuck()) {
    s += SampleAndTake();
  }
  return s;
}

void LLM::DoPrompt(const std::string &prompt, bool progress_bar) {
  // tokenize the prompt. This also inserts a leading space because
  // add_bos is true.
  auto toks = context.Tokenize(prompt, true);

  const int n_ctx = llama_n_ctx(context.lctx);

  CHECK((int)toks.size() <= n_ctx - 4)
    << "Prompt too long (" << (int)toks.size() << " tokens). "
    "Context size: " << n_ctx;

  TakeTokenBatch(toks, progress_bar);

  if (VERBOSE) {
    PrintKV();
  }
}

LLM::State LLM::SaveState() const {
  State state;
  state.context_state = context.SaveState();
  state.sampler_copy = sampler;

  if (VERBOSE) {
    printf("SaveState!\n");
    PrintKV();
  }
  return state;
}

void LLM::LoadState(const State &state) {
  if (VERBOSE) {
    printf("LoadState!\n");
    PrintKV();
  }
  context.LoadState(state.context_state);
  sampler = state.sampler_copy;

  if (VERBOSE) {
    printf("After LoadState:\n");
    PrintKV();
  }
}

void LLM::AnsiPrintCandidates(const Candidates &candidates,
                              int maximum) {
  auto IsAscii = [](const std::string &s) {
      for (char c : s) {
        if (c < ' ' || c > '~') return false;
      }
      return true;
    };

  std::vector<std::pair<std::string, float>> toks;
  for (const llama_token_data &tok : candidates) {
    if (tok.id == llama_token_nl(context.model)) {
      toks.emplace_back("\\n", tok.logit);
    } else {
      std::string s = context.TokenString(tok.id);
      if (IsAscii(s)) {
        toks.emplace_back(s, tok.logit);
      } else {
        toks.emplace_back("??", tok.logit);
      }
    }
  }

  std::sort(toks.begin(), toks.end(),
            [](const std::pair<std::string, float> &a,
               const std::pair<std::string, float> &b) {
              return a.second > b.second;
            });
  for (int i = 0;
       (maximum < 0 || i < maximum) && i < (int)toks.size();
       i++) {
    printf("  [%s] %.9f\n", toks[i].first.c_str(), toks[i].second);
  }
}

// Debugging only.
void LLM::PrintKV() const {
  constexpr int NUM_SEQ = 1;
  llama_kv_cache_view kcv = llama_kv_cache_view_init(
      context.lctx, NUM_SEQ);
  llama_kv_cache_view_update(context.lctx, &kcv);

  printf(AWHITE("KV Cache:") "\n");
  printf("  n_cells: %d\n", kcv.n_cells);
  printf("  n_max_seq: %d\n", kcv.n_max_seq);
  printf("  token_count: %d\n", kcv.token_count);
  printf("  used_cells: %d\n", kcv.used_cells);
  printf("  max_contiguous: %d\n", kcv.max_contiguous);
  printf("  max_contiguous_idx: %d\n", kcv.max_contiguous_idx);
  printf("  cells...\n");
  printf("  cells_sequences...\n");

  llama_kv_cache_view_free(&kcv);
}
