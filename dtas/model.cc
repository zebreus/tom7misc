
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <unordered_set>

#include "../fceulib/emulator.h"
#include "../fceulib/fc.h"
#include "../fceulib/opcodes.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/x6502.h"

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "emulator-pool.h"
#include "evaluator.h"
#include "image.h"
#include "interval-cover.h"
#include "map-util.h"
#include "mario-util.h"
#include "mario.h"
#include "modeling.h"
#include "periodically.h"
#include "status-bar.h"
#include "threadutil.h"
#include "util.h"

static constexpr const char *ROMFILE = "mario.nes";

#ifndef AOT_INSTRUMENTATION
#error Please define AOT instrumentation for this one.
#endif

static Bank GetPRG() {
  static constexpr int HEADER_SIZE = 16;
  std::vector<uint8_t> rombytes = Util::ReadFileBytes(ROMFILE);
  CHECK(rombytes.size() >= 16 + 32768);

  CHECK(0 == memcmp("NES\x1a", rombytes.data(), 4)) <<
    "Not a NES file.";

  const int prg_bytes = rombytes[4] * 16384;
  [[maybe_unused]]
  const int chr_bytes = rombytes[5] * 8192;

  CHECK(rombytes.size() >= HEADER_SIZE + prg_bytes) << "Not enough "
    "bytes in file for purported PRG size?";
  Bank bank;
  for (int i = 0; i < std::min(prg_bytes, 32768); i++) {
    bank.rom.push_back(rombytes[HEADER_SIZE + i]);
  }
  return bank;
}

static void Model() {
  std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
  MarioUtil::WarpTo(emu.get(), 0xF4, 0x6B, 0);
  // std::vector<uint8_t> start_save = emu->SaveUncompressed();
  // The stack pointer is 0xFC when entering NonMaskableInterrupt.
  const State start_state = State::FromEmulator(emu.get(), 0xFC);

  Modeling modeling(GetPRG());

  // These are the entry points that we actually care about:
  // NonMaskableInterrupt is the entry point for the frame,
  // which happens during vblank. We actually know that we
  // enter with the captured emulator state, the first time.
  modeling.EnterBlock(NONMASKABLE_INTERRUPT, start_state);

  // TODO: Sprite0Hit will spin waiting for a PPU status
  // register to change. We'll want to at least include
  // that as a possibility!

  CHECK(!modeling.Done());
  /*
  printf("%d blocks. %d dirty\n",
         (int)modeling.blocks.size(),
         (int)modeling.dirty.Size());
  */

  StatusBar status(1);
  Periodically status_per(1);
  int64_t iters = 0;
  for (int64_t iters = 0; !modeling.Done(); iters++) {
    if (status_per.ShouldRun()) {
      int64_t denom = modeling.blocks.size();
      int64_t remain = modeling.dirty.Size();
      [[maybe_unused]]
      int64_t numer = denom - remain;
      // status.Progressf(numer, denom, ACYAN("%lld") " iters.", iters);
    }

    printf(AWHITE("== starting iteration %lld ==") "\n", iters);
    printf("Number of blocks: " ACYAN("%d") "\n",
           (int)modeling.blocks.size());
    modeling.Expand();
  }
  status.Statusf("Done in " ACYAN("%lld") " iters.\n", iters);


}


int main(int argc, char **argv) {
  ANSI::Init();

  Model();

  return 0;
}
