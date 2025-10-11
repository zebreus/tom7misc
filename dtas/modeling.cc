
#include "modeling.h"

#include <algorithm>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "../fceulib/emulator.h"
#include "../fceulib/opcodes.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "byte-set.h"
#include "formula.h"
#include "sourcemap.h"
#include "util.h"
#include "zoning.h"

// For debugging symbols. We should just load
// debugging symbols for the rom under test; this
// need not be mario-specific.
#include "mario-util.h"

#define N_FLAG 0x80
#define V_FLAG 0x40
#define U_FLAG 0x20
#define B_FLAG 0x10
#define D_FLAG 0x08
#define I_FLAG 0x04
#define Z_FLAG 0x02
#define C_FLAG 0x01

static inline uint16_t Word16(uint8_t hi, uint8_t lo) {
  return (((uint16_t)hi) << 8) | lo;
}

std::optional<std::string> Bank::GetLabel(uint16_t addr) const {
  return MarioUtil::GetLabel(addr);
}

// Manipulates labels for call stacks. This might not work as expected if
// the program doesn't pair JSR and RTS, but it also isn't required for
// correctness (it is always OK to split a block or conflate them; you
// just increase cost or conservativity).
static std::string PushLabel(const std::string &label,
                             const std::string &suffix) {
  return std::format("{}.{}", label, suffix);
}
static std::string PopLabel(const std::string &label) {
  auto pos = label.rfind('.');
  if (pos == std::string::npos) return label;
  return label.substr(0, pos);
}

std::string Modeling::TagString(const BlockTag &tag) {
  return std::format(ACYAN("{}") AGREY(":") AYELLOW("{:04x}"),
                     tag.label, tag.addr);
}

std::string Modeling::PlainTagString(const BlockTag &tag) {
  return std::format("{}:{:04x}", tag.label, tag.addr);
}

std::string State::DebugString() const {
  std::string ret;
  AppendFormat(
      &ret,
      AWHITE(" == state == ") "\n"
      AGREEN("A") ":{}\n"
      ABLUE("X") ":{}\n"
      ACYAN("Y") ":{}\n"
      APURPLE("S") ":{}\n"
      AORANGE("P") ":{}\n"
      AGREY("(memory elided)") "\n",
      A.DebugString(),
      X.DebugString(),
      Y.DebugString(),
      S.DebugString(),
      P.DebugString());
  // Show stack, at least if stack is definite.
  if (S.Size() == 1) {
    uint8_t sp = S.GetSingleton();

    for (int i = std::max((int)sp - 2, 0);
         i < std::min((int)sp + 6, 0xFF);
         i++) {
      AppendFormat(&ret,
                   "Stack[" APURPLE("{:02x}") "] {} = {}\n",
                   i,
                   (i == sp) ? ABLUE("**") : "  ",
                   RAM(0x100 + i).DebugString());
    }
  }
  return ret;
}

State State::FromEmulator(const Emulator *emu, uint8_t sp) {
  State state;
  state.A = ByteSet::Top();
  state.X = ByteSet::Top();
  state.Y = ByteSet::Top();
  state.S = ByteSet::Singleton(sp);
  state.P = ByteSet::Top();
  state.ram.resize(2048);
  for (int i = 0; i < 2048; i++) {
    state.ram[i] = MemByteSet::Singleton(emu->ReadRAM(i));
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
    LEX(RAM(i));
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
    UNION(MemByteSet, ram[i]);
  }
  return changed;
}

bool Modeling::HasBlock(const BlockTag &tag) const {
  return block_index.contains(tag);
}

void Modeling::EnterBlock(const BlockTag &tag, const State &state) {
  auto it = block_index.find(tag);
  if (it == block_index.end()) {
    // New block.
    if (state.A.Empty() || state.X.Empty() || state.Y.Empty() ||
        state.S.Empty() || state.P.Empty()) {
      LOG(FATAL) << "Tried to enter block " << TagString(tag) <<
        " in impossible state:\n" << state.DebugString();
    }

    if (0 == (zoning.addr[tag.addr] & Zoning::X)) {
      if (verbose > 0) {
        Print("\nTried to enter non-executable address {}.\n",
              TagString(tag));
      }
      // Do nothing.
    } else {
      block_tags[tag.addr].push_back(tag);
      if (verbose > 0) {
        const auto &v = block_tags[tag.addr];
        if (v.size() == 1) {
          Print("New block {}\n", TagString(tag));
        } else {
          Print("Addr " AYELLOW("{:04x}") " now has tags:", tag.addr);
          for (const BlockTag &otag : v) {
            Print(" " ACYAN("{}"), otag.label);
          }
          Print("\n");
        }
      }
      const int bidx = (int)blocks.size();
      block_index[tag] = bidx;
      blocks.emplace_back(BasicBlock{.tag = tag, .state_in = state});
      dirty.Push(bidx);
    }
  } else {
    const int bidx = it->second;
    CHECK(bidx >= 0 && bidx < (int)blocks.size());
    BasicBlock &block = blocks[bidx];
    // Only need to update if the union of states is different from
    // what it was.
    if (block.state_in.MergeState(state)) {
      dirty.Push(bidx);
    }
  }
}

bool Modeling::Done() const {
  return dirty.Empty();
}

// Adapts a simple function from uint8 -> uint8 into an
// argument appropriate for ReadModifyWrite, setting only
// the Z and N flags.
template<class F>
static std::function<std::pair<ByteSet, ByteSet>(ByteSet)>
SimpleWithZN(F f_in) {
  return [f = std::move(f_in)](const ByteSet &xs) ->
    std::pair<ByteSet, ByteSet> {
    ByteSet value_set, flags_set;
    for (uint8_t x : xs) {
      const uint8_t v = f(x);
      uint8_t flags = 0;
      if (v == 0) flags |= Z_FLAG;
      if (v & 0x80) flags |= N_FLAG;
      value_set.Add(v);
      flags_set.Add(flags);
    };
    return std::make_pair(value_set, flags_set);
  };
}

// Get the byteset for a specific memory location.
// If it's in ROM, it will be a singleton set with the
// fixed contents. If it's in RAM, we use the set
// in that slot from the state. If it's memory-mapped,
// then we have special cases.
ByteSet Modeling::GetByteSet(const State &state, uint16_t addr) const {
  // ROM reads.
  if (addr >= rom.ORIGIN)
    return ByteSet::Singleton(rom.Read(addr));

  // RAM.
  if (addr < 2048) {
    return RegByteSet(state.RAM(addr));
  }

  // TODO: Special cases for memory-mapped addresses.

  // Otherwise, treat it as though any value is possible.
  return ByteSet::Top();
}

// For 16-bit addresses.
ByteSet Modeling::GetByteSetFromOffsets16(
    const State &state, uint16_t addr, const ByteSet &offsets) const {
  ByteSet ret;
  for (uint8_t o : offsets) {
    ret.AddSet(GetByteSet(state, addr + o));
  }
  return ret;
}

// For 8-bit addresses into the zero page (wrap-around).
ByteSet Modeling::GetByteSetFromOffsetsZpg(
    const State &state, uint8_t addr, const ByteSet &offsets) const {
  ByteSet ret;
  for (uint8_t o : offsets) {
    uint8_t zpg_addr = addr + o;
    ret.AddSet(GetByteSet(state, zpg_addr));
  }
  return ret;
}

// Write *addr = s.
// If the address is in RAM, we simply set the state in ram to
// that set. Writes to ROM are ignored. Writes to memory-mapped
// regions may be treated specially.
//
// This function assumes the write takes place to specifically that
// address. Remember that when we write to an indeterminate address,
// we don't know that the write even takes place at any given address,
// so we need to *union* at each destination, not *overwrite*.
void Modeling::WriteMemByteSet(const ErrorLoc &loc,
                               State *state, uint16_t addr,
                               const MemByteSet &s) const {
  // ROM writes are ignored.
  if (addr >= rom.ORIGIN)
    return;

  // RAM.
  if (addr < 2048) {
    state->ram[addr] = s;
    CheckMemoryInvariants(loc, *state, addr);
    return;
  }

  // TODO: Special cases for memory-mapped addresses.
}

void Modeling::MergeWriteByteSet(const ErrorLoc &loc,
                                 State *state, uint16_t addr,
                                 const ByteSet &s) const {
  // ROM writes are ignored.
  if (addr >= rom.ORIGIN)
    return;

  // RAM.
  if (addr < 2048) {
    state->ram[addr].AddSet(s);
    CheckMemoryInvariants(loc, *state, addr);
    return;
  }

  // TODO: Special cases for memory-mapped addresses.
}


