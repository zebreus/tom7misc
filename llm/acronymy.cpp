// Defines sigaction on msys:
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "common.h"
#include "llama.h"
#include "build-info.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))
#include <signal.h>
#include <unistd.h>
#elif defined (_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <signal.h>
#endif

using namespace std;

static console_state con_st;
static llama_context ** g_ctx;

static void sigint_handler(int signo) {
  if (signo == SIGINT) {
    console_cleanup(con_st);
    printf("\n");
    llama_print_timings(*g_ctx);
    _exit(130);
  }
}

static bool IsAscii(const std::string &s) {
  for (char c : s) {
    if (c < ' ' || c > '~') return false;
  }
  return true;
}

static bool AllSpace(const std::string &s) {
  for (char c : s) {
    if (c != ' ') return false;
  }
  return true;
}

static bool IsAlphabetical(const std::string &s) {
  for (char c : s) {
    if (c == ' ') continue;
    if (c >= 'a' && c <= 'z') continue;
    if (c >= 'A' && c <= 'Z') continue;
    return false;
  }
  return true;
}

static inline bool ContainsChar(const std::string &s, char t) {
  for (char c : s)
    if (c == t) return true;
  return false;
}

int main(int argc, char ** argv) {
  gpt_params params;
  params.model = "models/65B/ggml-model-q4_0.bin";
  // params.model = "models/7B/ggml-model-q4_0.bin";
  params.mirostat = 2;

  std::vector<std::string> words = {
    "abysmal",
    "exciting",
    "maniac",
    "applying",
    "violence",
  };

  params.prompt =
    "Definitions as acronyms. Each word of the definition starts with "
    "the corresponding letter of the word being defined. Every word "
    "counts, even short words like \"in\" or \"the.\" Only the first "
    "letter of each word in the definition is considered.\n\n"

    "Examples:\n"
    "Word: fomo\n"
    "Acronym Definition: Fear Of Missing Out\n"
    "Word: distribute\n"
    "Acronym Definition: Deliver Items Systematically To Receiving Individuals By Urgent Truckloads Efficiently\n"
    "Word: path\n"
    "Acronym Definition: Passage Across The Hill\n"
    "Word: moving\n"
    "Acronym Definition: Making Oneself Veer Into Neighboring Geography\n"
    "Word: gap\n"
    "Acronym Definition: Gone Access Path\n"
    "Word: surfeit\n"
    "Acronym Definition: Surplus Undermining Responsible Food Eating In Teatimes\n"
    "Word: yolo\n"
    "Acronym Definition: You Only Live Once\n";

  if (gpt_params_parse(argc, argv, params) == false) {
    return 1;
  }

  // save choice to use color for later
  // (note for later: this is a slightly awkward choice)
  con_st.use_color = params.use_color;
  con_st.multiline_input = params.multiline_input;
  console_init(con_st);
  atexit([]() { console_cleanup(con_st); });

  if (params.n_ctx > 2048) {
    fprintf(stderr, "%s: warning: model does not support "
            "context sizes greater than 2048 tokens (%d specified); "
            "expect poor results\n", __func__, params.n_ctx);
  }

  if (params.seed < 0) {
    params.seed = time(nullptr);
  }

  fprintf(stderr, "%s: seed  = %d\n", __func__, params.seed);

  llama_context * ctx;
  g_ctx = &ctx;

  // load the model and apply lora adapter, if any
  ctx = llama_init_from_gpt_params(params);
  if (ctx == nullptr) {
    fprintf(stderr, "%s: error: unable to load model\n", __func__);
    return 1;
  }

  const int n_ctx = llama_n_ctx(ctx);

  std::vector<llama_token> session_tokens;

  #if 0
  // XXX probably unnecessary
  // number of tokens to keep when resetting context
  if (params.n_keep < 0 || params.n_keep > (int) embd_inp.size() ||
      params.instruct) {
    params.n_keep = (int)embd_inp.size();
  }
  #endif

  // determine newline token
  // XXX llama_token_nl?
  auto llama_token_newline = ::llama_tokenize(ctx, "\n", false);

  fprintf(stderr, "sampling: repeat_last_n = %d, repeat_penalty = %f, "
          "presence_penalty = %f, frequency_penalty = %f, top_k = %d, "
          "tfs_z = %f, top_p = %f, typical_p = %f, temp = %f, "
          "mirostat = %d, mirostat_lr = %f, mirostat_ent = %f\n",
          params.repeat_last_n, params.repeat_penalty,
          params.presence_penalty, params.frequency_penalty, params.top_k,
          params.tfs_z, params.top_p, params.typical_p, params.temp,
          params.mirostat, params.mirostat_eta, params.mirostat_tau);
  fprintf(stderr,
          "generate: n_ctx = %d, n_batch = %d, n_predict = %d, "
          "n_keep = %d\n",
          n_ctx, params.n_batch, params.n_predict, params.n_keep);
  fprintf(stderr, "\n\n");

  // Facts about the vocabulary!
  // Only one token has a newline in it, which is the newline token.
  // Space only appears leading tokens, although there are a number
  // of tokens that consist of only spaces.

  std::vector<bool> starts_space;
  std::vector<bool> all_space;
  // std::vector<bool> is_ascii;
  // Only allowing A-Z, a-z, space.
  std::vector<bool> is_alphabetical;
  // ignores leading space (see above).
  // Only for tokens that consist of letters (with perhaps leading spaces).
  // The first letter will be lowercase (in a-z), or else 0.
  std::vector<char> first_letter;

  {
    const int nv = llama_n_vocab(ctx);
    printf("Vocab size: %d\n", nv);
    for (int id = 0; id < nv; id++) {
      const string s = llama_token_to_str(ctx, id);
      starts_space.push_back(s[0] == ' ');
      all_space.push_back(AllSpace(s));
      bool alpha = IsAlphabetical(s);
      is_alphabetical.push_back(alpha);
      char c = 0;
      if (alpha) {
        int x = 0;
        // OK for this to read the 0 at the end.
        while (s[x] == ' ') x++;
        char cc = s[x] | 32;
        if (cc >= 'a' && cc <= 'z') c = cc;
      }
      first_letter.push_back(c);
    }
  }

  // Print vocabulary stats and exit.
  if (false) {
    int ascii = 0, has_newline = 0, has_space = 0, has_space_inside = 0;
    const int nv = llama_n_vocab(ctx);
    printf("Vocab size: %d\n", nv);
    for (int id = 0; id < nv; id++) {
      const char *sp = llama_token_to_str(ctx, id);
      if (sp == nullptr) {
        printf("Token %d null??\n", id);
        return -1;
      }

      string s = sp;
      if (IsAscii(s)) {
        ascii++;
      }
      if (ContainsChar(s, '\n')) {
        has_newline++;
      }

      if (ContainsChar(s, ' ')) {
        has_space++;

        if (ContainsChar(s.c_str() + 1, ' ')) {
          has_space_inside++;
          // These turn out to be strings that are all spaces.
          printf("%d [%s]\n", id, s.c_str());
        }
      }

      if (false) {
        if (sp == nullptr) {
          printf("%d nullptr\n", id);
        } else {
          std::string s = sp;
          if (IsAscii(s)) {
            printf("%d [%s]\n", id, s.c_str());
          } else {
            printf("%d [?]\n", id);
          }
        }
      }
    }

    printf("Ascii: %d\n"
           "Has newline: %d\n"
           "Has space: %d\n"
           "Non-leading space: %d\n",
           ascii,
           has_newline,
           has_space, has_space_inside);
    return 0;
  }

  // TODO: replace with ring-buffer
  std::vector<llama_token> last_n_tokens(n_ctx);
  // Begin token?
  std::fill(last_n_tokens.begin(), last_n_tokens.end(), 0);

  int n_past             = 0;
  int n_remain           = params.n_predict;
  int n_consumed         = 0;

  std::vector<llama_token> embd;

  // Accept the batch of tokens together. Some processing happens
  // in parallel (e.g. embeddings). Processing in batch increases
  // memory requirements.
  auto TakeTokenBatch = [&](const std::vector<llama_token> &batch) {

      if (llama_eval(ctx, batch.data(), batch.size(),
                     n_past, params.n_threads)) {
        fprintf(stderr, "%s : failed to eval\n", __func__);
        exit(-1);
      }

      n_past += batch.size();
      n_consumed += batch.size();
      for (llama_token t : batch) {
        embd.push_back(t);
        last_n_tokens.erase(last_n_tokens.begin());
        last_n_tokens.push_back(t);
      }

    };

  // Accept the single token.
  auto TakeToken = [&](llama_token t) {
      std::vector<llama_token> batch = {t};
      TakeTokenBatch(batch);
    };

  // Sample from n_vocab logits according to the parameters.
  auto SampleLogits = [&](float *logits) {
      const float   temp            = params.temp;
      const int32_t top_k           = params.top_k <= 0 ? llama_n_vocab(ctx) : params.top_k;
      const float   top_p           = params.top_p;
      const float   tfs_z           = params.tfs_z;
      const float   typical_p       = params.typical_p;
      const int32_t repeat_last_n   = params.repeat_last_n < 0 ? n_ctx : params.repeat_last_n;
      const float   repeat_penalty  = params.repeat_penalty;
      const float   alpha_presence  = params.presence_penalty;
      const float   alpha_frequency = params.frequency_penalty;
      const int     mirostat        = params.mirostat;
      const float   mirostat_tau    = params.mirostat_tau;
      const float   mirostat_eta    = params.mirostat_eta;
      const bool    penalize_nl     = params.penalize_nl;

      auto n_vocab = llama_n_vocab(ctx);

      llama_token id = 0;

      // Apply params.logit_bias map
      for (auto it = params.logit_bias.begin();
           it != params.logit_bias.end(); it++) {
        logits[it->first] += it->second;
      }

      std::vector<llama_token_data> candidates;
      candidates.reserve(n_vocab);
      for (llama_token token_id = 0; token_id < n_vocab; token_id++) {
        candidates.emplace_back(llama_token_data{token_id,
                                                 logits[token_id],
                                                 0.0f});
      }

      llama_token_data_array candidates_p = {
        candidates.data(), candidates.size(), false
      };

      // Apply penalties
      float nl_logit = logits[llama_token_nl()];
      auto last_n_repeat =
        std::min(std::min((int)last_n_tokens.size(), repeat_last_n),
                 n_ctx);
      llama_sample_repetition_penalty(
          ctx, &candidates_p,
          last_n_tokens.data() + last_n_tokens.size() - last_n_repeat,
          last_n_repeat, repeat_penalty);
      llama_sample_frequency_and_presence_penalties(
          ctx, &candidates_p,
          last_n_tokens.data() + last_n_tokens.size() - last_n_repeat,
          last_n_repeat, alpha_frequency, alpha_presence);
      if (!penalize_nl) {
        logits[llama_token_nl()] = nl_logit;
      }

      if (temp <= 0) {
        // Greedy sampling
        id = llama_sample_token_greedy(ctx, &candidates_p);
      } if (mirostat == 1) {
        static float mirostat_mu = 2.0f * mirostat_tau;
        const int mirostat_m = 100;
        llama_sample_temperature(ctx, &candidates_p, temp);
        id = llama_sample_token_mirostat(
            ctx, &candidates_p, mirostat_tau, mirostat_eta,
            mirostat_m, &mirostat_mu);
      } else if (mirostat == 2) {
        static float mirostat_mu = 2.0f * mirostat_tau;
        llama_sample_temperature(ctx, &candidates_p, temp);
        id = llama_sample_token_mirostat_v2(
            ctx, &candidates_p, mirostat_tau,
            mirostat_eta, &mirostat_mu);
      } else {
        // Temperature sampling
        llama_sample_top_k(ctx, &candidates_p, top_k, 1);
        llama_sample_tail_free(ctx, &candidates_p, tfs_z, 1);
        llama_sample_typical(ctx, &candidates_p, typical_p, 1);
        llama_sample_top_p(ctx, &candidates_p, top_p, 1);
        llama_sample_temperature(ctx, &candidates_p, temp);
        id = llama_sample_token(ctx, &candidates_p);
      }

      // add it to the context
      TakeToken(id);
      return id;
    };

  // Start by accepting the prompt. We just do this in one chunk
  // since we know it's reasonable, but breaking into batches of ~512
  // would make sense.
  {
    // Add a space in front of the first character to match OG llama
    // tokenizer behavior
    params.prompt.insert(0, 1, ' ');

    // tokenize the prompt
    auto embd_inp = ::llama_tokenize(ctx, params.prompt, true);

    const int n_ctx = llama_n_ctx(ctx);

    if ((int) embd_inp.size() > n_ctx - 4) {
      fprintf(stderr, "%s: error: prompt is too long (%d tokens, max %d)\n",
              __func__, (int) embd_inp.size(), n_ctx - 4);
      return 1;
    }

    TakeTokenBatch(embd_inp);
  }

  // Now expand acronyms.

  const int n_vocab = llama_n_vocab(ctx);
  for (const string &word : words) {
    {
      string prompt = "Word: " + word + "\nAcronym Definition: ";
      auto prompt_tok = ::llama_tokenize(ctx, prompt, true);
      TakeTokenBatch(prompt_tok);
    }

    string result = "";

    // The word we're currently working on.
    int word_idx = 0;
    // Have we emitted any token for this word? If so,
    // we have output its first letter.
    bool in_word = false;
    for (;;) {
      // this is size n_vocab (just the last token) if
      // params.logits_all is false (XXX assert!)
      auto logits = llama_get_logits(ctx);

      int rejected = 0;
      static constexpr float IMPOSSIBLE = -1.0e28f;
      static constexpr bool FILTER_WORDS = true;
      if (FILTER_WORDS) {
        // down-weight illegal tokens.
        const bool final_word = word_idx == (int)word.size() - 1;
        for (int id = 0; id < n_vocab; id++) {

          // Never allow end of stream (although we could just
          // treat this like newline?)
          if (id == llama_token_eos()) {
            logits[id] = IMPOSSIBLE;
            rejected++;
            continue;
          }

          // Allow newline only if we are inside the last word.
          if (id == llama_token_nl()) {
            if (in_word && final_word)
              continue;

            logits[id] = IMPOSSIBLE;
            rejected++;
            continue;
          }

          // Can we just use -inf?
          if (!is_alphabetical[id]) {
            logits[id] = IMPOSSIBLE;
            rejected++;
            continue;
          }

          // If we're not inside the word yet, then this
          // token needs to continue the current word (or
          // start the next one).
          if (in_word) {
            // OK for the word to be all spaces; this would
            // just start a new word. (Unless this is the
            // final word).
            if (all_space[id]) {
              if (final_word) {
                logits[id] = IMPOSSIBLE;
                rejected++;
              }
              continue;
            }

            // Similarly, OK for the token to start a new word,
            // as long as it has the right character.
            if (starts_space[id]) {
              if (final_word || first_letter[id] != word[word_idx + 1]) {
                rejected++;
                logits[id] = IMPOSSIBLE;
                continue;
              }
            }

            // Otherwise, any token is okay.

          } else {
            // Next token must start a word. So don't
            // allow spaces.
            if (all_space[id]) {
              rejected++;
              logits[id] = IMPOSSIBLE;
              continue;
            }

            // Similarly, we don't allow multiple spaces between words.
            if (starts_space[id]) {
              rejected++;
              logits[id] = IMPOSSIBLE;
              continue;
            }

            // And the token has to start with the right letter.
            if (first_letter[id] != word[word_idx]) {
              rejected++;
              logits[id] = IMPOSSIBLE;
              continue;
            }
          }
        }
      }

      static constexpr bool SHOW_TOKENS = true;
      if (SHOW_TOKENS) {
        std::vector<std::pair<string, float>> toks;
        static constexpr bool PRINT_ALL = false;
        printf("Rejected %d. Remaining: ", rejected);
        for (int id = 0; id < n_vocab; id++) {
          if (logits[id] > IMPOSSIBLE) {
            if (id == llama_token_nl()) {
              if (PRINT_ALL) printf("[\\n] ");
              toks.emplace_back("\\n", logits[id]);
            } else {
              string s = llama_token_to_str(ctx, id);
              if (IsAscii(s)) {
                if (PRINT_ALL) printf("[%s] ", s.c_str());
                toks.emplace_back(s, logits[id]);
              } else {
                if (PRINT_ALL) printf("[??] ");
                toks.emplace_back("??", logits[id]);
              }
            }
          }
        }
        if (!PRINT_ALL) {
          printf("\n");
          std::sort(toks.begin(), toks.end(),
                    [](const std::pair<string, float> &a,
                       const std::pair<string, float> &b) {
                      return a.second > b.second;
                    });
          for (int i = 0; i < 50 && i < (int)toks.size(); i++) {
            printf("  [%s] %.9f\n", toks[i].first.c_str(), toks[i].second);
          }
        } else {
          printf("\n");
        }
      }

      // Sample it (and take the token).
      int id = SampleLogits(logits);

      // Advance the state.
      if (id == llama_token_nl()) {
        printf("Done. %s = [%s]\n", word.c_str(), result.c_str());
        break;
      }

      result += llama_token_to_str(ctx, id);

      if (all_space[id]) {
        in_word = false;
        word_idx++;
      } else if (starts_space[id]) {
        in_word = true;
        word_idx++;
      } else {
        // just continuing a word.
        in_word = true;
      }

      printf("%s = [%s] (tok %d in %c idx %d rej %d)\n",
             word.c_str(), result.c_str(), id, in_word ? 'Y' : 'N',
             word_idx, rejected);
    }

  }

  llama_print_timings(ctx);
  llama_free(ctx);

  return 0;
}
