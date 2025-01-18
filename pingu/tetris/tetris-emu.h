
#ifndef _PINGU_TETRIS_EMU_H
#define _PINGU_TETRIS_EMU_H

#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>

#include "../fceulib/emulator.h"

#include "base/logging.h"
#include "image.h"

#include "nes-tetris.h"

inline RNGState GetRNG(const Emulator &emu) {
  RNGState state;

  state.rng1 = emu.ReadRAM(MEM_RNG1);
  state.rng2 = emu.ReadRAM(MEM_RNG2);
  state.last_drop = emu.ReadRAM(MEM_LAST_DROP);
  state.drop_count = emu.ReadRAM(MEM_DROP_COUNT);

  return state;
}

// 10x20, row major.
inline std::vector<uint8_t> GetBoard(const Emulator &emu) {
  std::vector<uint8_t> ret(10 * 20);
  for (int i = 0; i < 10 * 20; i++) {
    ret[i] = emu.ReadRAM(MEM_BOARD_START + i);
  }
  return ret;
}

inline void SaveScreenshot(const std::string &filename, Emulator *emu) {
  std::vector<uint8_t> save = emu->SaveUncompressed();
  emu->StepFull(0, 0);

  ImageRGBA img(emu->GetImage(), 256, 256);
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());

  emu->LoadUncompressed(save);
}

// True if we're in the middle of clearing a line.
inline bool IsLineClearing(const Emulator &emu) {
  return emu.ReadRAM(MEM_CURRENT_PIECE) == 0x13;
}

// Heuristic; not sure if this is correct.
// (Probably program counter would be definitive when
// outside of NMI.)
inline bool IsPaused(const Emulator &emu) {
  return emu.ReadRAM(0x00a0) == 0x70 &&
    emu.ReadRAM(0x00a1) == 0x77 &&
    emu.ReadRAM(0x00a2) == 0x05;
}

#endif