// Update flags. This affects only the flags in mask, which are
// removed from the current state (other bits are preserved). The
// set of argument flags must only have bits in the mask set, and
// must be nonempty.
static void CombineFlags(State *state, const ByteSet &flags, uint8_t mask) {
  ByteSet new_flags;
  for (uint8_t b : state->P) {
    b &= ~mask;

    for (uint8_t f : flags) {
      new_flags.Add(b | f);
    }
  }
  state->P = std::move(new_flags);
}

namespace {
struct ZNBools {
  bool has_zero = false;
  bool has_nonzero = false;
  bool has_neg = false;
  bool has_nonneg = false;
};
}

static void CombineZNFlags(State *state,
                           ZNBools bools) {
  // Possible values for the ZN flags.
  // We never have both Z and N, so this
  // is some subset of 00, 10, 01.

  // {10} - When the set contains only zero.
  // {10, 00} - When the set contains zero and positive numbers.
  // {10, 01} - When the set contains zero and negative numbers.
  // {01} - When the set contains only negative numbers.
  // {00} - When the set contains only positive numbers.
  // ...
  std::vector<uint8_t> zflags;
  if (bools.has_zero) zflags.push_back(Z_FLAG);
  if (bools.has_nonzero) zflags.push_back(0);

  std::vector<uint8_t> nflags;
  if (bools.has_neg) nflags.push_back(N_FLAG);
  if (bools.has_nonneg) nflags.push_back(0);

  CHECK(!zflags.empty() && !nflags.empty());

  ByteSet new_flags;
  for (uint8_t b : state->P) {
    b &= ~(Z_FLAG | N_FLAG);

    for (uint8_t z : zflags) {
      for (uint8_t n : nflags) {
        new_flags.Add(b | z | n);
      }
    }
  }
  state->P = std::move(new_flags);
}

// Compute the values of the Z and N flags in the status
// register, given the byteset. Preserves the values of
// the other flags.
static void ZN(State *state, const ByteSet &s) {
  constexpr bool VERBOSE = false;
  CHECK(!s.Empty());
  if (VERBOSE) {
    Print("ZN flags for: {{");
    for (uint8_t v : s) {
      Print("{:02x}, ", v);
    }
    Print("}}\n");
  }

  bool contains_z = s.Contains(0);
  bool contains_pos = false;
  bool contains_neg = false;
  for (uint8_t v : s) {
    if (v > 0 && v < 0x80) contains_pos = true;
    else if (v >= 0x80) contains_neg = true;
  }

  if (VERBOSE) {
    Print("Contains z: {:c} p: {:c} n: {:c}\n",
          contains_z ? 'X' : '_',
          contains_pos ? 'X' : '_',
          contains_neg ? 'X' : '_');
  }


  CombineZNFlags(state,
                 ZNBools{
                   .has_zero = contains_z,
                   .has_nonzero = contains_pos || contains_neg,
                   .has_neg = contains_neg,
                   .has_nonneg = contains_pos || contains_z});
}

static void Compare(State *state, const ByteSet &reg, const ByteSet &op) {
  // Relation    Z C N
  // reg < op    0 0 sign-bit of result
  // reg = op    1 1 0
  // reg > op    0 1 sign-bit of result

  // We don't do this one with separate boolean flags
  // because not all combinations are possible.
  ByteSet zncflags;

  for (uint8_t r : reg) {
    for (uint8_t o : op) {
      uint32_t t = r - o;

      uint8_t f = 0;
      if (t == 0) f |= Z_FLAG;
      if (t & 0x80) f |= N_FLAG;
      if (r >= o) f |= C_FLAG;

      zncflags.Add(f);
    }
  }

  // Now update flags.
  CombineFlags(state, zncflags, Z_FLAG | N_FLAG | C_FLAG);
}

// A <- A + Value + Carry
// updating neg, zero, carry, overflow flags
void Modeling::AddWithCarry(State *state, const ByteSet &values) {
  bool has_carry = false;
  bool has_nocarry = false;
  for (uint8_t flags : state->P) {
    if (flags & C_FLAG) has_carry = true;
    else has_nocarry = true;
  }

  // We need to iterate over (a, c) pairs rather than summing
  // them ahead of time into a set. This is because the overflow
  // flag depends on a, not a+c.
  ByteSet carry_values;
  if (has_carry) carry_values.Add(0x01);
  if (has_nocarry) carry_values.Add(0x00);

  // Now all possible sums.
  ByteSet flags_nzcv;
  ByteSet results;
  for (uint8_t v : values) {
    for (uint8_t a : state->A) {
      for (uint8_t c : carry_values) {

        // Use wide addition so that we can test for carry,
        // overflow, etc. First blend possible carry flags
        // with possible values for A.
        uint32_t l = (uint32_t)a + (uint32_t)c + (uint32_t)v;

        uint8_t res8 = l & 0xFF;
        results.Add(res8);

        // we have overflow if the signs
        // started the same, but end different.
        uint8_t s1 = a & 0x80;
        uint8_t s2 = v & 0x80;
        uint8_t sr = res8 & 0x80;
        uint8_t overflow = (s1 == s2 && sr != s1) ? V_FLAG : 0;

        uint8_t flags =
          ((l >> 8) & C_FLAG) |
          (res8 == 0 ? Z_FLAG : 0) |
          ((res8 & 0x80) ? N_FLAG : 0) |
          overflow;
        flags_nzcv.Add(flags);
      }
    }
  }

  state->A = std::move(results);
  CombineFlags(state, flags_nzcv, Z_FLAG | N_FLAG | C_FLAG | V_FLAG);
}

// A <- A - Value - ~Carry
// updating neg, zero, carry, overflow flags
void Modeling::SubtractWithCarry(State *state, const ByteSet &values) {
  // Test whether we always/sometimes/never have carry bit.
  bool has_carry = false;
  bool has_nocarry = false;
  for (uint8_t flags : state->P) {
    if (flags & C_FLAG) has_carry = true;
    else has_nocarry = true;
  }

  // Like above. Note that the sense of the carry is
  // reversed here (carry ZERO means DO subtract 1).
  ByteSet borrow_values;
  if (has_carry) borrow_values.Add(0x00);
  if (has_nocarry) borrow_values.Add(0x01);

  // Now all possible sums.
  ByteSet flags_nzcv;
  ByteSet results;
  for (uint8_t v : values) {
    for (uint8_t a : state->A) {
      for (uint8_t borrow : borrow_values) {
        uint32_t l = (uint32_t)a - (uint32_t)borrow - (uint32_t)v;

        uint8_t res8 = l & 0xFF;
        results.Add(res8);

        // we have overflow if the signs
        // started different (because the RHS is negated)
        // and result in a sign different from the LHS.
        uint8_t s1 = a & 0x80;
        uint8_t s2 = v & 0x80;
        uint8_t sr = res8 & 0x80;
        uint8_t overflow = (s1 != s2 && sr != s1) ? V_FLAG : 0;

        uint8_t flags =
          (((l >> 8) & C_FLAG) ^ C_FLAG) |
          (res8 == 0 ? Z_FLAG : 0) |
          ((res8 & 0x80) ? N_FLAG : 0) |
          overflow;
        flags_nzcv.Add(flags);
      }
    }
  }

  state->A = std::move(results);
  CombineFlags(state, flags_nzcv, Z_FLAG | N_FLAG | C_FLAG | V_FLAG);
}


