
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

// Inlines blocks.
struct InlinePass {
  InlinePass(uint64_t opts, ProgressRecorder *progress) :
    opts(opts),
    progress(progress) {}

  void DoFn(const std::string &fname, SymbolicFn *fn) {
    // First, count uses.
    std::unordered_map<std::string, int> uses;
    for (const auto &[block_name, block] : fn->blocks) {
      for (const Inst &inst : block.insts) {
        if (const inst::SymbolicIf *iff =
            std::get_if<inst::SymbolicIf>(&inst)) {
          uses[iff->true_lab]++;
        } else if (const inst::SymbolicJump *jmp =
                   std::get_if<inst::SymbolicJump>(&inst)) {
          uses[jmp->lab]++;
        } else {
          OnlySymbolic(inst);
        }
      }
    }

    if (VERBOSE) {
      for (const auto &[lab, num] : uses) {
        printf("Label " AYELLOW("%s") " used " AWHITE("%d") " time(s)\n",
               lab.c_str(), num);
      }
    }

    // Now for any unconditional jump, we can replace it with the
    // code of the block (if appropriate).
    std::unordered_set<std::string> drop;
    for (auto &[block_name, block] : fn->blocks) {
      // These blocks are effectively removed. We don't want to
      // process them, because they could cause us to drop other
      // blocks that we actually still need. (This would happen
      // if A jumps to B, and B jumps to C. If we inline B into
      // A, then we don't want to inline C into the dying B;
      // then the inlined jump in A has no target.)
      if (drop.contains(block_name))
        continue;

      for (int inst_idx = 0;
           inst_idx < (int)block.insts.size();
           inst_idx++) {
        const Inst &inst = block.insts[inst_idx];
        if (const inst::SymbolicJump *jmp =
            std::get_if<inst::SymbolicJump>(&inst)) {
          auto uit = uses.find(jmp->lab);
          CHECK(uit != uses.end()) << "Populated above.";
          auto bit = fn->blocks.find(jmp->lab);
          CHECK(bit != fn->blocks.end()) << "Jump to unknown label?";

          // Can relax this condition a bit, but it becomes non-conservative.
          // If it's something like "move local1, local2; ret" (which would
          // be common for sumcase and if joins) then we should probably
          // inline it; we can then rewrite the move to the local name and
          // just return.
          if (block_name != jmp->lab &&
              (uit->second == 1 || bit->second.insts.size() == 1)) {
            // Since we're dropping the instruction, copy the string label.
            std::string lab = jmp->lab;
            // If we're inlining the last use, mark it as dead so that
            // we don't waste any time processing it in future passes.
            //
            // Note that while the initial block can be inlined (would
            // be weird, but could happen), it cannot be dropped.
            if (uit->second == 1 && lab != fn->initial) {
              if (VERBOSE) {
                printf("  Add " AYELLOW("%s") " to drop.\n", lab.c_str());
              }
              drop.insert(lab);
            }

            // In any case, insert the instructions here. We can drop
            // the tail, which is unreachable.
            CHECK(inst_idx < (int)block.insts.size());
            block.insts.resize(inst_idx);
            const Block &other = bit->second;
            for (const Inst &oinst : other.insts) {
              block.insts.push_back(oinst);
            }
            progress->Record("inlined block");
            if (VERBOSE) {
              printf("  Inlined " AYELLOW("%s") " into " AYELLOW("%s") "\n",
                     lab.c_str(), block_name.c_str());

              printf(" ====== resulting block " AYELLOW("%s") " =====\n",
                     block_name.c_str());
              PrintBlock(block);
            }
            // XXX Actually, we can keep processing the block...
            break;
          }
        }
      }
    }

    if (VERBOSE) {
      SymbolicProgram tmp;
      tmp.code = {{fname, *fn}};
      printf("=================== rewritten blocks =============\n");
      PrintSymbolicProgram(tmp);
    }

    if (!drop.empty()) {
      progress->Record("dropped last use of block");

      std::unordered_map<std::string, Block> new_blocks;
      for (auto &[lab, block] : fn->blocks) {
        if (!drop.contains(lab)) {
          new_blocks[lab] = std::move(block);
        } else {
          if (VERBOSE) {
            printf("  drop " AYELLOW("%s") "\n", lab.c_str());
          }
        }
      }
      fn->blocks = std::move(new_blocks);
    }
  }

  void DoProgram(SymbolicProgram *pgm) {
    if (opts & Optimization::O_INLINE_BLOCK) {
      // Rewrites code in place.
      for (auto &[fname, fn] : pgm->code) {
        if (VERBOSE) {
          printf("do " APURPLE("%s") "\n", fname.c_str());
        }
        DoFn(fname, &fn);
      }
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
  InlinePass inline_pass(opts, &progress);

  do {
    progress.Reset();

    if (VERBOSE) printf(AWHITE("Dead") ".\n");
    dead.DoProgram(&program);

    if (VERBOSE) {
      printf(AWHITE("Inline") ".\n");
      PrintSymbolicProgram(program);
    }
    inline_pass.DoProgram(&program);

    if (VERBOSE) {
      printf("\n" AYELLOW("After optimization:\n"));
      PrintSymbolicProgram(program);
    }

  } while (progress.MadeProgress());

  return program;
}

}  // namespace bc

