
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"

#include "ansi-image.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bounds.h"
#include "byteset.h"
#include "image.h"
#include "mario-util.h"
#include "mario.h"
#include "modeling.h"
#include "periodically.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"
#include "zoning.h"

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

  #if 0
  // Ban some data regions that contain 0x20, the opcode for JSR.
  // These can look like possible valid return addresses, and
  // currently we are using heuristics to trace execution.
  // MetatileGraphics_Low to JumpEngine
  for (int addr = 0x8b08; addr < 0x8e04; addr++) {
    modeling.zoning.addr[addr] &= ~Zoning::X;
  }
  #endif

  modeling.zoning = Zoning::FromFile("mario.zoning");
  CHECK((modeling.zoning.addr[0x8e01] & Zoning::X) == 0);


  // These are the entry points that we actually care about:
  // NonMaskableInterrupt is the entry point for the frame,
  // which happens during vblank. We actually know that we
  // enter with the captured emulator state, the first time.
  modeling.EnterBlock(BlockTag("", NONMASKABLE_INTERRUPT), start_state);

  // TODO: Sprite0Hit will spin waiting for a PPU status
  // register to change. We'll want to at least include
  // that as a possibility!

  CHECK(!modeling.Done());
  /*
  printf("%d blocks. %d dirty\n",
         (int)modeling.blocks.size(),
         (int)modeling.dirty.Size());
  */

  static constexpr int VERBOSE_ITER_START = 142500;

  // TODO: Animate progress. Each iteration can be a frame.
  // For each block we can render the state (it's like 2048x256 bits?).
  // but I guess that's just enormous! There are like a thousand blocks.
  // So maybe we just want to render a few of them.

  // Write a diagnostic image to show the state of the model.
  // Slow! TODO: We could do this every 10 frames or whatever.
  static constexpr bool WRITE_IMAGES = false;

  Asynchronously save_async(12);

  auto MaybeRender = [&save_async](const Modeling &modeling, int frame) {
      if (!WRITE_IMAGES) return;

      ByteSet zero;
      constexpr int LEFT = 8;
      constexpr int TOP = 8;
      constexpr int MARGIN = 4;
      constexpr int REGS1 = 16 + LEFT + MARGIN;
      constexpr int REGS2 = REGS1 + 256 + MARGIN;
      constexpr int RAM = REGS2 + 256 + MARGIN;

      constexpr int MAX_BLOCKS = 2048;
      constexpr int WIDTH = 2048 + 256 + 256 + 16 + MARGIN * 6;
      std::vector<std::pair<uint16_t, int>> indices;
      for (const auto &[tag, idx] : modeling.block_index) {
        // This just strips the labels, so we end up with duplicate
        // tags by addr. But that is fine for visualization.
        indices.emplace_back(tag.addr, idx);
      }
      // TODO: Would be better if this was using the label too.
      std::sort(indices.begin(), indices.end(),
                [](const auto &a, const auto &b) {
                  return a < b;
                });


      ImageRGBA img(WIDTH, MAX_BLOCKS);
      img.Clear32(0x000000FF);

      auto Draw3 = [&img](int x, int y,
                          const ByteSet &r, const ByteSet &g, const ByteSet &b) {
          for (int i = 0; i < 256; i++) {
            uint32_t c = 0x000000FF;
            if (r.Contains(i)) c |= 0xFF000000;
            if (g.Contains(i)) c |= 0x00FF0000;
            if (b.Contains(i)) c |= 0x0000FF00;
            img.SetPixel32(x + i, y, c);
          }
        };

      auto MapCount = [](int size) -> uint8_t {
          // We are interested in low counts like 1, 2, 3, etc. Spend more
          // dynamic range on these parts.
          if (size < 16) {
            return size * 8;
          } else {
            return 127 + (size >> 1);
          }
        };

      for (int y = 0; y < MAX_BLOCKS; y++) {
        int yy = y + TOP;
        if (y < indices.size()) {
          const auto &[addr, index] = indices[y];
          const BasicBlock &block = modeling.blocks[index];
          const State &state = block.state_in;
          // draw address
          for (int i = 0; i < 16; i++) {
            if (addr & (1 << (15 - i))) {
              img.SetPixel32(i + LEFT, yy, 0xFFFFFFFF);
            }
          }

          // Now draw machine state at entry.
          // A, X, Y in RGB.
          Draw3(REGS1, yy, state.A, state.X, state.Y);
          // S, P in RG.
          Draw3(REGS2, yy, state.S, state.P, zero);

          // Could fit memory as a count of bytes, I think.
          for (int m = 0; m < 2048; m++) {
            uint8_t b = MapCount(state.ram[m].Size());
            img.SetPixel(RAM + m, yy, b, b, b, 0xFF);
          }
        }
      }

      save_async.Run([image = std::move(img), frame]() {
          image.Save(StringPrintf("model-%d.png", frame));
        });
    };

  StatusBar status(1);
  Timer timer;
  Periodically status_per(1);
  int64_t iters = 0;
  std::vector<int> num_blocks_at_iter;
  while (!modeling.Done()) {
    MaybeRender(modeling, iters);
    if (iters == 15000) break;

    if (status_per.ShouldRun()) {
      Bounds bounds;
      for (int x = 0; x < num_blocks_at_iter.size(); x++) {
        bounds.Bound(x, num_blocks_at_iter[x]);
      }
      ImageRGBA img(70, 60);
      Bounds::Scaler scaler = bounds.Stretch(img.Width(), img.Height()).FlipY();
      float px = 0, py = img.Height();
      for (int x = 0; x < num_blocks_at_iter.size(); x++) {
        bounds.Bound(x, num_blocks_at_iter[x]);
        const auto &[sx, sy] = scaler.Scale(x, num_blocks_at_iter[x]);
        img.BlendLineAA32(px, py, sx, sy, 0xFFFF00FF);
        px = sx;
        py = sy;
      }
      printf("%s\n", ANSIImage::HalfChar(img).c_str());

      int64_t denom = modeling.blocks.size();
      int64_t remain = modeling.dirty.Size();
      [[maybe_unused]]
      int64_t numer = denom - remain;
      // status.Progressf(numer, denom, ACYAN("%lld") " iters.", iters);
      printf("%lld iters, " ACYAN("%lld") " blocks in %s\n",
             iters,
             modeling.blocks.size(),
             ANSI::Time(timer.Seconds()).c_str());
    }

    if (iters > VERBOSE_ITER_START) {
      printf(AWHITE("== starting iteration %lld ==") "\n", iters);
      printf("Number of blocks: " ACYAN("%d") "\n",
             (int)modeling.blocks.size());
      modeling.verbose = 2;
    }
    modeling.Expand();
    num_blocks_at_iter.push_back(modeling.blocks.size());
    iters++;
  }
  status.Statusf("Done in " ACYAN("%lld") " iters.\n", iters);

}

int main(int argc, char **argv) {
  ANSI::Init();

  Model();

  return 0;
}
