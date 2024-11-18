
#ifndef _MODELING_H
#define _MODELING_H

#include <cstddef>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "../fceulib/emulator.h"
#include "base/logging.h"
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

// Abstract state of the machine at a particular program point. It
// represents some approximation to the *set* of states that the
// machine might take on when entering that point.
//
// We have just the registers and RAM here; there are other things
// that affect execution but perhaps not in a simple game like Mario.
struct State {
  // PC is determined by the program point, so we do not store it.
  ByteSet A, X, Y, S, P;
  std::vector<ByteSet64> ram;

  // Start the state with the current exact memory, but with universal
  // sets for the registers. (We may need to do something about these,
  // especially the stack?)
  static State FromEmulator(const Emulator *emu);
};

struct BasicBlock {
  // Machine address that begins the block.
  uint16_t start_addr = 0;
  // The abstract state entering this block.
  State state_in;
};

struct Modeling {
  // --- The program being analyzed ---
  // This is the source of instructions and any other
  // read-only data accessed by the program.
  Bank rom;
  // We give each basic block a unique index.
  std::unordered_map<uint16_t, int> block_index;
  std::vector<BasicBlock> blocks;

  // Record that we can reach the address with the
  // given state. It may add a new basic block to our
  // analysis. Returns true if this caused a change.
  bool EnterBlock(uint16_t addr, const State &state);

  // Run the abstract evaluation forward. This may
  // insert new basic blocks (if we discover that they
  // are reachable) and may expand the 'in' states for
  // any basic blocks.
  //
  // Returns true if we made some change. Otherwise,
  // the analysis is saturated.
  bool Expand();
};

#endif
