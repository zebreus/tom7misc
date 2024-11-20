
#ifndef _MODELING_H
#define _MODELING_H

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"
#include "base/logging.h"
#include "byteset.h"

struct Bank {
  static constexpr int ORIGIN = 0x8000;
  // Only the address space from 0x8000-0xFFFF is mapped.
  // Aborts on a read outside this space.
  uint8_t Read(uint16_t addr) const {
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

  bool operator ==(const State &other) const {
    return (*this <=> other) == std::strong_ordering::equal;
  }
  std::strong_ordering operator <=>(const State &other) const;

  // Union other into this state. Return true if anything changed.
  bool MergeState(const State &other);
};

struct BasicBlock {
  // Machine address that begins the block.
  uint16_t start_addr = 0;
  // length, etc.??

  // The abstract state entering this block.
  State state_in;
};

// Blocks (as their indices) that need to be worked on. We maintain
// uniqueness and FIFO ordering.
struct Dirty {
  size_t Size() const {
    return set.size();
  }

  void Push(int idx) {
    if (!set.contains(idx)) {
      queue.push_back(idx);
      set.insert(idx);
    }
  }

  int Pop() {
    CHECK(!queue.empty());
    int a = queue.front();
    queue.pop_front();
    set.erase(a);
    return a;
  }

  bool Empty() const {
    return set.empty();
  }

 private:
  // Representation invariant: The set and queue
  // contain exactly the same elements.
  std::unordered_set<int> set;
  std::deque<int> queue;
};

struct Modeling {
  explicit Modeling(Bank rom_in) : rom(std::move(rom_in)) {}
  // --- The program being analyzed ---
  // This is the source of instructions and any other
  // read-only data accessed by the program.
  Bank rom;
  // We give each basic block a unique index.
  std::unordered_map<uint16_t, int> block_index;
  std::vector<BasicBlock> blocks;
  // The basic blocks that may need update.
  Dirty dirty;

  // True if the analysis is quiescent.
  bool Done() const;

  // Get the byteset for a specific memory location. If it's in ROM,
  // it will be a singleton set with the fixed contents. If it's in
  // RAM, we use the set in that slot from the state. If it's
  // memory-mapped, then we have special cases.
  ByteSet GetByteSet(const State &state, uint16_t addr) const;
  // Conversely, write to an address (typically a RAM address).
  void WriteByteSet64(State *state, uint16_t addr,
                      const ByteSet64 &s) const;

  // Record that we can reach the address with the
  // given state. It may add a new basic block to our
  // analysis. Marks the block as dirty if it is new
  // or has changed.
  void EnterBlock(uint16_t addr, const State &state);

  // Run the abstract evaluation forward for the next block in the
  // queue (if there are any). This may insert new basic blocks (if we
  // discover that they are reachable) and may expand the 'in' states
  // for any basic blocks.
  void Expand();
};

#endif
