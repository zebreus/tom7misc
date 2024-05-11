
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
#include "progress.h"
#include "bc.h"

static constexpr int VERBOSE = 1;

using ProgressRecorder = Progress<VERBOSE != 0>;

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
struct PeepholePass {
  // static constexpr bool VERBOSE = true;

  PeepholePass(uint64_t opts, ProgressRecorder *progress) :
    opts(opts),
    progress(progress) {}

  void DoFn(const std::string &fname, SymbolicFn *fn) {
    for (auto &[block_name, block] : fn->blocks) {
      Block new_block;
      new_block.insts.reserve(block.insts.size());
      for (int idx = 0; idx < (int)block.insts.size(); idx++) {
        const Inst &inst1 = block.insts[idx];

        // Instruction pairs.
        if (idx < (int)block.insts.size() - 1) {
          const Inst &inst2 = block.insts[idx + 1];

          if (opts & Optimization::O_TAIL_CALL) {
            const inst::Call *call = std::get_if<inst::Call>(&inst1);
            const inst::Ret *ret = std::get_if<inst::Ret>(&inst2);

            if (call != nullptr && ret != nullptr &&
                ret->arg == call->out) {
              progress->Record("make tail call");
              if (VERBOSE) {
                printf("  Tail call to " APURPLE("%s") "\n",
                       call->f.c_str());
              }
              new_block.insts.push_back(Inst{inst::TailCall{
                  .f = call->f,
                  .arg = call->arg,
                  }});

              continue;
            }
          }
        }

        // If we didn't 'continue' above, then we pass through the
        // instruction.
        new_block.insts.push_back(inst1);
      }

      block = std::move(new_block);
    }

  }

  void DoProgram(SymbolicProgram *pgm) {
    for (auto &[fname, fn] : pgm->code) {
      if (VERBOSE) {
        printf("do " APURPLE("%s") "\n", fname.c_str());
      }
      DoFn(fname, &fn);
    }
  }

