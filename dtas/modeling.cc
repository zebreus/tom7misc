
#include "modeling.h"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <bitset>

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
