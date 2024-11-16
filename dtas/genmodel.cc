
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <cstdint>
#include <mutex>
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
#include "mario-util.h"
#include "mario.h"
#include "periodically.h"
#include "status-bar.h"
#include "threadutil.h"
#include "util.h"

static constexpr const char *ROMFILE = "mario.nes";

#ifndef AOT_INSTRUMENTATION
#error Please define AOT instrumentation for this one.
#endif

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

static void GenModel() {
  Bank prg = GetPRG();

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

  // The PC counts only include the address of the start of the
  // instruction. So when it is nonzero, we smear this to the
  // rest of the instruction bytes to get a contiguous region.
  IntervalCover<int64_t> cover(0);
  for (int addr = 0x8000; addr < 0x10000; addr++) {
    const int64_t count = pc_histo[addr];
    if (count > 0) {
      uint8_t opcode = prg.Read(addr);
      uint8_t len = Opcodes::opcode_size[opcode];
      for (int i = 0; i < len; i++) {
        cover.SetPoint(addr + i, count);
      }
    }
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