void Modeling::Expand() {
  // Loop over basic blocks.
  // For each one, run the instructions forward, transforming
  // the state to get the possible next states. (We should
  // do this by generating a Z3 program representing the
  // sequence of instructions?) When we get to any branch
  // instruction, we use the condition to see whether we will
  // enqueue one or two basic blocks as follows. Suppose it tests
  // that the carry flag is set (BCS):
  //  - If the carry flag has a known value (i.e. the possible
  //    values of the P register all have the same value in
  //    that bit) then we know we take that branch, and so
  //    we just continue with the basic block and the given
  //    state.
  //  - Otherwise, it can take on either value. This means
  //    that we will insert both the block at the branch
  //    destination and the next instruction. For the branch
  //    taken, we know that the carry flag is set (and so
  //    we can quotient out the others from the P register's
  //    byteset); likewise on the next instruction we know it
  //    is clear. This just applies to the state that we're
  //    passing on through this code path; the flag could
  //    have a different value if we arrive to that basic
  //    block a different way.
  //  - In either case, we might be visiting a basic
  //    block for the first time; if so we insert it with
  //    EnterBlock.
  //
  // Why bother with basic blocks? We could just think of each
  // one as an individual instruction. I guess we get some
  // benefit from not having to store the full state for
  // intermediates.

  if (dirty.Empty())
    return;

  // PERF: It should be feasible to do the analysis in parallel.
  // We just need to synchronize the updated states that we
  // deduce.
  const int block_idx = dirty.Pop();
  const BasicBlock &block = blocks[block_idx];

  State state = block.state_in;
  const std::string current_label = block.tag.label;
  uint16_t pc = block.StartAddr();

  ErrorLoc loc{
    .pc = pc,
  };

  auto Next8 = [&]() -> uint8_t {
      uint8_t ret = rom.Read(pc);
      pc++;
      return ret;
    };

  // Get the next 16 bits after the program counter; advance it.
  // This is for instructions like "LDA addr" that contain a
  // fixed 16-bit address in the instruction.
  auto Next16 = [&]() -> uint16_t {
      uint8_t lo = Next8();
      uint8_t hi = Next8();
      return ((uint16_t)hi << 8) | lo;
    };

  // Handle branch instructions. Since this ends the basic
  // block, the caller should return, not continue!
  auto Branch = [&](uint8_t mask, uint8_t true_case) {
      CHECK((mask & true_case) == true_case);
      // In the false branch, we know that the bit is the
      // opposite of whatever was in true_case.
      const uint8_t false_case = mask & ~true_case;

      // Displacement when branch taken.
      int8_t displacement = (int8_t)Next8();
      // All branches happen on flags. Compute the
      // possible results of the flags.
      bool has_true = false;
      bool has_false = false;
      for (int i = 0; i < 256; i++) {
        if (state.P.Contains(i)) {
          if ((i & mask) == true_case) {
            has_true = true;
          } else {
            has_false = true;
          }
        }
      }

      CHECK(has_true || has_false) << state.DebugString();
      if (has_true) {
        // Do the branch.
        State truestate = state;
        truestate.P = truestate.P.Map([mask, true_case](uint8_t v) {
            return (v & ~mask) | true_case;
          });
        // The displacement is relative to the instruction after
        // the branch, which is what the pc now represents.
        EnterBlock(BlockTag(current_label, pc + displacement),
                   std::move(truestate));
      }

      if (has_false) {
        // Don't take the branch; "jump" to the next
        // instruction. We know that the tested flag
        // has the value false_case when the branch was
        // not taken.
        state.P = state.P.Map([mask, false_case](uint8_t v) {
            return (v & ~mask) | false_case;
          });
        EnterBlock(BlockTag(current_label, pc), state);
      }
    };

  // XXX should do this like RotateRight, returning flags
  auto RotateLeft = [](State *state, const ByteSet &src) {
      bool has_carry = false;
      bool has_no_carry = false;
      for (uint8_t f : state->P) {
        if (f & C_FLAG) has_carry = true;
        else has_no_carry = true;
      }

      CHECK(has_carry || has_no_carry);

      ByteSet zncflags;
      ByteSet new_a;
      for (uint8_t b : src) {
        uint8_t f = 0;
        if (b & 0x80) f |= C_FLAG;
        uint8_t v = b << 1;
        if (v & 0x80) f |= N_FLAG;
        else if (v == 0 && has_no_carry) f |= Z_FLAG;

        if (has_carry) new_a.Add(v | 0x01);
        if (has_no_carry) new_a.Add(v);

        zncflags.Add(f);
      }
      state->A = std::move(new_a);
      CombineFlags(state, zncflags, Z_FLAG | N_FLAG | C_FLAG);
      CHECK(!state->A.Empty());
      CHECK(!state->P.Empty());
    };

  // Returns (values, flags).
  // This depends on the state (and cannot just return one value) because
  // the carry flag is rotated into the value.
  auto RotateRight = [](const State &state, const ByteSet &src) ->
    std::pair<ByteSet, ByteSet> {

      bool has_carry = false;
      bool has_no_carry = false;
      for (uint8_t f : state.P) {
        if (f & C_FLAG) has_carry = true;
        else has_no_carry = true;
      }

      CHECK(has_carry || has_no_carry);

      ByteSet new_a;
      ByteSet zncflags;
      for (uint8_t b : src) {
        const uint8_t value = b >> 1;

        uint8_t f = 0;
        if (b & 1) f |= C_FLAG;

        if (has_carry) new_a.Add(value | 0x80);
        if (has_no_carry) new_a.Add(value);

        // C flag is always determined from the lsb of the input value.
        // But the negative flag and zero flags depend on the old carry.
        if (has_carry) {
          // Can't be zero.
          zncflags.Add(f | N_FLAG);
        }
        if (has_no_carry) {
          if (value == 0) {
            zncflags.Add(f | Z_FLAG);
          } else {
            zncflags.Add(f);
          }
        }
      }

      return std::make_pair(new_a, zncflags);
    };

  // f takes ByteSet and returns a pair of ByteSets,
  // which are the possible (values, flags) when f is applied to
  // elements in the argument ByteSet.
  // mem[addr+offset] = f(mem[addr+offset]).first
  // flags = (flags & ~mask) | f(mem[addr+offset]).second
  // FIXME: I think that we need a version for zero page, which
  // wraps only to addresses on zero page.
  auto ReadModifyWrite = [this, &loc](State *state,
                                      uint8_t flag_mask,
                                      uint16_t addr_base,
                                      const ByteSet &addr_offset,
                                      const auto &f) {
      if (addr_offset.Size() == 1) {
        uint16_t eaddr = addr_base + addr_offset.GetSingleton();
        ByteSet before = this->GetByteSet(*state, eaddr);
        const auto &[after, flags] = f(before);
        // Overwrite, since this is a definite address.
        this->WriteMemByteSet(loc, state, eaddr, MemByteSet(after));
        CombineFlags(state, flags, flag_mask);
      } else {
        ByteSet merged_flags;
        for (uint8_t o : addr_offset) {
          // TODO: Make sure we're handling page wrapping
          // correctly here.
          uint16_t eaddr = addr_base + o;
          ByteSet before = this->GetByteSet(*state, eaddr);
          // Since we don't know whether we're actually writing
          // here, the write is added to the possibilities.
          const auto &[after, flags] = f(before);
          merged_flags.AddSet(flags);
          ByteSet together = ByteSet::Union(before, after);
          this->WriteMemByteSet(loc, state, eaddr, MemByteSet(together));
        }
        // Update with the possible flag values.
        CombineFlags(state, merged_flags, flag_mask);
      }
    };

  // A <- A + Value + Carry
  // updating neg, zero, carry, overflow flags
  auto ReadAddWithCarry = [this](State *state, uint16_t addr_base,
                                 const ByteSet &addr_offset) {
      ByteSet all_values;
      for (uint8_t off : addr_offset) {
        uint16_t eaddr = addr_base + off;
        all_values.AddSet(this->GetByteSet(*state, eaddr));
      }
      return AddWithCarry(state, all_values);
    };

  // A <- A - *addr - ~Carry
  // updating neg, zero, carry, overflow flags
  auto ReadSubtractWithCarry = [this](State *state, uint16_t addr_base,
                                      const ByteSet &addr_offset) {
      ByteSet all_values;
      for (uint8_t off : addr_offset) {
        uint16_t eaddr = addr_base + off;
        all_values.AddSet(this->GetByteSet(*state, eaddr));
      }

      return SubtractWithCarry(state, all_values);
    };

  // For LSR. The operation itself is just a right shift, but it is
  // nontrivial because it modifies the carry flag. (And Z, and N.)
  auto LogicalShiftRightOne = [](uint8_t v) -> std::pair<uint8_t, uint8_t> {
      // Compute flags (on the input so that we can see if
      // we will get carry). We cover the N flag, but it
      // is always zero for LSR.
      uint8_t f = 0;
      if (v & 1) f |= C_FLAG;
      if (v == 0 || v == 1) f |= Z_FLAG;

      return std::make_pair(v >> 1, f);
    };

  auto LogicalShiftRight = [&LogicalShiftRightOne](const ByteSet &xs) {
      ByteSet val_set, flag_set;
      for (uint8_t x : xs) {
        const auto &[v, f] = LogicalShiftRightOne(x);
        val_set.Add(v);
        flag_set.Add(f);
      }
      return std::make_pair(val_set, flag_set);
    };

  // A strange case.
  auto Bit = [this](State *state, uint16_t addr) {
      ByteSet nzv_flags;
      ByteSet s = GetByteSet(*state, addr);
      for (uint8_t b : s) {
        const uint8_t nv = b & (N_FLAG | V_FLAG);
        for (uint8_t a : state->A) {
          uint8_t x = b & a;
          nzv_flags.Add((x ? 0 : Z_FLAG) | nv);
        }
      }
      CombineFlags(state, nzv_flags, N_FLAG | Z_FLAG | V_FLAG);
    };

  // Keep reading instructions until we reach the end of the block.
  do {
    const uint16_t instruction_pc = pc;
    // Read the opcode, which advances the PC past it.
    const uint8_t opcode = Next8();

    int inst_verbose = verbose;
    {
      auto it = verbose_addrs.find(instruction_pc);
      if (it != verbose_addrs.end()) inst_verbose = it->second;
    }

    if (state.A.Empty() || state.X.Empty() || state.Y.Empty() ||
        state.S.Empty() || state.P.Empty()) {
      LOG(FATAL) <<
        std::format("Tried to execute addr {:04x} " AGREY("({})") " in "
                     "impossible state:\n{}\n",
                     instruction_pc,
                     Opcodes::opcode_name[opcode],
                     state.DebugString());
    }

    if (inst_verbose > 1) {
      if (auto lo = rom.GetLabel(instruction_pc)) {
        Print(AWHITE("{}") ":\n", lo.value());
      }
      Print("{:04x}: " ABLUE("{:02x}") " " AGREY("({})") "\n",
            instruction_pc, opcode, Opcodes::opcode_name[opcode]);

      if (inst_verbose > 2) {
        Print("Label: " ACYAN("{}") "\n", current_label);
        Print("{}\n", state.DebugString());

        // XXX make this configurable; this is hard coded for the
        // mario JumpEngine
        for (uint16_t addr : {0x0004, 0x0005, 0x0006, 0x007, 0x0770}) {
          Print("RAM[" AWHITE("{:04x}") "]: {}\n",
                addr,
                GetByteSet(state, addr).DebugString());
        }
      }
    }

    switch (opcode) {
    case 0x4c: { // JMP a
      uint16_t addr = Next16();
      EnterBlock(BlockTag(current_label, addr), state);
      return;
    }

    case 0xf0: { // BEQ *+d
      return Branch(Z_FLAG, Z_FLAG);
    }
    case 0xd0: { // BNE *+d
      return Branch(Z_FLAG, 0);
    }
    case 0x50: { // BVC *+d
      return Branch(V_FLAG, 0);
    }
    case 0x70: { // BVS *+d
      return Branch(V_FLAG, V_FLAG);
    }
    case 0xb0: { // BCS *+d
      return Branch(C_FLAG, C_FLAG);
    }
    case 0x90: { // BCC *+d
      return Branch(C_FLAG, 0);
    }
    case 0x30: { // BMI *+d
      return Branch(N_FLAG, N_FLAG);
    }
    case 0x10: { // BPL *+d
      return Branch(N_FLAG, 0);
    }

    case 0xc8: { // INY
      state.Y = state.Y.Map([](uint8_t v) { return v + 1; });
      ZN(&state, state.Y);
      break;
    }
    case 0x88: { // DEY
      // y <- y-1
      state.Y = state.Y.Map([](uint8_t v) { return v - 1; });
      ZN(&state, state.Y);
      break;
    }
    case 0xca: { // DEX
      // x <- x-1.
      state.X = state.X.Map([](uint8_t v) { return v - 1; });
      ZN(&state, state.X);
      break;
    }

    case 0xa9: { // LDA #i
      uint8_t v = Next8();
      state.A = ByteSet::Singleton(v);
      ZN(&state, state.A);
      break;
    }
    case 0xad: { // LDA a
      uint16_t addr = Next16();
      state.A = GetByteSet(state, addr);
      ZN(&state, state.A);
      break;
    }
    case 0xa5: { // LDA d
      uint16_t addr = Next8();
      state.A = GetByteSet(state, addr);
      ZN(&state, state.A);
      break;
    }
    case 0xb1: { // LDA (d),y
      uint8_t zpg_addr = Next8();
      state.A.Clear();
      for (uint8_t addr_lo : GetByteSet(state, zpg_addr)) {
        for (uint8_t addr_hi : GetByteSet(state, (uint8_t)(zpg_addr + 1))) {
          for (uint8_t y : state.Y) {
            uint16_t effective_addr = Word16(addr_hi, addr_lo) + y;
            state.A.AddSet(GetByteSet(state, effective_addr));
          }
        }
      }
      ZN(&state, state.A);
      break;
    }
    case 0xb9: { // LDA a,y
      uint16_t base_addr = Next16();
      state.A.Clear();
      for (uint8_t y : state.Y) {
        uint16_t effective_addr = base_addr + y;
        state.A.AddSet(GetByteSet(state, effective_addr));
      }
      ZN(&state, state.A);
      break;
    }
    case 0xb5: { // LDA d,x
      uint8_t zeropage_base = Next8();
      state.A.Clear();
      for (uint8_t x : state.X) {
        // Overflow stays on the zero page.
        uint8_t zeropage_effective = zeropage_base + x;
        state.A.AddSet(GetByteSet(state, zeropage_effective));
      }
      ZN(&state, state.A);
      break;
    }
    case 0xbd: { // LDA a,x
      uint16_t addr = Next16();
      state.A.Clear();
      for (uint8_t v : state.X) {
        state.A.AddSet(GetByteSet(state, addr + v));
      }
      ZN(&state, state.A);
      break;
    }


    case 0x20: { // JSR a
      // Potentially hard?
      //
      // It writes into the stack, so want to have more certainty
      // about where the stack pointer points. Otherwise this will
      // make it look like the entire range $0100-$01FF can become the
      // PC after any JSR is executed. We might also conflate stack
      // data and addresses when we push/pop.
      //
      // Optimistically, we always jump to a subroutine with the stack
      // pointer in the same place, and we always return to a single
      // caller. In practice this is not true for mario.nes; there are
      // some shared subroutines and the stack depth is not always the
      // same.
      //
      // Turns out that the stack pointer is (empirically) always 0xFC
      // when we call the NMI. Most instructions that use the stack
      // are (empirically) executed with a single fixed stack pointer
      // value, or else a small number. But it causes a lot of uncertainty
      // when the stack pointer is not definite, and it's even worse
      // in RTS when we don't know what PC to pop. So to hopefully
      // control this, we track the call stack using block tags whenever
      // we do a RTS (in essence duplicating the subroutine).
      //
      // What's pushed:
      //               opcode
      // (current pc)  dst lo
      //               dst hi
      // The thing pushed on the stack is actually pc+1,
      // which is weird because it's between the two address bytes.
      // I guess it's just a pipelining quirk. RTI adds 1 to the
      // that it pops.
      const uint16_t stored_pc = pc + 1;

      // XXX Could be WriteStack etc.
      if (state.S.Size() == 1) {
        // Then it is definitely stored in the stack here,
        // so we can replace the memory location.
        uint16_t saddr = 0x0100 + *state.S.begin();
        // Push low, then push high. Note this actually puts them
        // in "big-endian" order if you are looking at memory,
        // since the stack grows downward.
        WriteMemByteSet(loc, &state, saddr,
                        MemByteSet::Singleton(stored_pc & 0xFF));
        WriteMemByteSet(loc, &state, saddr - 1,
                        MemByteSet::Singleton(stored_pc >> 8));
        state.S = ByteSet::Singleton(saddr - 2);
      } else {
        // Not great: The stack pointer has an uncertain value.
        // We don't know where the stored_pc goes, and so we also
        // can't overwrite these addresses; we just have to add
        // to the set. This makes returns from the JSR pretty
        // uncertain. (And even worse: If the stack pointers
        // are not always aligned to 16 bits in this set, we
        // could conflate high bytes with low bytes, and then
        // it will look like we can return to nonsense addresses.)
        for (uint8_t sp : state.S) {
          uint16_t saddr = 0x0100 + sp;
          // Push low, then push high. Note this actually puts them
          // in "big-endian" order if you are looking at memory,
          // since the stack grows downward.
          MergeWriteByteSet(loc, &state, saddr,
                            MemByteSet::Singleton(stored_pc & 0xFF));
          MergeWriteByteSet(loc, &state, saddr - 1,
                            MemByteSet::Singleton(stored_pc >> 8));
        }
        state.S = state.S.Map([](uint8_t v) {
            return v - 2;
          });
      }

      uint16_t daddr = Next16();
      // TODO: Include history here.

      BlockTag subroutine_tag =
        BlockTag(PushLabel(current_label,
                           std::format("{:04x}", instruction_pc)),
                 daddr);
      EnterBlock(subroutine_tag, state);

      // Ends basic block.
      return;
    }

    case 0x60: { // RTS
      // See JSR for some details.
      for (uint8_t sp : state.S) {
        uint16_t saddr = 0x0100 + sp;
        if (state.RAM(saddr + 1).Size() == 1 &&
            state.RAM(saddr + 2).Size() == 1) {
          // Only one value for the stack at this point.
          // This is the reasonable case.
          uint16_t hi = state.RAM(saddr + 1).GetSingleton();
          uint16_t lo = state.RAM(saddr + 2).GetSingleton();
          // +1 is just how it works; a quirk of JSR and RTS.
          uint16_t raddr = ((hi << 8) | lo) + 1;
          State ret_state = state;
          // Pop from this specific stack offset.
          ret_state.S = ByteSet::Singleton(sp + 2);
          // TODO: could pop from history here?
          BlockTag ret_tag =
            BlockTag(PopLabel(current_label), raddr);
          EnterBlock(ret_tag, std::move(ret_state));
        } else {
          // Multiple RTS destinations. This is a bad situation.
          // We can maybe handle the fact that we don't know *which*
          // caller we return to here, but since the addresses are
          // two bytes, when they are ambiguous we might get
          // nonsensical addresses by combining the high byte of one
          // with the low byte of another (or swapping low/hi bytes).
          // To prevent the analysis from including these, we filter
          // for addresses in ROM that contain a JSR at the appropriate
          // offset. i.e., we only allow returning to places that might
          // have called a subroutine. This excludes self-modifying
          // code, multi-bank hijinks, manually manipulating the stack,
          // exploitable bugs, and so on.
          //
          // XXX This is a justifiable hack but makes some assumptions
          // about how the code works. We should be tracking something
          // about static call graphs to do this better.

          if (inst_verbose > 0) {
            Print(ARED("Ugh") "! Multiple possible RTS destinations [sp="
                  ACYAN("{:02x}") "]: ", sp);
          }

          int born = 0;
          int num_accepted = 0, num_rejected = 0;
          // Print("\n");
          for (int hi = 0; hi < 256; hi++) {
            if (state.RAM(saddr + 1).Contains(hi)) {
              for (int lo = 0; lo < 256; lo++) {
                if (state.RAM(saddr + 2).Contains(lo)) {
                  uint16_t raddr = ((hi << 8) | lo) + 1;

                  // See above.
                  bool allow = false;
                  bool unzoned = false;
                  // Only ROM code addresses.
                  if (raddr >= 0x8000) {
                    if (false) {
                      for (int i = (int)raddr - 5; i < (int)raddr + 2; i++) {
                        Print("{:04x}: {:02x}{}\n",
                               i,
                               rom.Read(i),
                               i == raddr ? AYELLOW(" <- raddr") : "");
                      }
                    }
                    // JSR memory layout is
                    // 0x20 HI LO next
                    //            ^
                    //            raddr points here (we already
                    //            added 1 1)
                    if (raddr - 3 >= 0x8000 &&
                        rom.Read(raddr - 3) == 0x20) {

                      if (zoning.addr[raddr - 3] & Zoning::X) {
                        allow = true;
                      } else {
                        // Disallow, but show in a different color
                        // to distinguish it; this is a bit of a
                        // stronger assumption.
                        unzoned = true;
                      }
                    }
                  }

                  if (allow) {
                    num_accepted++;
                    if (inst_verbose > 0) Print(" {:04x}", raddr);
                    State ret_state = state;
                    // We know where the stack points, and what was
                    // there.
                    ret_state.S = ByteSet::Singleton(sp + 2);
                    WriteMemByteSet(loc, &ret_state, saddr + 1,
                                    MemByteSet::Singleton(hi));
                    WriteMemByteSet(loc, &ret_state, saddr + 2,
                                    MemByteSet::Singleton(lo));

                    // TODO: We might want to split by the value of the
                    // stack pointer here, since it's quite bad if that
                    // is indefinite (e.g. if we encounter another RTS).
                    // The contents of the stack is not very important
                    // though, because we just popped it.
                    BlockTag ret_tag =
                      BlockTag(PopLabel(current_label), raddr);

                    if (!HasBlock(ret_tag)) born++;
                    EnterBlock(ret_tag, std::move(ret_state));
                  } else {
                    num_rejected++;
                    if (inst_verbose > 0) {
                      if (num_rejected < 10) {
                        Print(" {}{:04x}" ANSI_RESET,
                              unzoned ? ANSI_DARK_RED : ANSI_RED,
                              raddr);
                      } else if (num_rejected == 10) {
                        Print(" " ARED("..."));
                      }
                    }
                  }
                }
              }
            }
          }
          if (inst_verbose > 0) Print("\n");

          if (inst_verbose > 0 || (born > 16 && !quiet)) {
            Print("RTS at " ACYAN("{:04x}")
                   " added " AORANGE("{}") " new blocks. "
                   "[acc " AGREEN("{}") "; rej " ARED("{}") "]\n",
                   instruction_pc, born, num_accepted, num_rejected);
            CHECK(born < 1000); // XXX
          }

        }
      }

      // Ends the block.
      return;
    }

    case 0xe8: { // INX
      state.X = state.X.Map([](uint8_t v) { return v + 1; });
      ZN(&state, state.X);
      break;
    }
    case 0x68: { // PLA
      // Pull A from stack.

      state.A.Clear();
      for (uint8_t sp : state.S) {
        // Following what fceulib does for a stack pointer of 0xFF,
        // we get a stack address of 0x0100.
        uint16_t saddr = 0x0100 + (uint8_t)(sp + 1);
        state.A.AddSet(RegByteSet(state.RAM(saddr)));
      }

      state.S = state.S.Map([](uint8_t v) {
          return v + 1;
        });

      ZN(&state, state.A);

      break;
    }
    case 0x48: { // PHA
      // Push A onto stack.

      if (state.S.Size() == 1) {
        const uint8_t sp = *state.S.begin();
        // Then it is definitely stored in the stack here,
        // so we can replace the memory location.
        const uint16_t saddr = 0x0100 + sp;
        WriteMemByteSet(loc, &state, saddr, MemByteSet(state.A));
        state.S = ByteSet::Singleton(sp - 1);
      } else {
        for (uint8_t sp : state.S) {
          const uint16_t saddr = 0x0100 + sp;
          MergeWriteByteSet(loc, &state, saddr, state.A);
        }
        state.S = state.S.Map([](uint8_t v) {
            return v - 1;
          });
      }

      break;
    }

    case 0x0e: { // ASL a
      LOG(FATAL) << "Unimplemented 'ASL a'";
      break;
    }
    case 0x0a: { // ASL
      // Shift left, into carry.
      // Here any of the Zero, Negative, and Carry
      // flags are modified.

      // Look at the values before shifting to
      // determine the possible flags that can
      // result:
      bool has_carry = false;
      bool has_no_carry = false;
      bool has_zero = false;
      bool has_nonzero = false;
      bool has_neg = false;
      bool has_pos = false;
      for (int i = 0; i < 256; i++) {
        if (state.A.Contains(i)) {
          if (i & 0x80) {
            has_carry = true;
          } else {
            has_no_carry = true;
          }

          if (i & 0x7f) {
            has_nonzero = true;
          } else {
            has_zero = true;
          }

          // bit that gets shifted into negative bit.
          if (i & 0b01000000) {
            has_neg = true;
          } else {
            has_pos = true;
          }
        }
      }

      CHECK(has_carry || has_no_carry);
      std::vector<uint8_t> carry_flags;
      if (has_carry) carry_flags.push_back(C_FLAG);
      if (has_no_carry) carry_flags.push_back(0);

      CHECK(has_neg || has_pos);
      std::vector<uint8_t> neg_flags;
      if (has_neg) neg_flags.push_back(N_FLAG);
      if (has_pos) neg_flags.push_back(0);

      CHECK(has_zero || has_nonzero);
      std::vector<uint8_t> zero_flags;
      if (has_zero) zero_flags.push_back(Z_FLAG);
      if (has_nonzero) zero_flags.push_back(0);

      state.A = state.A.Map([](uint8_t v) { return v << 1; });

      // TODO: CombineFlags
      // No combination of flags is contradictory, so we
      // add all of them:
      ByteSet new_flags;
      for (uint8_t v : state.P) {
        v &= ~(C_FLAG | N_FLAG | Z_FLAG);

        for (uint8_t c : carry_flags) {
          for (uint8_t n : neg_flags) {
            for (uint8_t z : zero_flags) {
              new_flags.Add(v | c | n | z);
            }
          }
        }
      }
      state.P = std::move(new_flags);
      break;
    }

    case 0x2a: { // ROL
      RotateLeft(&state, state.A);
      break;
    }
    case 0x26: { // ROL d
      LOG(FATAL) << "Unimplemented ROL";
      // XXX this needs to be ReadModifyWrite
      // uint16_t addr = Next8();
      // RotateLeft(&state, GetByteSet(state, addr));
      break;
    }
    case 0x2e: { // ROL a
      LOG(FATAL) << "Unimplemented ROL";
      // uint16_t addr = Next16();
      // RotateLeft(&state, GetByteSet(state, addr));
      break;
    }

    case 0x38: { // SEC
      state.P = state.P.Map([](uint8_t v) {
          return v | C_FLAG;
        });
      break;
    }
    case 0x18: { // CLC
      state.P = state.P.Map([](uint8_t v) { return v & ~C_FLAG; });
      break;
    }
    case 0xB8: { // CLV
      state.P = state.P.Map([](uint8_t v) { return v & ~V_FLAG; });
      break;
    }
    case 0x78: { // SEI
      state.P = state.P.Map([](uint8_t v) { return v | I_FLAG; });
      break;
    }
    case 0x58: { // CLI
      state.P = state.P.Map([](uint8_t v) { return v & ~I_FLAG; });
      break;
    }

    case 0x85: { // STA d
      uint16_t addr = Next8();
      WriteMemByteSet(loc, &state, addr, MemByteSet(state.A));
      break;
    }
    case 0x8d: { // STA a
      uint16_t addr = Next16();
      WriteMemByteSet(loc, &state, addr, MemByteSet(state.A));
      break;
    }
    case 0x9d: { // STA a,x
      uint16_t addr = Next16();
      if (state.X.Size() == 1) {
        // If the address is definite, then we can overwrite.
        uint16_t eaddr = addr + state.X.GetSingleton();
        WriteMemByteSet(loc, &state, eaddr, MemByteSet(state.A));
      } else {
        for (uint8_t x : state.X) {
          uint16_t eaddr = addr + x;
          MergeWriteByteSet(loc, &state, eaddr, state.A);
        }
      }
      break;
    }
    case 0x99: { // STA a,y
      uint16_t addr = Next16();
      if (state.Y.Size() == 1) {
        // If the address is definite, then we can overwrite.
        uint16_t eaddr = addr + state.Y.GetSingleton();
        WriteMemByteSet(loc, &state, eaddr, MemByteSet(state.A));
      } else {
        for (uint8_t y : state.Y) {
          uint16_t eaddr = addr + y;
          MergeWriteByteSet(loc, &state, eaddr, state.A);
        }
      }
      break;
    }
    case 0x95: { // STA d,x
      uint8_t zpg_addr = Next8();
      if (state.X.Size() == 1) {
        // If the address is definite, then we can overwrite.
        uint8_t zpg_eaddr = zpg_addr + state.X.GetSingleton();
        WriteMemByteSet(loc, &state, zpg_eaddr, MemByteSet(state.A));
      } else {
        for (uint8_t x : state.X) {
          uint8_t zpg_eaddr = zpg_addr + x;
          MergeWriteByteSet(loc, &state, zpg_eaddr, state.A);
        }
      }
      break;
    }
    case 0x91: { // STA (d),y
      const uint16_t zpg_addr = Next8();

      ByteSet addr_lo = GetByteSet(state, zpg_addr);
      ByteSet addr_hi = GetByteSet(state, (uint8_t)(zpg_addr + 1));

      if (addr_hi.Size() == 1 && addr_lo.Size() == 1 &&
          state.Y.Size() == 1) {

        // Then we have a definite address, and can overwrite.
        uint16_t eaddr =
          Word16(addr_hi.GetSingleton(), addr_lo.GetSingleton()) +
          state.Y.GetSingleton();
        WriteMemByteSet(loc, &state, eaddr, MemByteSet(state.A));

      } else {
        // Have to merge it everywhere.
        for (uint8_t hi : addr_hi) {
          for (uint8_t lo : addr_lo) {
            for (uint8_t y : state.Y) {
              uint16_t eaddr = Word16(hi, lo) + y;
              MergeWriteByteSet(loc, &state, eaddr, state.A);
            }
          }
        }
      }

      break;
    }

    case 0xa0: { // LDY #i
      uint8_t imm = Next8();
      state.Y = ByteSet::Singleton(imm);
      ZN(&state, state.Y);
      break;
    }
    case 0xac: { // LDY a
      uint16_t addr = Next16();
      state.Y = GetByteSet(state, addr);
      ZN(&state, state.Y);
      break;
    }
    case 0xa4: { // LDY d
      uint8_t addr = Next8();
      state.Y = GetByteSet(state, addr);
      ZN(&state, state.Y);
      break;
    }
    case 0xbc: { // LDY a,x
      uint16_t addr = Next16();
      state.Y.Clear();
      for (uint8_t v : state.X) {
        state.Y.AddSet(GetByteSet(state, addr + v));
      }
      ZN(&state, state.Y);
      break;
    }
    case 0xb4: { // LDY d,x
      uint8_t zpg_addr = Next8();
      state.Y.Clear();
      for (uint8_t v : state.X) {
        state.Y.AddSet(GetByteSet(state, zpg_addr + v));
      }
      ZN(&state, state.Y);
      break;
    }

    case 0xa2: { // LDX #i
      uint8_t imm = Next8();
      state.X = ByteSet::Singleton(imm);
      ZN(&state, state.X);
      break;
    }
    case 0xa6: { // LDX d
      uint16_t addr = Next8();
      state.X = GetByteSet(state, addr);
      ZN(&state, state.X);
      break;
    }
    case 0xae: { // LDX a
      uint16_t addr = Next16();
      state.X = GetByteSet(state, addr);
      ZN(&state, state.X);
      break;
    }
    case 0xbe: { // LDX a,y
      uint16_t addr = Next16();
      state.X.Clear();
      for (uint8_t v : state.Y) {
        state.X.AddSet(GetByteSet(state, addr + v));
      }
      ZN(&state, state.X);
      break;
    }
    case 0xb6: { // LDX d,y
      uint8_t zpg_addr = Next8();
      state.X.Clear();
      for (uint8_t v : state.Y) {
        // Zero-page wraparound.
        uint8_t eaddr = zpg_addr + v;
        state.X.AddSet(GetByteSet(state, eaddr));
      }
      ZN(&state, state.X);
      break;
    }

    case 0x6a: { // ROR
      ByteSet flags;
      std::tie(state.A, flags) = RotateRight(state, state.A);
      CombineFlags(&state, flags, N_FLAG | Z_FLAG | C_FLAG);
      break;
    }

    case 0x6e: { // ROR a
      uint16_t addr = Next16();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG | C_FLAG,
                      addr,
                      // No offset.
                      ByteSet::Singleton(0),
                      [&](const ByteSet &xs) {
                        return RotateRight(state, xs);
                      });
      break;
    }

    case 0x7e: { // ROR a,x
      uint16_t addr = Next16();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG | C_FLAG,
                      addr,
                      state.X,
                      [&](const ByteSet &xs) {
                        return RotateRight(state, xs);
                      });
      break;
    }

    case 0x69: { // ADC #i
      uint8_t imm = Next8();
      AddWithCarry(&state, ByteSet::Singleton(imm));
      break;
    }
    case 0x6d: { // ADC a
      uint16_t addr = Next16();
      ReadAddWithCarry(&state, addr, ByteSet::Singleton(0));
      break;
    }
    case 0x65: { // ADC d
      // A <- A + operand + carry
      uint16_t addr = Next8();
      ReadAddWithCarry(&state, addr, ByteSet::Singleton(0));
      break;
    }
    case 0x79: { // ADC a,y
      uint16_t addr = Next16();
      ReadAddWithCarry(&state, addr, state.Y);
      break;
    }
    case 0x7d: { // ADC a,x
      uint16_t addr = Next16();
      ReadAddWithCarry(&state, addr, state.X);
      break;
    }
    case 0x75: { // ADC d,x
      uint16_t addr = Next8();
      ReadAddWithCarry(&state, addr, state.X);
      break;
    }

    case 0xed: { // SBC a
      uint16_t addr = Next16();
      ReadSubtractWithCarry(&state, addr, ByteSet::Singleton(0));
      break;
    }
    case 0xe5: { // SBC d
      uint16_t addr = Next8();
      ReadSubtractWithCarry(&state, addr, ByteSet::Singleton(0));
      break;
    }
    case 0xf9: { // SBC a,y
      uint16_t addr = Next16();
      ReadSubtractWithCarry(&state, addr, state.Y);
      break;
    }
    case 0xfd: { // SBC a,x
      uint16_t addr = Next16();
      ReadSubtractWithCarry(&state, addr, state.X);
      break;
    }
    case 0xf5: { // SBC d,x
      uint16_t addr = Next8();
      ReadSubtractWithCarry(&state, addr, state.X);
      break;
    }
    case 0xe9: { // SBC #i
      uint8_t imm = Next8();
      SubtractWithCarry(&state, ByteSet::Singleton(imm));
      break;
    }

    case 0xa8: { // TAY
      state.Y = state.A;
      ZN(&state, state.Y);
      break;
    }
    case 0x98: { // TYA
      state.A = state.Y;
      ZN(&state, state.A);
      break;
    }

    case 0xaa: { // TAX
      state.X = state.A;
      ZN(&state, state.X);
      break;
    }
    case 0x8a: { // TXA
      state.A = state.X;
      ZN(&state, state.A);
      break;
    }

    case 0x84: { // STY d
      uint8_t zpg_addr = Next8();
      WriteMemByteSet(loc, &state, zpg_addr, MemByteSet(state.Y));
      break;
    }
    case 0x94: { // STY d,x
      uint8_t zpg_addr = Next8();
      if (state.X.Size() == 1) {
        uint8_t eaddr = zpg_addr + state.X.GetSingleton();
        WriteMemByteSet(loc, &state, eaddr, MemByteSet(state.Y));
      } else {
        for (uint8_t x : state.X) {
          uint8_t eaddr = zpg_addr + x;
          MergeWriteByteSet(loc, &state, eaddr, state.Y);
        }
      }
      break;
    }
    case 0x8c: { // STY a
      uint16_t addr = Next16();
      WriteMemByteSet(loc, &state, addr, MemByteSet(state.Y));
      break;
    }

    case 0x8e: { // STX a
      uint16_t addr = Next16();
      WriteMemByteSet(loc, &state, addr, MemByteSet(state.X));
      break;
    }
    case 0x86: { // STX d
      uint16_t addr = Next8();
      WriteMemByteSet(loc, &state, addr, MemByteSet(state.X));
      break;
    }


    case 0x6c: { // JMP (a)
      // Indirect jump. Like RTS, this could create a lot
      // of destinations if memory is uncertain, since
      // we have to combine the two bytes.
      uint16_t indirect_addr = Next16();

      ByteSet addr_lo = GetByteSet(state, indirect_addr);
      ByteSet addr_hi = GetByteSet(state, indirect_addr + 1);

      int accepted = 0, rejected = 0, born = 0;
      for (uint8_t hi : addr_hi) {
        for (uint8_t lo : addr_lo) {
          uint16_t target_addr = Word16(hi, lo);

          if (zoning.addr[target_addr] & Zoning::X) {
            accepted++;
            // TODO: This is another place to consider splitting blocks.
            BlockTag tag(current_label, target_addr);
            if (!HasBlock(tag)) born++;
            EnterBlock(tag, state);
          } else {
            rejected++;
          }
        }
      }

      if (inst_verbose > 0 || (born > 16 && !quiet)) {
        Print("Indirect JMP at " AYELLOW("{:04x}")
               " added " AORANGE("{}") " new blocks. "
               "[acc " AGREEN("{}") "; rej " ARED("{}") "]\n",
               instruction_pc, born, accepted, rejected);
        CHECK(born < 1000);
      }

      return;
    }

    case 0x05: { // ORA d
      uint16_t addr = Next8();
      ZNBools bools;
      ByteSet new_a;
      for (uint8_t a : state.A) {
        for (uint8_t o : GetByteSet(state, addr)) {
          uint8_t r = a | o;
          new_a.Add(r);
          if (r == 0) bools.has_zero = true;
          else bools.has_nonzero = true;
          if (r & 0x80) bools.has_neg = true;
          else bools.has_nonneg = true;
        }
      }
      state.A = std::move(new_a);
      CombineZNFlags(&state, bools);
      break;
    }
    case 0x0d: { // ORA a
      uint16_t addr = Next16();
      ZNBools bools;
      ByteSet new_a;
      for (uint8_t a : state.A) {
        for (uint8_t o : GetByteSet(state, addr)) {
          uint8_t r = a | o;
          new_a.Add(r);
          if (r == 0) bools.has_zero = true;
          else bools.has_nonzero = true;
          if (r & 0x80) bools.has_neg = true;
          else bools.has_nonneg = true;
        }
      }
      state.A = std::move(new_a);
      CombineZNFlags(&state, bools);
      break;
    }
    case 0x09: { // ORA #i
      const uint8_t imm = Next8();
      state.A = state.A.Map([imm](uint8_t v) { return v | imm; });
      ZN(&state, state.A);
      break;
    }

    case 0xce: { // DEC a
      uint16_t addr = Next16();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG,
                      addr,
                      // no offset
                      ByteSet::Singleton(0),
                      SimpleWithZN([](uint8_t v) { return v - 1; }));
      break;
    }
    case 0xde: { // DEC a,x
      uint16_t addr = Next16();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG,
                      addr,
                      state.X,
                      SimpleWithZN([](uint8_t v) { return v - 1; }));
      break;
    }
    case 0xc6: { // DEC d
      uint16_t addr = Next8();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG,
                      addr,
                      // no offset
                      ByteSet::Singleton(0),
                      SimpleWithZN([](uint8_t v) { return v - 1; }));
      break;
    }
    case 0xd6: { // DEC d,x
      uint16_t addr = Next8();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG,
                      addr,
                      state.X,
                      SimpleWithZN([](uint8_t v) { return v - 1; }));
      break;
    }

    case 0x49: { // EOR #i
      uint8_t imm = Next8();
      state.A = state.A.Map([imm](uint8_t v) {
          return imm ^ v;
        });
      ZN(&state, state.A);
      break;
    }
    case 0x45: { // EOR d
      uint8_t zpg_addr = Next8();
      ByteSet new_a;
      for (uint8_t a : state.A) {
        for (uint8_t m : GetByteSet(state, zpg_addr)) {
          new_a.Add(a ^ m);
        }
      }
      state.A = std::move(new_a);
      ZN(&state, state.A);
      break;
    }

    case 0xe0: { // CPX #i
      uint8_t imm = Next8();
      Compare(&state, state.X, ByteSet::Singleton(imm));
      break;
    }

    case 0xc0: { // CPY #i
      uint8_t imm = Next8();
      Compare(&state, state.Y, ByteSet::Singleton(imm));
      break;
    }
    case 0xc4: { // CPY d
      uint8_t zpg_addr = Next8();
      Compare(&state, state.Y, GetByteSet(state, zpg_addr));
      break;
    }
    case 0xcc: { // CPY a
      uint16_t addr = Next16();
      Compare(&state, state.Y, GetByteSet(state, addr));
      break;
    }

    case 0xc9: { // CMP #i
      uint8_t imm = Next8();
      Compare(&state, state.A, ByteSet::Singleton(imm));
      break;
    }
    case 0xc5: { // CMP d
      uint8_t zpg_addr = Next8();
      Compare(&state, state.A, GetByteSet(state, zpg_addr));
      break;
    }
    case 0xd5: { // CMP d,x
      uint8_t zpg_addr = Next8();
      Compare(&state, state.A,
              GetByteSetFromOffsetsZpg(state, zpg_addr, state.X));
      break;
    }
    case 0xcd: { // CMP a
      uint16_t addr = Next16();
      Compare(&state, state.A, GetByteSet(state, addr));
      break;
    }
    case 0xdd: { // CMP a,x
      uint16_t addr = Next16();
      Compare(&state, state.A,
              GetByteSetFromOffsets16(state, addr, state.X));
      break;
    }
    case 0xd9: { // CMP a,y
      uint16_t addr = Next16();
      Compare(&state, state.A,
              GetByteSetFromOffsets16(state, addr, state.Y));
      break;
    }

    case 0x24: { // BIT d
      uint16_t addr = Next8();
      Bit(&state, addr);
      break;
    }
    case 0x2C: { // BIT a
      uint16_t addr = Next16();
      Bit(&state, addr);
      break;
    }

    case 0x40: { // RTI

      // Unnecessary because we are about to exit. But this
      // is how it works.

      state.P.Clear();
      for (uint8_t sp : state.S) {
        uint16_t saddr = 0x0100 + sp;
        state.P.AddSet(RegByteSet(state.RAM(saddr).Map([](uint8_t flags) {
            // Note that this behavior differs in fceulib; see x6502.cc.
            return flags & ~B_FLAG;
          })));
      }

      // PC actually gets set to a value from the stack, but we have
      // nothing to do with it here (especially if it is not definite).
      pc = 0;

      // Flags byte and pc popped from stack.
      state.S = state.S.Map([](uint8_t v) {
          return v + 3;
        });

      // We treat this as terminal for the analysis, as everything
      // is happening within the nonmaskable interrupt, which is
      // the entry point.
      //
      // TODO: This would be a good place to record the "final" state
      // if we want to be able to ask what values are possible after a
      // frame ends.
      if (inst_verbose > 1) {
        Print(ANSI_DARK_GREEN "(return from interrupt)" "\n");
      }
      return;
    }

    case 0x29: { // AND #i
      uint8_t imm = Next8();
      state.A = state.A.Map([imm](uint8_t v) { return v & imm; });
      ZN(&state, state.A);
      break;
    }
    case 0x39: { // AND a,y
      uint16_t addr = Next16();
      ByteSet ms = GetByteSetFromOffsets16(state, addr, state.Y);

      ByteSet new_a;
      for (uint8_t m : ms) {
        for (uint8_t a : state.A) {
          new_a.Add(m & a);
        }
      }
      state.A = std::move(new_a);
      ZN(&state, state.A);
      break;
    }
    case 0x3d: { // AND a,x
      uint16_t addr = Next16();
      ByteSet ms = GetByteSetFromOffsets16(state, addr, state.X);

      ByteSet new_a;
      for (uint8_t m : ms) {
        for (uint8_t a : state.A) {
          new_a.Add(m & a);
        }
      }
      state.A = std::move(new_a);
      ZN(&state, state.A);
      break;
    }
    case 0x2d: { // AND a
      uint16_t addr = Next16();
      ByteSet new_a;
      for (uint8_t m : GetByteSet(state, addr)) {
        for (uint8_t a : state.A) {
          new_a.Add(m & a);
        }
      }
      state.A = std::move(new_a);
      ZN(&state, state.A);
      break;
    }
    case 0x25: { // AND d
      uint16_t addr = Next8();
      ByteSet new_a;
      for (uint8_t m : GetByteSet(state, addr)) {
        for (uint8_t a : state.A) {
          new_a.Add(m & a);
        }
      }
      state.A = std::move(new_a);
      ZN(&state, state.A);
      break;
    }


    case 0xe6: { // INC d
      uint16_t addr = Next8();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG,
                      addr,
                      // no offset
                      ByteSet::Singleton(0),
                      SimpleWithZN([](uint8_t v) { return v + 1; }));
      break;
    }
    case 0xee: { // INC a
      uint16_t addr = Next16();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG,
                      addr,
                      // no offset
                      ByteSet::Singleton(0),
                      SimpleWithZN([](uint8_t v) { return v + 1; }));
      break;
    }
    case 0xf6: { // INC d,x
      uint16_t addr = Next8();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG,
                      addr,
                      state.X,
                      SimpleWithZN([](uint8_t v) { return v + 1; }));
      break;
    }
    case 0xfe: { // INC a,x
      uint16_t addr = Next16();
      ReadModifyWrite(&state,
                      Z_FLAG | N_FLAG,
                      addr,
                      state.X,
                      SimpleWithZN([](uint8_t v) { return v + 1; }));
      break;
    }

    case 0x4a: { // LSR A
      // a <- a>>1

      ByteSet znc_flags;
      ByteSet new_a;
      std::tie(new_a, znc_flags) = LogicalShiftRight(state.A);

      state.A = std::move(new_a);
      CombineFlags(&state, znc_flags, Z_FLAG | N_FLAG | C_FLAG);
      break;
    }

    case 0x46: { // LSR d
      uint16_t addr = Next8();
      ReadModifyWrite(&state,
                      C_FLAG | Z_FLAG | N_FLAG,
                      addr,
                      // no offset
                      ByteSet::Singleton(0),
                      LogicalShiftRight);
      break;
    }
    case 0x4e: { // LSR a
      uint16_t addr = Next16();
      ReadModifyWrite(&state,
                      C_FLAG | Z_FLAG | N_FLAG,
                      addr,
                      // no offset
                      ByteSet::Singleton(0),
                      LogicalShiftRight);
      break;
    }

    case 0x56: { // LSR d,x
      uint16_t addr = Next8();
      ReadModifyWrite(&state,
                      C_FLAG | Z_FLAG | N_FLAG,
                      addr,
                      state.X,
                      LogicalShiftRight);
      break;
    }
    case 0x5e: { // LSR a,x
      uint16_t addr = Next16();
      ReadModifyWrite(&state,
                      C_FLAG | Z_FLAG | N_FLAG,
                      addr,
                      state.X,
                      LogicalShiftRight);
      break;
    }

    // Unused but easy instructions.

    case 0xBA: { // TSX
      state.X = state.S;
      ZN(&state, state.X);
      break;
    }

    case 0x9A: { // TXS
      state.S = state.X;
      ZN(&state, state.S);
      break;
    }

    default:
      LOG(FATAL) << "Unimplemented (and unexpected) instruction:\n" <<
        std::format("{:04x}: 0x{:02x} ({})",
                    pc - 1, opcode, Opcodes::opcode_name[opcode]) <<
        "\nIt was not seen when tracing real emulator execution, but\n"
        "it could just be that the model can't rule it out, or found\n"
        "a new possibility.\n";
    }

    // Stop if we've encountered a new block (i.e., there is a jump
    // into this block from elsewhere).
  } while (!block_index.contains(BlockTag(current_label, pc)));

  if (verbose > 1) {
    Print(ANSI_DARK_BLUE "(block ends)" ANSI_RESET "\n");
  }

  // If we get here, then the basic block has ended, but we treat
  // it as an unconditional jump to the next instruction.
  EnterBlock(BlockTag(current_label, pc), state);
}


