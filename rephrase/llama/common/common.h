// Various helper functions and utilities

#ifndef _REPHRASE_LLAMA_COMMON_COMMON_H
#define _REPHRASE_LLAMA_COMMON_COMMON_H

#include "llama.h"

#include "sampling.h"
#include <cstdio>

#define LOG_NO_FILE_LINE_FUNCTION
#include "log.h"

#include <cmath>
#include <string>
#include <vector>
#include <random>
#include <thread>
#include <unordered_map>
#include <tuple>

#ifdef _WIN32
#define DIRECTORY_SEPARATOR '\\'
#else
#define DIRECTORY_SEPARATOR '/'
#endif // _WIN32

#define die(msg) do {                  \
    fputs("error: " msg "\n", stderr); \
    exit(1); \
  } while (0)
#define die_fmt(fmt, ...) do {                        \
    fprintf(stderr, "error: " fmt "\n", __VA_ARGS__); \
    exit(1);                                          \
  } while (0)

#define print_build_info() do {                                         \
    fprintf(stderr, "%s: build = %d (%s)\n", __func__,                  \
            LLAMA_BUILD_NUMBER, LLAMA_COMMIT);                          \
    fprintf(stderr, "%s: built with %s for %s\n",                       \
            __func__, LLAMA_COMPILER, LLAMA_BUILD_TARGET);              \
  } while(0)

#define DEFAULT_MODEL_PATH "models/7B/ggml-model-f16.gguf"

// build info
extern int LLAMA_BUILD_NUMBER;
extern char const *LLAMA_COMMIT;
extern char const *LLAMA_COMPILER;
extern char const *LLAMA_BUILD_TARGET;

struct llama_control_vector_load_info;

// Batch utils

void llama_batch_clear(struct llama_batch & batch);

void llama_batch_add(
                 struct llama_batch & batch,
                        llama_token   id,
                          llama_pos   pos,
    const std::vector<llama_seq_id> & seq_ids,
                               bool   logits);

//
// Vocab utils
//

// tokenizes a string into a vector of tokens
// should work similar to Python's `tokenizer.encode`
std::vector<llama_token> llama_tokenize(
  const struct llama_context * ctx,
           const std::string & text,
                        bool   add_special,
                        bool   parse_special = false,
                        bool   add_leading_space = true);

std::vector<llama_token> llama_tokenize(
    const struct llama_model * model,
           const std::string & text,
                        bool   add_special,
                        bool   parse_special = false,
                        bool   add_leading_space = true);

// tokenizes a token into a piece, optionally renders special/control tokens
// should work similar to Python's `tokenizer.id_to_piece`
std::string llama_token_to_piece(
        const struct llama_context * ctx,
                       llama_token   token,
                       bool          special = true);

//
// Embedding utils
//

void llama_embd_normalize(const float * inp, float * out, int n);

float llama_embd_similarity_cos(const float * embd1, const float * embd2, int n);

//
// Control vector utils
//

struct llama_control_vector_data {
    int n_embd;

    // stores data for layers [1, n_layer] where n_layer = data.size() / n_embd
    std::vector<float> data;
};

struct llama_control_vector_load_info {
    float strength;

    std::string fname;
};

//
// Split utils
//
static const char * const LLM_KV_SPLIT_NO            = "split.no";
static const char * const LLM_KV_SPLIT_COUNT         = "split.count";
static const char * const LLM_KV_SPLIT_TENSORS_COUNT = "split.tensors.count";

#endif
