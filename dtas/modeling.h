
#ifndef _MODELING_H
#define _MODELING_H

#include <cstddef>
#include <iterator>
#include <vector>
#include <cstdint>
#include <bitset>

#include "base/logging.h"
#include "../fceulib/emulator.h"
#include "byteset.h"

struct Bank {
  static constexpr int ORIGIN = 0x8000;
  // Only the address space from 0x8000-0xFFFF is mapped.
  // Aborts on a read outside this space.
  uint8_t Read(uint16_t addr) {
    CHECK(addr >= ORIGIN) << "Out of ROM address space.";
    // We could mirror the ROM here if it is small?
    CHECK((int)addr < ORIGIN + rom.size());
    return rom[addr - ORIGIN];
  }
  std::vector<uint8_t> rom;
};

struct BasicBlock {
  // Machine address that begins the block.
  uint16_t start_addr = 0;
};

// State of the machine at a particular program point. We have the
// registers and RAM here.
struct State {
  // PC is determined by the program point, so we do not store it.
  ByteSet A, X, Y, S, P;
  std::vector<ByteSet64> ram;

  // Start the state with the current exact memory, but with universal
  // sets for the registers. (We may need to do something about the
  // stack register and flags.)
  static State FromEmulator(const Emulator *emu);
};


// Represents a program during analysis.
struct Program {
  // We give each basic block a unique index.
  std::unordered_map<uint16_t, int> block_index;
  std::vector<BasicBlock> blocks;
};

#endif
