
#include "modeling.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <bitset>

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

bool EnterBlock(uint16_t addr, const State &state) {
  std::unordered_map<uint16_t, int> block_index;
  std::vector<BasicBlock> blocks;

  auto it = block_index.find(addr);
  if (it == block_index.end()) {
    // New block.
    block_index[addr] = blocks.size();
    blocks.emplace_back(BasicBlock{.start_addr = addr, .state_in = state});
    return true;
  } else {

  }

}

bool Modeling::Expand() {
  //

}