// Write the current model as an .asm file with annotations on
// basic blocks.
void Modeling::WriteAnnotatedAssembly(const SourceMap &source_map,
                                      std::string_view filename) const {
  std::string out;
  auto invert = source_map.InvertCode();
  for (int line_num = 0; line_num < source_map.lines.size(); line_num++) {
    auto it = invert.find(line_num);
    if (it != invert.end()) {
      const uint16_t addr = it->second;
      auto bit = block_tags.find(addr);
      if (bit != block_tags.end()) {
        for (const BlockTag &tag : bit->second) {
          // Then we have a basic block starting here.
          auto blit = block_index.find(tag);
          CHECK(blit != block_index.end());
          const BasicBlock &block = blocks[blit->second];
          CHECK(block.tag == tag);
          AppendFormat(&out,
                       ";** {} **\n",
                       PlainTagString(block.tag));

          const State &state = block.state_in;

          AppendFormat(
              &out,
              "; A:{}\n"
              "; X:{}\n"
              "; Y:{}\n",
              state.A.DebugString(),
              state.X.DebugString(),
              state.Y.DebugString());
          // Show stack, at least if stack is definite.
          if (state.S.Size() == 1) {
            uint8_t sp = state.S.GetSingleton();

            for (int i = std::max((int)sp - 2, 0);
                 i < std::min((int)sp + 6, 0xFF);
                 i++) {
              AppendFormat(&out,
                           "; Stack[{:02x}] {} = {}\n",
                           i,
                           (i == sp) ? "**" : "  ",
                           state.RAM(0x100 + i).DebugString());
            }
          }
        }
      }
    }

    AppendFormat(&out,
                 "{}\n",
                 source_map.lines[line_num]);
  }

  Util::WriteFile(std::string(filename), out);
}

