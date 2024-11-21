
#include "modeling.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"
#include "../fceulib/opcodes.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "byteset.h"

#define N_FLAG 0x80
#define V_FLAG 0x40
#define U_FLAG 0x20
#define B_FLAG 0x10
#define D_FLAG 0x08
#define I_FLAG 0x04
#define Z_FLAG 0x02
#define C_FLAG 0x01

State State::FromEmulator(const Emulator *emu, uint8_t sp) {
  State state;
  state.A = ByteSet::Top();
  state.X = ByteSet::Top();
  state.Y = ByteSet::Top();
  state.S = ByteSet::Singleton(sp);
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

void Modeling::EnterBlock(uint16_t addr, const State &state) {
  auto it = block_index.find(addr);
  if (it == block_index.end()) {
    // New block.
    const int bidx = (int)blocks.size();
    block_index[addr] = bidx;
    blocks.emplace_back(BasicBlock{.start_addr = addr, .state_in = state});
    dirty.Push(bidx);
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
    return state.ram[addr].ToByteSet();
  }

  // TODO: Special cases for memory-mapped addresses.

  // Otherwise, treat it as though any value is possible.
  return ByteSet::Top();
}

// Write *addr = s.
// If the address is in RAM, we simply set the state in ram to
// that set. Writes to ROM are ignored. Writes to memory-mapped
// regions may be treated specially.
void Modeling::WriteByteSet64(State *state, uint16_t addr,
                              const ByteSet64 &s) const {
  // ROM writes are ignored.
  if (addr >= rom.ORIGIN)
    return;

  // RAM.
  if (addr < 2048) {
    state->ram[addr] = s;
    return;
  }

  // TODO: Special cases for memory-mapped addresses.
};


// Compute the values of the Z and N flags in the status
// register, given the byteset. Preserves the values of
// the other flags.
void ZN(State *state, const ByteSet &s) {
  constexpr bool VERBOSE = false;
  CHECK(!s.Empty());
  if (VERBOSE) {
    printf("ZN flags for: {");
    for (uint8_t v : s) {
      printf("%02x, ", v);
    }
    printf("}\n");
  }

  ByteSet res;
  bool contains_z = s.Contains(0);
  bool contains_pos = false;
  bool contains_neg = false;
  for (int i = 0; i < 256; i++) {
    if (s.Contains(i)) {
      if (i < 0x80) {
        contains_pos = true;
      } else {
        contains_neg = true;
      }
    }
  }

  if (VERBOSE) {
    printf("Contains z: %c p: %c n: %c\n",
           contains_z ? 'X' : '_',
           contains_pos ? 'X' : '_',
           contains_neg ? 'X' : '_');
  }

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
  if (contains_z) zflags.push_back(Z_FLAG);
  if (contains_pos || contains_neg) zflags.push_back(0);

  std::vector<uint8_t> nflags;
  if (contains_pos || contains_z) nflags.push_back(0);
  if (contains_neg) nflags.push_back(N_FLAG);

  CHECK(!zflags.empty() && !nflags.empty());

  for (uint8_t b : s) {
    b &= ~(Z_FLAG | N_FLAG);

    for (uint8_t z : zflags) {
      for (uint8_t n : nflags) {
        res.Add(b | z | n);
      }
    }
  }
}

void Modeling::Expand() {
  // TODO:
  //
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
  //

  printf(AWHITE("== starting iteration! ==") "\n");
  printf("Number of blocks: %d\n", (int)blocks.size());

  if (dirty.Empty())
    return;

  // PERF: It should be feasible to do the analysis in parallel.
  // We just need to synchronize the updated states that we
  // deduce.
  const int block_idx = dirty.Pop();
  const BasicBlock &block = blocks[block_idx];

  State state = block.state_in;
  uint16_t pc = block.start_addr;

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

      CHECK(has_true || has_false);
      if (has_true) {
        // Do the branch.
        State truestate = state;
        truestate.P = truestate.P.Map([mask, true_case](uint8_t v) {
            return (v & ~mask) | true_case;
          });
        // The displacement is relative to the instruction after
        // the branch, which is what the pc now represents.
        EnterBlock(pc + displacement, std::move(truestate));
      }

      if (has_false) {
        // Don't take the branch; "jump" to the next
        // instruction. We know that the tested flag
        // has the value false_case when the branch was
        // not taken.
        state.P = state.P.Map([mask, false_case](uint8_t v) {
            return (v & ~mask) | false_case;
          });
        EnterBlock(pc, state);
      }

    };

  // Keep reading instructions until we reach the end of the block.
  do {
    // Read the opcode, which advances the PC past it.
    const uint8_t opcode = Next8();

    printf("Opcode: " ABLUE("%02x") " " AGREY("(%s)") "\n",
           opcode, Opcodes::opcode_name[opcode]);

    switch (opcode) {
    case 0x4c: { // JMP a
      uint16_t addr = Next16();
      EnterBlock(addr, state);
      return;
    }
    case 0xf0: { // BEQ *+d
      return Branch(Z_FLAG, Z_FLAG);
    }
    case 0xd0: { // BNE *+d
      return Branch(Z_FLAG, 0);
    }
    case 0xad: { // LDA a
      uint16_t addr = Next16();
      state.A = GetByteSet(state, addr);
      ZN(&state, state.A);
      break;
    }
    case 0xc8: { // INY
      LOG(FATAL) << "Unimplemented 'INY'";
      break;
    }
    case 0x29: { // AND #i
      uint8_t imm = Next8();
      state.A = state.A.Map([imm](uint8_t v) { return v + imm; });
      ZN(&state, state.A);
      break;
    }
    case 0xbd: { // LDA a,x
      LOG(FATAL) << "Unimplemented 'LDA a,x'";
      break;
    }
    case 0x85: { // STA d
      LOG(FATAL) << "Unimplemented 'STA d'";
      break;
    }
    case 0x88: { // DEY
      // y <- y-1
      state.Y = state.Y.Map([](uint8_t v) { return v - 1; });
      ZN(&state, state.Y);
      break;
    }
    case 0x4a: { // LSR A
      // a <- a>>1

      bool has_odd = false;
      bool has_even = false;
      // After shifting.
      bool has_zero = false;
      bool has_nonzero = false;
      for (int i = 0; i < 256; i++) {
        if (state.A.Contains(i)) {
          if (i & 1) has_odd = true;
          else has_even = true;

          if (i == 0 || i == 1) {
            has_zero = true;
          } else {
            has_nonzero = true;
          }
        }

      }
      CHECK(has_odd || has_even);
      std::vector<uint8_t> carry_flags;
      if (has_odd) carry_flags.push_back(C_FLAG);
      if (has_even) carry_flags.push_back(0);

      CHECK(has_zero || has_nonzero);
      std::vector<uint8_t> zero_flags;
      if (has_zero) zero_flags.push_back(Z_FLAG);
      if (has_nonzero) zero_flags.push_back(0);

      state.A = state.A.Map([](uint8_t v) { return v >> 1; });
      // N is impossible since the high bit will be zero after
      //   shifting.
      // Z flag is as usual.
      // Carry flag from the bit shifted off.

      ByteSet new_flags;
      for (uint8_t v : state.P) {
        v &= ~(Z_FLAG | N_FLAG);

        for (uint8_t z : zero_flags) {
          for (uint8_t c : carry_flags) {
            new_flags.Add(v | z | c);
          }
        }
      }
      state.P = std::move(new_flags);
      break;
    }
    case 0xca: { // DEX
      // x <- x-1.
      state.X = state.X.Map([](uint8_t v) { return v - 1; });
      ZN(&state, state.X);
      break;
    }
    case 0x10: { // BPL *+d
      LOG(FATAL) << "Unimplemented 'BPL *+d'";
      break;
    }
    case 0x8d: { // STA a
      uint16_t addr = Next16();
      WriteByteSet64(&state, addr, ByteSet64(state.A));
      break;
    }
    case 0x99: { // STA a,y
      LOG(FATAL) << "Unimplemented 'STA a,y'";
      break;
    }
    case 0xa5: { // LDA d
      LOG(FATAL) << "Unimplemented 'LDA d'";
      break;
    }
    case 0x20: { // JSR a
      // Potentially hard?
      // It writes into the stack, so want to have more
      // certainty about where the stack pointer points.
      // Otherwise this will make it look like the entire
      // range $0100-$01FF can become the PC after any
      // JSR is executed. We might also conflate stack
      // data and addresses when we push/pop.
      //
      // Optimistically, we always jump to a subroutine
      // with the stack pointer in the same place, and
      // we always return to a single caller. This might
      // be true and is checkable. We could start by
      // seeing what happens in emulation.
      //
      // Turns out that the stack pointer is (empirically)
      // always 0xFC when we call the NMI. Most instructions
      // that use the stack are (empirically) executed with
      // a single fixed stack pointer value, or else a small
      // number. So it seems plausible that this simple
      // approach will still work.

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
        state.ram[saddr] = ByteSet64::Singleton(stored_pc & 0xFF);
        state.ram[saddr - 1] = ByteSet64::Singleton(stored_pc >> 8);
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
          state.ram[saddr].Add(stored_pc & 0xFF);
          state.ram[saddr - 1].Add(stored_pc >> 8);
        }
        state.S = state.S.Map([](uint8_t v) {
            return v - 2;
          });
      }

      uint16_t daddr = Next16();
      EnterBlock(daddr, state);

      // Ends basic block.
      return;
    }
    case 0xc9: { // CMP #i
      LOG(FATAL) << "Unimplemented 'CMP #i'";
      break;
    }
    case 0x90: { // BCC *+d
      LOG(FATAL) << "Unimplemented 'BCC *+d'";
      break;
    }
    case 0x60: { // RTS
      // See JSR for some details.
      for (uint8_t sp : state.S) {
        uint16_t saddr = 0x0100 + sp;
        if (state.ram[saddr + 1].Size() == 1 &&
            state.ram[saddr + 2].Size() == 1) {
          // Only one value for the stack at this point.
          // This is the reasonable case.
          uint16_t hi = state.ram[saddr + 1].GetSingleton();
          uint16_t lo = state.ram[saddr + 2].GetSingleton();
          // +1 is just how it works; a quirk of JSR and RTS.
          uint16_t raddr = ((hi << 8) | lo) + 1;
          State ret_state = state;
          // Pop from this specific stack offset.
          ret_state.S = ByteSet::Singleton(sp + 2);
          EnterBlock(raddr, std::move(ret_state));
        } else {
          // Ugh.
          printf("Warning: More than one possible RTS destination.\n");
          LOG(FATAL) << "unimplemented";
        }
      }

      // Ends the block.
      return;
    }
    case 0xe8: { // INX
      LOG(FATAL) << "Unimplemented 'INX'";
      break;
    }
    case 0x68: { // PLA
      LOG(FATAL) << "Unimplemented 'PLA'";
      break;
    }
    case 0xb1: { // LDA (d),y
      LOG(FATAL) << "Unimplemented 'LDA (d),y'";
      break;
    }
    case 0x48: { // PHA
      LOG(FATAL) << "Unimplemented 'PHA'";
      break;
    }
    case 0x0a: { // ASL
      LOG(FATAL) << "Unimplemented 'ASL'";
      break;
    }
    case 0xa9: { // LDA #i
      uint8_t v = Next8();
      state.A = ByteSet::Singleton(v);
      ZN(&state, state.A);
      break;
    }
    case 0x2a: { // ROL
      LOG(FATAL) << "Unimplemented 'ROL'";
      break;
    }
    case 0xb0: { // BCS *+d
      LOG(FATAL) << "Unimplemented 'BCS *+d'";
      break;
    }
    case 0xc0: { // CPY #i
      LOG(FATAL) << "Unimplemented 'CPY #i'";
      break;
    }
    case 0x05: { // ORA d
      LOG(FATAL) << "Unimplemented 'ORA d'";
      break;
    }
    case 0x18: { // CLC
      LOG(FATAL) << "Unimplemented 'CLC'";
      break;
    }
    case 0xb9: { // LDA a,y
      LOG(FATAL) << "Unimplemented 'LDA a,y'";
      break;
    }
    case 0x9d: { // STA a,x
      LOG(FATAL) << "Unimplemented 'STA a,x'";
      break;
    }
    case 0xa8: { // TAY
      LOG(FATAL) << "Unimplemented 'TAY'";
      break;
    }
    case 0xe0: { // CPX #i
      LOG(FATAL) << "Unimplemented 'CPX #i'";
      break;
    }
    case 0xb5: { // LDA d,x
      LOG(FATAL) << "Unimplemented 'LDA d,x'";
      break;
    }
    case 0xa0: { // LDY #i
      LOG(FATAL) << "Unimplemented 'LDY #i'";
      break;
    }
    case 0xac: { // LDY a
      uint16_t addr = Next16();
      state.Y = GetByteSet(state, addr);
      ZN(&state, state.Y);
      break;
    }
    case 0x69: { // ADC #i
      LOG(FATAL) << "Unimplemented 'ADC #i'";
      break;
    }
    case 0xf9: { // SBC a,y
      LOG(FATAL) << "Unimplemented 'SBC a,y'";
      break;
    }
    case 0xa4: { // LDY d
      LOG(FATAL) << "Unimplemented 'LDY d'";
      break;
    }
    case 0xa2: { // LDX #i
      LOG(FATAL) << "Unimplemented 'LDX #i'";
      break;
    }
    case 0x86: { // STX d
      LOG(FATAL) << "Unimplemented 'STX d'";
      break;
    }
    case 0xa6: { // LDX d
      LOG(FATAL) << "Unimplemented 'LDX d'";
      break;
    }
    case 0x38: { // SEC
      LOG(FATAL) << "Unimplemented 'SEC'";
      break;
    }
    case 0x91: { // STA (d),y
      LOG(FATAL) << "Unimplemented 'STA (d),y'";
      break;
    }
    case 0xe6: { // INC d
      LOG(FATAL) << "Unimplemented 'INC d'";
      break;
    }
    case 0x7e: { // ROR a,x
      LOG(FATAL) << "Unimplemented 'ROR a,x'";
      break;
    }
    case 0x65: { // ADC d
      LOG(FATAL) << "Unimplemented 'ADC d'";
      break;
    }
    case 0xf5: { // SBC d,x
      LOG(FATAL) << "Unimplemented 'SBC d,x'";
      break;
    }
    case 0x79: { // ADC a,y
      LOG(FATAL) << "Unimplemented 'ADC a,y'";
      break;
    }
    case 0x98: { // TYA
      LOG(FATAL) << "Unimplemented 'TYA'";
      break;
    }
    case 0xae: { // LDX a
      uint16_t addr = Next16();
      state.X = GetByteSet(state, addr);
      ZN(&state, state.X);
      break;
    }
    case 0xc5: { // CMP d
      LOG(FATAL) << "Unimplemented 'CMP d'";
      break;
    }
    case 0x46: { // LSR d
      LOG(FATAL) << "Unimplemented 'LSR d'";
      break;
    }
    case 0x95: { // STA d,x
      LOG(FATAL) << "Unimplemented 'STA d,x'";
      break;
    }
    case 0xbe: { // LDX a,y
      LOG(FATAL) << "Unimplemented 'LDX a,y'";
      break;
    }
    case 0x84: { // STY d
      LOG(FATAL) << "Unimplemented 'STY d'";
      break;
    }
    case 0x30: { // BMI *+d
      LOG(FATAL) << "Unimplemented 'BMI *+d'";
      break;
    }
    case 0x6c: { // JMP (a)
      LOG(FATAL) << "Unimplemented 'JMP (a)'";
      break;
    }
    case 0x09: { // ORA #i
      const uint8_t imm = Next8();
      state.A = state.A.Map([imm](uint8_t v) { return v | imm; });
      ZN(&state, state.A);
      break;
    }
    case 0xce: { // DEC a
      LOG(FATAL) << "Unimplemented 'DEC a'";
      break;
    }
    case 0x49: { // EOR #i
      LOG(FATAL) << "Unimplemented 'EOR #i'";
      break;
    }
    case 0xe9: { // SBC #i
      LOG(FATAL) << "Unimplemented 'SBC #i'";
      break;
    }
    case 0xaa: { // TAX
      LOG(FATAL) << "Unimplemented 'TAX'";
      break;
    }
    case 0xed: { // SBC a
      LOG(FATAL) << "Unimplemented 'SBC a'";
      break;
    }
    case 0x26: { // ROL d
      LOG(FATAL) << "Unimplemented 'ROL d'";
      break;
    }
    case 0xcd: { // CMP a
      LOG(FATAL) << "Unimplemented 'CMP a'";
      break;
    }
    case 0x3d: { // AND a,x
      LOG(FATAL) << "Unimplemented 'AND a,x'";
      break;
    }
    case 0x8c: { // STY a
      LOG(FATAL) << "Unimplemented 'STY a'";
      break;
    }
    case 0xc6: { // DEC d
      LOG(FATAL) << "Unimplemented 'DEC d'";
      break;
    }
    case 0x6a: { // ROR
      LOG(FATAL) << "Unimplemented 'ROR'";
      break;
    }
    case 0x75: { // ADC d,x
      LOG(FATAL) << "Unimplemented 'ADC d,x'";
      break;
    }
    case 0xd5: { // CMP d,x
      LOG(FATAL) << "Unimplemented 'CMP d,x'";
      break;
    }
    case 0xd9: { // CMP a,y
      LOG(FATAL) << "Unimplemented 'CMP a,y'";
      break;
    }
    case 0x24: { // BIT d
      LOG(FATAL) << "Unimplemented 'BIT d'";
      break;
    }
    case 0x7d: { // ADC a,x
      LOG(FATAL) << "Unimplemented 'ADC a,x'";
      break;
    }
    case 0x40: { // RTI
      LOG(FATAL) << "Unimplemented 'RTI'";
      break;
    }
    case 0x45: { // EOR d
      LOG(FATAL) << "Unimplemented 'EOR d'";
      break;
    }
    case 0x39: { // AND a,y
      LOG(FATAL) << "Unimplemented 'AND a,y'";
      break;
    }
    case 0xee: { // INC a
      LOG(FATAL) << "Unimplemented 'INC a'";
      break;
    }
    case 0xde: { // DEC a,x
      LOG(FATAL) << "Unimplemented 'DEC a,x'";
      break;
    }
    case 0x8e: { // STX a
      LOG(FATAL) << "Unimplemented 'STX a'";
      break;
    }
    case 0x2c: { // BIT a
      LOG(FATAL) << "Unimplemented 'BIT a'";
      break;
    }
    case 0x6d: { // ADC a
      LOG(FATAL) << "Unimplemented 'ADC a'";
      break;
    }
    case 0x8a: { // TXA
      LOG(FATAL) << "Unimplemented 'TXA'";
      break;
    }
    case 0xdd: { // CMP a,x
      LOG(FATAL) << "Unimplemented 'CMP a,x'";
      break;
    }
    case 0x2d: { // AND a
      LOG(FATAL) << "Unimplemented 'AND a'";
      break;
    }
    case 0x25: { // AND d
      LOG(FATAL) << "Unimplemented 'AND d'";
      break;
    }
    case 0x2e: { // ROL a
      LOG(FATAL) << "Unimplemented 'ROL a'";
      break;
    }
    case 0x0e: { // ASL a
      LOG(FATAL) << "Unimplemented 'ASL a'";
      break;
    }
    case 0x0d: { // ORA a
      LOG(FATAL) << "Unimplemented 'ORA a'";
      break;
    }
    case 0xe5: { // SBC d
      LOG(FATAL) << "Unimplemented 'SBC d'";
      break;
    }
    case 0xbc: { // LDY a,x
      LOG(FATAL) << "Unimplemented 'LDY a,x'";
      break;
    }
    case 0x4e: { // LSR a
      LOG(FATAL) << "Unimplemented 'LSR a'";
      break;
    }
    default:
      LOG(FATAL) << "Unimplemented (and unexpected) instruction " <<
        StringPrintf("%02x (%s)", opcode, Opcodes::opcode_name[opcode]) <<
        ". It was not seen when tracing real emulator execution.";
    }

    // Stop if we've encountered a new block (i.e., there is a jump
    // into this block from elsewhere).
  } while (!block_index.contains(pc));

  // If we get here, then the basic block has ended, but we treat
  // it as an unconditional jump to the next instruction.
  EnterBlock(pc, state);
}
