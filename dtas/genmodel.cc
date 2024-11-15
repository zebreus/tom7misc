
#include <cstdio>
#include <memory>
#include <cstdint>
#include <mutex>
#include <vector>
#include <unordered_set>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/x6502.h"
#include "../fceulib/fc.h"

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "mario-util.h"
#include "mario.h"
#include "status-bar.h"
#include "evaluator.h"
#include "emulator-pool.h"
#include "arcfour.h"
#include "randutil.h"
#include "periodically.h"
#include "interval-cover.h"
#include "threadutil.h"

static constexpr const char *ROMFILE = "mario.nes";

#ifndef AOT_INSTRUMENTATION
#error Please define AOT instrumentation for this one.
#endif

static void GenModel() {
  // Not generating any model yet!
  // This is just collecting some statistics to assess the
  // feasibility.
  EmulatorPool emu_pool(ROMFILE);

  std::vector<int64_t> pc_histo(0x10000, 0);

  auto emu = emu_pool.AcquireClean();
  MarioUtil::WarpTo(emu.get(), 0xF4, 0x6B, 0);

  std::vector<uint8_t> start_state = emu->SaveUncompressed();
  Evaluator eval(&emu_pool, emu.get());

  // ArcFour rc("genmodel");

  std::unordered_set<uint16_t> reached;

  std::mutex m;
  Periodically status_per(0.5);
  StatusBar status(1);
  const int NUM_REPS = 10000;
  int done = 0;
  ParallelComp(
      10000,
      [&](int idx) {
        ArcFour rc(StringPrintf("genmodel.%lld", idx));
        auto emu = emu_pool.Acquire();
        emu->LoadUncompressed(start_state);

        // Clear AOT. The init was deterministic and doesn't need
        // to be modeled.
        for (int addr = 0; addr < 0x10000; addr++) {
          emu->GetFC()->X->pc_histo[addr] = 0;
        }

        // Run some frames.
        for (int i = 0; i < 500; i++) {
          // We generate all kinds of inputs here, including pausing.
          // But pausing is less interesting than other inputs.

          // Special case some extreme inputs.
          uint8_t b = 0;
          switch (idx) {
          case 0: b = 0; break;
          case 1: b = INPUT_B | INPUT_R; break;
          case 2: b = INPUT_B | INPUT_L; break;
          default:
            b = rc.Byte() & ~INPUT_T;
            if (idx % 8 == 7) {
              if (rc.Byte() < 32) {
                b |= INPUT_T;
              }
            }
          }

          emu->StepFull(b, 0);

          // If we died, then no need to track further code paths.
          if (eval.IsDead(emu.get())) {
            break;
          }
        }

        {
          MutexLock ml(&m);
          // Accumulate into histo and reached set.
          for (int addr = 0; addr < 0x10000; addr++) {
            auto *X = emu->GetFC()->X;
            if (X->pc_histo[addr] > 0) {
              reached.insert(addr);
              pc_histo[addr] += X->pc_histo[addr];
            }
          }
          done++;
          if (status_per.ShouldRun()) {
            status.Progressf(done, NUM_REPS, "%d insts reached",
                             (int)reached.size());
          }
        }
      },
      12);

  // Output histo.
  printf("There are %d reached instructions.\n",
         (int)reached.size());

  // This is maybe not a great representation, since for multibyte
  // instructions the PC never takes on the immediate value. So we
  // have an enormous number of holes. We should probably
  // disassemble the instruction at the address to get its length.
  IntervalCover<int64_t> cover(0);
  for (int addr = 0; addr < 0x10000; addr++) {
    cover.SetPoint(addr, pc_histo[addr]);
  }

  for (uint64_t pt = cover.First(); !cover.IsAfterLast(pt);
       pt = cover.Next(pt)) {
    IntervalCover<int64_t>::Span s = cover.GetPoint(pt);
    printf(AWHITE("%04x") "-" AWHITE("%04x") ": %lld\n", (int)s.start,
           (int)s.end, s.data);
  }
}



int main(int argc, char **argv) {
  ANSI::Init();

  GenModel();

  return 0;
}