std::string Modeling::ErrorLocString(const ErrorLoc &loc) const {
  return std::format("PC: " AORANGE("{:04x}"), loc.pc);
}

void Modeling::CheckMemoryInvariants(const ErrorLoc &loc,
                                     const State &state, uint16_t addr) const {
  CHECK(addr < 2048);

  auto it = ram_constraints.find(addr);
  if (it == ram_constraints.end()) return;

  const ValueConstraint &vc = it->second;
  const MemByteSet &actual = state.ram[addr];
  if (!ByteSet::Subset(actual, vc.valid_values)) {
    Print("Memory invariant " AWHITE("{}") " violated."
          "At: {}\n"
          "Address " AYELLOW("{:04x}") " should contain only: {}\n"
          "But it contained: {}\n",
          vc.comment,
          ErrorLocString(loc),
          addr,
          vc.valid_values.DebugString(),
          actual.DebugString());
    LOG(FATAL) << "Memory invariant violation.\n";
  }
}


void Modeling::AddConstraint(const Constraint &c) {
  Print("Add constraint: {}\n", ColorConstraint(c));
  if (const AlwaysConstraint *always = std::get_if<AlwaysConstraint>(&c)) {
    // Top-level AND constraints are the same as individual ones.

    std::vector<std::shared_ptr<Form>> conj =
      SimplifyBoolFormula(always->form);

    for (const std::shared_ptr<Form> &form : conj) {
      (void)form;
      // Find formulas of the form ram[constant] in S, and promote
      // those to ValueConstraints.
      //
      // For other formulas, add them to a vector of all the always
      // formulas.
      LOG(FATAL) << "Unimplemented";
    }

  } else {
    Print("  (" ARED("not implemented") ")\n");
  }
}
