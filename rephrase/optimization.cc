
#include "optimization.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_set>
#include <vector>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "context.h"
#include "primop.h"
#include "progress.h"
#include "util.h"
#include "bc.h"

static constexpr bool VERBOSE = true;

using ProgressRecorder = Progress<VERBOSE>;

// TODO:
// Coalesce identical globals
// Standard data-flow stuff

namespace bc {

Optimization::Optimization() {}

void Optimization::SetVerbose(int v) {
  verbose = VERBOSE ? std::max(v, 2) : v;
}

namespace {
struct SimplePass {
  SimplePass(uint64_t opts, ProgressRecorder *progress) :
    opts(opts),
    progress(progress) {}

  void DoFn(const std::string &fname, SymbolicFn *fn) {
    // std::string arg;
    // std::string initial;
    // std::unordered_map<std::string, Block> blocks;

    // PERF: This approach takes a linear number of passes
    // to remove a series of jumps that are dead, because
    // the labels in dead blocks are seen as used until the
    // dead blocks are removed. We can do it in one pass
    // by analyzing a graph, or assuming every block is dead
    // to start, etc.
    std::unordered_set<std::string> used_block_labels;

    // for (auto &[blab, block] : fn->blocks) {

    // }
  }

  void DoProgram(SymbolicProgram *pgm) {
    // std::unordered_map<std::string, SymbolicFn> code;
    // std::unordered_map<std::string, Value> data;

    for (auto &[fname, fn] : pgm->code) {
      DoFn(fname, &fn);
    }

    // Nothing to do to data here.
  }

 private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
};

}  // namespace

SymbolicProgram Optimization::Optimize(const SymbolicProgram &program_in,
                                       uint64_t opts) {
  ProgressRecorder progress;
  SymbolicProgram program = program_in;
  SimplePass simple(opts, &progress);

  do {
    progress.Reset();
    if (VERBOSE) printf(AWHITE("Simple") ".\n");
    simple.DoProgram(&program);

    if (VERBOSE) {
      printf("\n" AYELLOW("After optimization:\n"));
      PrintSymbolicProgram(program);
    }

  } while (progress.MadeProgress());

  return program;
}

}  // namespace bc