 private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
};

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
            progress->Record("dropped instructions after jump");
            break;
          }

        } else if (const inst::TailCall *tail_call =
                   std::get_if<inst::TailCall>(&inst)) {
          if ((opts & Optimization::O_DEAD_INST) &&
              inst_idx < (int)old_block.insts.size() - 1) {
            progress->Record("dropped instructions after tail call");
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

#define AGLOBAL_LAB(s) AFGCOLOR(200, 160, 40, s)

// Coalesces global data that are exactly the same.
// TODO: We can also coalesce identical blocks, although
// here we want to be a bit smarter (e.g. not caring
// about the names of temporaries if they are only used
// within the block).
struct CoalescePass {
  CoalescePass(uint64_t opts, ProgressRecorder *progress) :
    opts(opts),
    progress(progress) {}

  void DoProgram(SymbolicProgram *pgm) {
    if (opts & Optimization::O_COALESCE_DATA) {
      // Invert the data map so we get the list of symbols that
      // have that same value.
      std::unordered_map<Value, std::vector<std::string>,
                         ValueHash, ValueEq> idata;

      for (const auto &[s, v] : pgm->data) {
        idata[v].push_back(s);
      }

      // Now for any value that has multiple names, pick one of
      // them. The keys in the renamed map are renamed to the
      // canonical name (the canonical name is not included) and
      // then we can delete the keys.
      std::unordered_map<std::string, std::string> renamed;
      for (auto &[v, names] : idata) {
        if (names.size() > 1) {
          std::sort(names.begin(), names.end());
          const std::string &canon = names[0];
          if (renamed.empty()) {
            progress->Record("Coalesce identical data");
          }
          for (int i = 1; i < (int)names.size(); i++) {
            if (VERBOSE) {
              printf("  " AGLOBAL_LAB("%s") " => " AGREEN("%s") "\n",
                     names[i].c_str(), canon.c_str());
            }
            renamed[names[i]] = canon;
          }
        }
      }

      if (!renamed.empty()) {
        // Rename references in the code.
        for (auto &[fname, fn] : pgm->code) {
          if (VERBOSE) {
            printf("do " APURPLE("%s") "\n", fname.c_str());
          }
          DoFn(renamed, fname, &fn);
        }

        // We could let the dead code pass clean up after us, although it
        // is better to just delete them here (we know which ones are
        // dead), and this makes sure that this pass is actually
        // conservative if dead code is not enabled.
        std::unordered_map<std::string, Value> new_data;
        for (auto &[name, value] : pgm->data) {
          if (renamed.contains(name)) {
            // Drop it.
            if (VERBOSE) {
              printf("  dropped now-unused " AORANGE("%s") "\n", name.c_str());
            }
          } else {
            new_data[std::move(name)] = std::move(value);
          }
        }
        pgm->data = std::move(new_data);
      }

    }
  }

  void DoFn(const std::unordered_map<std::string, std::string> &renamed,
            const std::string &fname,
            SymbolicFn *fn) {
    // In place.
    for (auto &[block_name, block] : fn->blocks) {
      for (Inst &inst : block.insts) {

        if (inst::Load *load = std::get_if<inst::Load>(&inst)) {
          auto rit = renamed.find(load->global);
          if (rit != renamed.end()) {
            load->global = rit->second;
          }
        } else if (inst::Save *save = std::get_if<inst::Save>(&inst)) {
          auto rit = renamed.find(save->global);
          if (rit != renamed.end()) {
            save->global = rit->second;
          }
        }
      }
    }
  }

 private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
};


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

template<class T>
struct Dataflow {
  // Key: Basic block label
  // Value: T for each index.
  std::unordered_map<std::string, std::vector<T>> state;

  Dataflow(const SymbolicFn &fn) {
    for (const auto &[name, block] : fn.blocks) {
      state[name].resize(block.insts.size());
    }
  }
};

// Computes dataflow: What locals are used?
//
// What we do here is compute, for every instruction in a function,
// the set of locals that are read (in or below the instruction)
// without being overwritten first.
struct DataflowPass {
  DataflowPass(uint64_t opts, ProgressRecorder *progress) :
    opts(opts),
    progress(progress) {}

  void DoFn(const std::string &fname, SymbolicFn *fn) {
    // PERF We know the full gamut ahead of time. Use a bit vector!
    Dataflow<std::unordered_set<std::string>> read_before_write(*fn);

    // Initialize with instructions that read their inputs.
    for (const auto &[name, block] : fn->blocks) {
      std::vector<std::unordered_set<std::string>> *dv =
        &read_before_write.state[name];
      for (int i = 0; i < (int)block.insts.size(); i++) {
        const Inst &inst = block.insts[i];

        // TODO: Insert the changed indices into todo set.
        if (const inst::Triop *triop = std::get_if<inst::Triop>(&inst)) {
          (*dv)[i].insert(triop->arg1);
          (*dv)[i].insert(triop->arg2);
          (*dv)[i].insert(triop->arg3);
        } else if (const inst::Binop *binop = std::get_if<inst::Binop>(&inst)) {
          (*dv)[i].insert(binop->arg1);
          (*dv)[i].insert(binop->arg2);
        } else if (const inst::Unop *unop = std::get_if<inst::Unop>(&inst)) {
          (*dv)[i].insert(unop->arg);
        } else if (const inst::Call *call = std::get_if<inst::Call>(&inst)) {
          (*dv)[i].insert(call->arg);
        } else if (const inst::TailCall *tail_call =
                   std::get_if<inst::TailCall>(&inst)) {
          (*dv)[i].insert(tail_call->arg);
        } else if (const inst::Ret *ret = std::get_if<inst::Ret>(&inst)) {
          (*dv)[i].insert(ret->arg);
        } else if (const inst::If *iff = std::get_if<inst::If>(&inst)) {
          (*dv)[i].insert(iff->cond);
        } else if (const inst::AllocVec *allocvec =
                   std::get_if<inst::AllocVec>(&inst)) {
          // nothing
        } else if (const inst::SetVec *setvec =
                   std::get_if<inst::SetVec>(&inst)) {
          (*dv)[i].insert(setvec->vec);
          (*dv)[i].insert(setvec->idx);
          (*dv)[i].insert(setvec->arg);
        } else if (const inst::GetVec *getvec =
                   std::get_if<inst::GetVec>(&inst)) {
          (*dv)[i].insert(getvec->vec);
          (*dv)[i].insert(getvec->idx);
        } else if (const inst::Alloc *alloc = std::get_if<inst::Alloc>(&inst)) {
          // nothing
        } else if (const inst::Copy *copy = std::get_if<inst::Copy>(&inst)) {
          (*dv)[i].insert(copy->obj);
        } else if (const inst::SetLabel *setlabel =
                   std::get_if<inst::SetLabel>(&inst)) {
          (*dv)[i].insert(setlabel->obj);
          (*dv)[i].insert(setlabel->arg);
        } else if (const inst::GetLabel *getlabel =
                   std::get_if<inst::GetLabel>(&inst)) {
          (*dv)[i].insert(getlabel->obj);
        } else if (const inst::DeleteLabel *deletelabel =
                   std::get_if<inst::DeleteLabel>(&inst)) {
          (*dv)[i].insert(deletelabel->obj);
        } else if (const inst::HasLabel *haslabel =
                   std::get_if<inst::HasLabel>(&inst)) {
          (*dv)[i].insert(haslabel->obj);
        } else if (const inst::Bind *bind = std::get_if<inst::Bind>(&inst)) {
          (*dv)[i].insert(bind->arg);
        } else if (const inst::Load *load = std::get_if<inst::Load>(&inst)) {
          // Nothing
        } else if (const inst::Save *save = std::get_if<inst::Save>(&inst)) {
          (*dv)[i].insert(save->arg);
        } else if (const inst::Jump *jump = std::get_if<inst::Jump>(&inst)) {
          // Nothing
        } else if (const inst::Fail *fail = std::get_if<inst::Fail>(&inst)) {
          (*dv)[i].insert(fail->arg);
        } else if (const inst::Note *note = std::get_if<inst::Note>(&inst)) {
          // Nothing
        } else if (const inst::SymbolicIf *iff =
                   std::get_if<inst::SymbolicIf>(&inst)) {
          (*dv)[i].insert(iff->cond);
        } else if (const inst::SymbolicJump *jmp =
                   std::get_if<inst::SymbolicJump>(&inst)) {
          // Nothing
        } else {
          LOG(FATAL) << "Unhandled instruction in DataflowPass.";
        }
      }
    }

    // TODO: propagate, updating the workset

    // TODO: delete effectless instructions whose values are not needed
  }

  void DoProgram(SymbolicProgram *pgm) {
    if (opts & Optimization::O_DEAD_LOCALS) {
      // Rewrites code in place. Each function is processed
      // independently (and we could do this in parallel?)
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


  constexpr uint64_t DISABLED_OPTIMIZATIONS = O_TAIL_CALL | O_DEAD_BLOCK |

    O_DEAD_DATA | O_INLINE_BLOCK | O_DEAD_INST
    ;

  /*
  static constexpr uint64_t O_DEAD_INST = 1ULL << 1;
  static constexpr uint64_t O_DEAD_BLOCK = 1ULL << 2;
  static constexpr uint64_t O_DEAD_DATA = 1ULL << 3;
  static constexpr uint64_t O_INLINE_BLOCK = 1ULL << 4;
  static constexpr uint64_t O_TAIL_CALL = 1ULL << 5;
  static constexpr uint64_t O_COALESCE_DATA = 1ULL << 6;
  static constexpr uint64_t O_DEAD_LOCALS = 1ULL << 7;
  */

  opts &= ~DISABLED_OPTIMIZATIONS;

  ProgressRecorder progress;
  SymbolicProgram program = program_in;
  DeadPass dead(opts, &progress);
  PeepholePass peep(opts, &progress);
  InlinePass inline_pass(opts, &progress);
  CoalescePass coalesce(opts, &progress);
  DataflowPass dataflow(opts, &progress);

  int passes = 0;
  do {
    if (VERBOSE > 0) {
      printf(ABGCOLOR(200, 200, 200, AFGCOLOR(0, 0, 0, " Pass %d ")) "\n", passes);
    }
    progress.Reset();

    if (VERBOSE > 0) printf(AWHITE("Dead") ".\n");
    dead.DoProgram(&program);

    if (VERBOSE > 0) printf(AWHITE("Peephole") ".\n");
    peep.DoProgram(&program);

    if (VERBOSE > 0) {
      printf(AWHITE("Inline") ".\n");
      if (VERBOSE > 2) {
        PrintSymbolicProgram(program);
      }
    }
    inline_pass.DoProgram(&program);

    if (VERBOSE > 0) {
      printf(AWHITE("Coalesce") ".\n");
      if (VERBOSE > 2) {
        PrintSymbolicProgram(program);
      }
    }
    coalesce.DoProgram(&program);

    if (VERBOSE > 0) {
      printf(AWHITE("Dataflow") ".\n");
      if (VERBOSE > 2) {
        PrintSymbolicProgram(program);
      }
    }
    dataflow.DoProgram(&program);

    if (VERBOSE > 1) {
      printf("\n" AYELLOW("After optimization:\n"));
      PrintSymbolicProgram(program);
    }

    passes++;
  } while (progress.MadeProgress());

  return program;
}

}  // namespace bc

