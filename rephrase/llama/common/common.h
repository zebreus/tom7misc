// Various helper functions and utilities

#ifndef _REPHRASE_LLAMA_COMMON_COMMON_H
#define _REPHRASE_LLAMA_COMMON_COMMON_H

#include "llama.h"

#include <string>
#include <vector>

// build info
extern int LLAMA_BUILD_NUMBER;
extern char const *LLAMA_COMMIT;
extern char const *LLAMA_COMPILER;
extern char const *LLAMA_BUILD_TARGET;

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
// Split utils
//
inline constexpr const char * const LLM_KV_SPLIT_NO            = "split.no";
inline constexpr const char * const LLM_KV_SPLIT_COUNT         = "split.count";
inline constexpr const char * const LLM_KV_SPLIT_TENSORS_COUNT = "split.tensors.count";

#endif
