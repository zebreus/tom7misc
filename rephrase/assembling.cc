
#include "assembling.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <utility>

#include "bc.h"
#include "base/logging.h"

namespace bc {

Assembling::Assembling() {}
Assembling::~Assembling() {}

void Assembling::SetVerbose(int verbose_in) {
  verbose = verbose_in;
}

std::pair<std::string, std::vector<Inst>>
Assembling::AssembleFn(const SymbolicFn &fn) {
  // The index of this label for jumps.
  std::unordered_map<std::string, int> locs;
  std::vector<Inst> insts;
  auto OutputBlock = [&locs, &insts](const std::string &name,
                                     const Block &block) {
      CHECK(!locs.contains(name)) << name;
      locs[name] = (int)insts.size();
      for (const Inst &inst : block.insts) {
        insts.push_back(inst);
      }
    };

  auto GetIndex = [&locs](const std::string &name) -> int {
      const auto it = locs.find(name);
      CHECK(it != locs.end()) << "Invalid program: The symbolic label "
                              << name << " does not refer to any block "
        "in this function?";
      return it->second;
    };

  // Need to start with the initial block.
  std::vector<std::pair<std::string, const Block *>> blocks;
  blocks.reserve(fn.blocks.size());
  for (const auto &[name, block] : fn.blocks) {
    if (name == fn.initial) {
      OutputBlock(name, block);
    } else {
      blocks.emplace_back(name, &block);
    }
  }
  std::sort(blocks.begin(), blocks.end(),
            [](const auto &a, const auto &b) {
              return a.first < b.first;
            });

  // Now output the rest in sorted order.
  for (const auto &[name, block] : blocks) {
    OutputBlock(name, *block);
  }

  // Now rewrite symbolic references in the linearized
  // instruction stream.
  for (int i = 0; i < (int)insts.size(); i++) {
    const Inst &inst = insts[i];
    if (const inst::SymbolicIf *iff =
        std::get_if<inst::SymbolicIf>(&inst)) {
      insts[i] = Inst(inst::If{
          .cond = iff->cond,
          .true_idx = GetIndex(iff->true_lab),
        });
    } else if (const inst::SymbolicJump *jmp =
               std::get_if<inst::SymbolicJump>(&inst)) {
      insts[i] = Inst(inst::Jump{.idx = GetIndex(jmp->lab)});
    } else {
      // Other instructions stay as-is.
    }
  }

  return std::make_pair(fn.arg, std::move(insts));
}

bc::Program Assembling::Assemble(const SymbolicProgram &sympgm) {
  bc::Program pgm;
  for (const auto &[name, fn] : sympgm.code) {
    pgm.code[name] = AssembleFn(fn);
  }
  pgm.data = sympgm.data;
  return pgm;
}

}  // namespace bc
