
#include "modeling.h"

#include <bitset>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "byteset.h"
#include "base/stringprintf.h"
#include "../fceulib/emulator.h"
#include "../fceulib/x6502.h"

State State::FromEmulator(const Emulator *emu) {
  State state;
  state.A = ByteSet::Top();
  state.X = ByteSet::Top();
  state.Y = ByteSet::Top();
  state.S = ByteSet::Top();
  state.P = ByteSet::Top();
  state.ram.resize(2048);
  for (int i = 0; i < 2048; i++) {
    state.ram[i].Add(emu->ReadRAM(i));
  }
  return state;
}

std::strong_ordering State::operator <=>(const State &other) const {
# define LEX(a) do {                                    \
    const auto ord = (this-> a <=> other. a );          \
    if (ord != std::strong_ordering::equal) return ord; \
  } while (0)

  LEX(A);
  LEX(X);
  LEX(Y);
  LEX(S);
  LEX(P);
  for (int i = 0; i < 2048; i++) {
    LEX(ram[i]);
  }

  return std::strong_ordering::equal;
}

bool State::MergeState(const State &other) {
  bool changed = false;
# define UNION(C, field) do {                         \
    auto s = C :: Union(this-> field , other. field); \
    if (this-> field != s) {                          \
      changed = true;                                 \
      this-> field = std::move(s);                    \
    }                                                 \
  } while (0)

  UNION(ByteSet, A);
  UNION(ByteSet, X);
  UNION(ByteSet, Y);
  UNION(ByteSet, S);
  UNION(ByteSet, P);
  for (int i = 0; i < 2048; i++) {
    UNION(ByteSet64, ram[i]);
  }
  return changed;
}

bool Modeling::EnterBlock(uint16_t addr, const State &state) {
  std::unordered_map<uint16_t, int> block_index;
  std::vector<BasicBlock> blocks;

  auto it = block_index.find(addr);
  if (it == block_index.end()) {
    // New block.
    block_index[addr] = blocks.size();
    blocks.emplace_back(BasicBlock{.start_addr = addr, .state_in = state});
    return true;
  } else {
    const int bidx = it->second;
    CHECK(bidx >= 0 && bidx < (int)blocks.size());
    BasicBlock &block = blocks[bidx];
    return block.state_in.MergeState(state);
  }
}

bool Modeling::Expand() {
  //
  LOG(FATAL) << "Unimplemented";
  return false;
}
