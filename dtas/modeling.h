
#ifndef _MODELING_H
#define _MODELING_H

#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"

#include "base/logging.h"
#include "byte-set.h"
#include "formula.h"
#include "hashing.h"
#include "sourcemap.h"
#include "zoning.h"

// Can use ByteSet64, which reduces the memory requirements to 25%,
// but is slower and less accurate.
using MemByteSet = ByteSet;
#define RegByteSet(x) x

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

  // For ROM addresses. If the address has a symbolic label (from the
  // assembly file), return it. (This is hard-coded to mario.nes!)
  std::optional<std::string> GetLabel(uint16_t addr) const;
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

  const MemByteSet &RAM(uint16_t addr) const {
    CHECK(addr < 2048);
    return ram[addr];
  }

  // Start the state with the current exact memory, but with universal
  // sets for the registers. The stack is set to the constant
  // value provided; you should observe this for your entry point.
  // (Not knowing the value of the stack pointer is bad because it
  // quickly leads to the entire stack having too many possible
  // values.)
  static State FromEmulator(const Emulator *emu, uint8_t sp);

  bool operator ==(const State &other) const {
    return (*this <=> other) == std::strong_ordering::equal;
  }
  std::strong_ordering operator <=>(const State &other) const;

  // Union other into this state. Return true if anything changed.
  bool MergeState(const State &other);

  // Multiline color debug string. RAM is not shown.
  std::string DebugString() const;

 private:
  friend struct Modeling;
  std::vector<MemByteSet> ram;
};

// A global assertion about a memory location.
// This is a common form of .always assertion, so we treat them
// specially for simplicity.
//
// These assertions are not taken as assumptions; they are checked during
// modeling and we abort if any are violated.
struct ValueConstraint {
  std::string comment;
  ByteSet valid_values;
};

// The same code address can be inserted as a basic block multiple
// times, by using a different tag for it. The reason to do this is
// that if you know the conditions under which the block is entered,
// it often reduces the possibilities for what can happen. This is
// particularly important for blocks that are called as subroutines
// from different places; splitting into two different blocks based
// on the source makes it possible to know where we will return to
// in an RTS instruction. The tag can be anything; "" is common.
struct BlockTag {
  BlockTag(std::string lab, uint16_t a) : label(std::move(lab)), addr(a) {}
  std::string label = "DEFAULT";
  uint16_t addr = 0;

  inline bool operator==(const BlockTag &other) const {
    return addr == other.addr && label == other.label;
  }
};


struct BasicBlock {
  BlockTag tag;
  // Machine address that begins the block.
  uint16_t StartAddr() const { return tag.addr; }

  // TODO: length, etc.??

  // The abstract state entering this block.
  State state_in;
};

template <>
struct Hashing<BlockTag> {
  size_t operator()(const BlockTag& tag) const {
    size_t h = Hashing<std::string>()(tag.label);
    h = std::rotl<size_t>(h, 11);
    h *= 31337;
    h += tag.addr;
    return h;
  }
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
  // read-only data accessed by the program. We only support
  // a single bank here.
  Bank rom;

  // We give each basic block a unique index.
  std::unordered_map<BlockTag, int, Hashing<BlockTag>> block_index;
  std::vector<BasicBlock> blocks;
  std::unordered_map<uint16_t, std::vector<BlockTag>> block_tags;
  // The basic blocks that may need update.
  Dirty dirty;
  // Default zoning marks everything as executable, but you
  // might want to load a zoning file to make assumptions
  // about what addresses are actually code.
  Zoning zoning;
  int verbose = 0;
  // Disable normal parsimonious messages like when we detect
  // a state space explosion.
  bool quiet = false;

  // Assertions about the values that can be stored in memory
  // locations.
  std::unordered_map<uint16_t, ValueConstraint> ram_constraints;

  // Override verbosity for specific addresses.
  std::unordered_map<uint16_t, int> verbose_addrs;

  // All these formulas are expected to always hold. This is not
  // an assumption; it (should be) checked by the model checker.
  // TODO: We may want a more efficient representation if there
  // are a lot of formulas. The only thing we do efficiently is
  // promote a constraint like ram[123] in S to ram_constraints.
  std::vector<std::shared_ptr<Form>> always_formulas;

  // True if the analysis is quiescent.
  bool Done() const;


  void AddConstraint(const Constraint &c);


  // The stuff below is mostly implementation details, perhaps
  // exposed for testing.

  static void AddWithCarry(State *state, const ByteSet &values);
  static void SubtractWithCarry(State *state, const ByteSet &values);
  // Returns (values, flags). This depends on the state and affects
  // flags because the carry flag is rotated into the value.
  static std::pair<ByteSet, ByteSet> RotateRight(
      const State &state, const ByteSet &src);
  static std::pair<ByteSet, ByteSet> RotateLeft(
      const State &state, const ByteSet &src);

  // Used internally to print the location of an error like a
  // memory invariant violation.
  struct ErrorLoc {
    uint16_t pc = 0;
  };

  // Get the byteset for a specific memory location. If it's in ROM,
  // it will be a singleton set with the fixed contents. If it's in
  // RAM, we use the set in that slot from the state. If it's
  // memory-mapped, then we have special cases.
  ByteSet GetByteSet(const State &state, uint16_t addr) const;
  // Conversely, write to an address (typically a RAM address).
  void WriteMemByteSet(const ErrorLoc &loc,
                       State *state, uint16_t addr,
                       const MemByteSet &s) const;
  // Merge the set with the memory address (typically a RAM address
  // when the destination of the write is not definite).
  void MergeWriteByteSet(const ErrorLoc &loc,
                         State *state, uint16_t addr,
                         const ByteSet &s) const;

  // When we have addr+x, x may take on multiple values. This merges
  // all the possibilities. We need to distinguish the full 16-bit
  // address case from the zero page case, since the latter only
  // wraps around on the zero page.
  ByteSet GetByteSetFromOffsets16(
      const State &state, uint16_t addr, const ByteSet &offsets) const;
  ByteSet GetByteSetFromOffsetsZpg(
      const State &state, uint8_t addr, const ByteSet &offsets) const;

  // True if we have this block in our analysis.
  bool HasBlock(const BlockTag &tag) const;
  // Record that we can reach the address with the
  // given state. It may add a new basic block to our
  // analysis. Marks the block as dirty if it is new
  // or has changed.
  void EnterBlock(const BlockTag &tag, const State &state);

  // Run the abstract evaluation forward for the next block in the
  // queue (if there are any). This may insert new basic blocks (if we
  // discover that they are reachable) and may expand the 'in' states
  // for any basic blocks.
  void Expand();

  // Check the invariants about the RAM address. Aborts if they are
  // violated. This is mainly used internally.
  void CheckMemoryInvariants(const ErrorLoc &loc,
                             const State &state, uint16_t addr) const;

  // Write the current model as an .asm file with annotations on
  // basic blocks.
  void WriteAnnotatedAssembly(const SourceMap &source_map,
                              std::string_view filename) const;

  // Color to-string methods for modeling data types.
  static std::string TagString(const BlockTag &tag);
  // No color.
  static std::string PlainTagString(const BlockTag &tag);
  std::string ErrorLocString(const ErrorLoc &loc) const;

};

#endif
