
#include "optimization.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
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

static constexpr bool VERBOSE = false;

using ProgressRecorder = Progress<VERBOSE>;

// TODO:
// Coalesce identical globals
// Standard data-flow stuff

namespace bc {

Optimization::Optimization() {}

void Optimization::SetVerbose(int v) {
  verbose = VERBOSE ? std::max(v, 2) : v;
}

// Assert that an instruction is symbolic.
static void OnlySymbolic(const Inst &inst) {
  if (const inst::If *iff = std::get_if<inst::If>(&inst)) {
    LOG(FATAL) << "Saw non-symbolic IF instruction in symbolic program.";
  } else if (const inst::Jump *jump = std::get_if<inst::Jump>(&inst)) {
    LOG(FATAL) << "Saw non-symbolic JUMP instruction in symbolic program.";
  }
}

namespace {
struct DeadPass {
  DeadPass(uint64_t opts, ProgressRecorder *progress) :
    opts(opts),
    progress(progress) {}

  void DoFn(
      std::unordered_set<std::string> *used_data,
      std::unordered_set<std::string> *used_functions,
      const std::string &fname, SymbolicFn *fn) {

    std::unordered_map<std::string, Block> new_blocks;

    // We process only the blocks that are referenced,
    // starting with the intial one.
    std::vector<std::string> todo = {fn->initial};
    std::unordered_set<std::string> seen = {fn->initial};
    auto AddToDo = [&seen, &todo](const std::string &lab) {
        if (!seen.contains(lab)) {
          todo.push_back(lab);
          seen.insert(lab);
        }
      };

    // Do all blocks if the optimization is not enabled.
    if ((opts & Optimization::O_DEAD_BLOCK) == 0) {
      for (const auto &[name, block_] : fn->blocks) {
        AddToDo(name);
      }
    }

    while (!todo.empty()) {
      std::string block_lab = std::move(todo.back());
      todo.pop_back();

      auto bit = fn->blocks.find(block_lab);
      CHECK(bit != fn->blocks.end()) << "Block " << block_lab << " is "
        "missing from the function " << fname << "?";
      const Block &old_block = bit->second;

      Block new_block;
      for (int inst_idx = 0;
           inst_idx < (int)old_block.insts.size();
           inst_idx++) {
        const Inst &inst = old_block.insts[inst_idx];

        new_block.insts.push_back(inst);

        if (const inst::Call *call = std::get_if<inst::Call>(&inst)) {
          used_functions->insert(call->f);
        } else if (const inst::Load *load = std::get_if<inst::Load>(&inst)) {
          used_data->insert(load->global);
        } else if (const inst::Save *save = std::get_if<inst::Save>(&inst)) {
          // TODO: We treat a save as "used" for now, but since it is only
          // initializing the global, we should probably remove it if
          // it's the only mention.
          used_data->insert(save->global);
        } else if (const inst::Fail *fail = std::get_if<inst::Fail>(&inst)) {
          if ((opts & Optimization::O_DEAD_INST) &&
              inst_idx < (int)old_block.insts.size() - 1) {
            progress->Record("dropped instructions after fail");
            break;
          }
        } else if (const inst::SymbolicIf *iff =
                   std::get_if<inst::SymbolicIf>(&inst)) {
          AddToDo(iff->true_lab);
        } else if (const inst::SymbolicJump *jmp =
                   std::get_if<inst::SymbolicJump>(&inst)) {
          AddToDo(jmp->lab);
          if ((opts & Optimization::O_DEAD_INST) &&
              inst_idx < (int)old_block.insts.size() - 1) {
            progress->Record("dropped instructions after fail");
            break;
          }
        } else {
          OnlySymbolic(inst);
          // These instructions don't use data, code, or function labels:
          // Triops, Binops, Unops, Ret, Allocvec, Setvec, Getvec,
          // Alloc, Copy, Setlabel, Getlabel, Deletelabel, Haslabel
          // bind,
        }
      }

      new_blocks[block_lab] = std::move(new_block);
    }

    if (new_blocks.size() < fn->blocks.size()) {
      progress->Record("dropped unreferenced blocks");
    }

    fn->blocks = std::move(new_blocks);
  }

  void DoProgram(SymbolicProgram *pgm) {
    std::unordered_set<std::string> used_data;
    std::unordered_set<std::string> used_functions;

    // Rewrites code in place. Populates the used_data and
    // used_functions maps.
    for (auto &[fname, fn] : pgm->code) {
      if (VERBOSE) {
        printf("do " APURPLE("%s") "\n", fname.c_str());
      }
      DoFn(&used_data, &used_functions, fname, &fn);
    }

    // TODO: Drop unused functions!

    if (opts & Optimization::O_DEAD_DATA) {
      std::unordered_map<std::string, Value> new_data;
      for (auto &[data_lab, value] : pgm->data) {
        if (used_data.contains(data_lab)) {
          new_data[data_lab] = std::move(value);
        }
      }
      if (new_data.size() < pgm->data.size()) {
        progress->Record("dropped unreferenced data");
      }
      pgm->data = std::move(new_data);
    }
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
  DeadPass dead(opts, &progress);

  do {
    progress.Reset();
    if (VERBOSE) printf(AWHITE("Dead") ".\n");
    dead.DoProgram(&program);

    if (VERBOSE) {
      printf("\n" AYELLOW("After optimization:\n"));
      PrintSymbolicProgram(program);
    }

  } while (progress.MadeProgress());

  return program;
}

}  // namespace bc

