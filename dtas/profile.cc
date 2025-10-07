
// Generates statistics about the target program by running
// it with instrumentation enabled.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../fceulib/emulator.h"
#include "../fceulib/fc.h"
#include "../fceulib/opcodes.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/x6502.h"

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "emulator-pool.h"
#include "evaluator.h"
#include "interval-cover.h"
#include "map-util.h"
#include "mario-util.h"
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

static void GenProfile() {
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

  std::unordered_set<uint16_t> reached;

  std::unordered_map<uint16_t, std::unordered_map<uint8_t, int64_t>>
     stack_histo;

  // Should set higher for real times.
  // const int NUM_REPS = 10000;
  const int NUM_REPS = 1000;


  std::mutex m;
  Periodically status_per(0.5);
  StatusBar status(1);
  int done = 0;
  ParallelComp(
      NUM_REPS,
      [&](int idx) {
        ArcFour rc(std::format("genprofile.{}", idx));
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
          auto *X = emu->GetFC()->X;
          // Accumulate into histo and reached set.
          for (int addr = 0; addr < 0x10000; addr++) {
            if (X->pc_histo[addr] > 0) {
              reached.insert(addr);
              pc_histo[addr] += X->pc_histo[addr];
            }
          }
          for (const auto &[addr, m] : X->stack_histo) {
            auto &mm = stack_histo[addr];
            for (const auto &[s, count] : m) {
              mm[s] += count;
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
  Print("There are " AWHITE("{}") " reached instructions.\n",
         (int)reached.size());

  std::unordered_map<uint8_t, int64_t> opcode_count;

  // The PC counts only include the address of the start of the
  // instruction. So when it is nonzero, we smear this to the
  // rest of the instruction bytes to get a contiguous region.
  IntervalCover<int64_t> cover(0);
  for (int addr = 0x8000; addr < 0x10000; addr++) {
    const int64_t count = pc_histo[addr];
    if (count > 0) {
      uint8_t opcode = prg.Read(addr);
      opcode_count[opcode] += count;
      uint8_t len = Opcodes::opcode_size[opcode];
      for (int i = 0; i < len; i++) {
        cover.SetPoint(addr + i, count);
      }
    }
  }

  Print("There are " AWHITE("{}") " distinct opcodes reached.\n",
        opcode_count.size());
  for (const auto &[opcode, count] :
         CountMapToDescendingVector(opcode_count)) {
    if (count > 0) {
      Print("{}" AGREY(" × ") AYELLOW("{:02x}") ": {}\n",
            count, opcode, Opcodes::opcode_name[opcode]);
    }
  }

  for (uint64_t pt = cover.First(); !cover.IsAfterLast(pt);
       pt = cover.Next(pt)) {
    IntervalCover<int64_t>::Span s = cover.GetPoint(pt);
    Print(AWHITE("{:04x}") "-" AWHITE("{:04x}") ": {}\n",
          s.start, s.end, s.data);
  }

  // Output the model as "assembly."
  std::string content;
  for (uint64_t pt = cover.First(); !cover.IsAfterLast(pt);
       pt = cover.Next(pt)) {
    IntervalCover<int64_t>::Span s = cover.GetPoint(pt);
    if (s.data > 0) {
      AppendFormat(&content, ";; [{:04x}, {:04x}) {} times\n",
                   s.start, s.end, s.data);
      for (int addr = s.start; addr < s.end; /* in loop */) {
        if (auto lo = MarioUtil::GetLabel(addr)) {
          AppendFormat(&content, "{}:\n", lo.value());
        }
        const uint8_t opcode = prg.Read(addr);
        std::string stacks;
        auto it = stack_histo.find(addr);
        if (it != stack_histo.end()) {
          AppendFormat(&content, ";; sp =");
          for (const auto &[sp, count] : it->second) {
            AppendFormat(&content, " {:02x}×{}", sp, count);
          }
          AppendFormat(&content, "\n");
        }

        AppendFormat(&content, "{:04x}:  {}\n",
                     addr,
                     Opcodes::opcode_name[opcode]);
        addr += Opcodes::opcode_size[opcode];
      }
    }
  }
  Util::WriteFile("mario.model", content);
  Print("Wrote mario.model.\n");
}


int main(int argc, char **argv) {
  ANSI::Init();

  GenProfile();

  return 0;
}
