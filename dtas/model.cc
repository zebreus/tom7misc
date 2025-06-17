
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"

#include "ansi-image.h"
#include "ansi.h"
#include "assemble.h"
#include "base/logging.h"
#include "bounds.h"
#include "byte-set.h"
#include "formula.h"
#include "image.h"
#include "mario-util.h"
#include "mario.h"
#include "modeling.h"
#include "periodically.h"
#include "sourcemap.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"
#include "zoning.h"

static constexpr const char *ASMFILE = "mario.asm";
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
  // Assemble from scratch.
  Assembly assembly = Assembly::Assemble(ASMFILE);

  std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
  CHECK(emu.get() != nullptr) << "Failed to load " << ROMFILE;
  MarioUtil::WarpTo(emu.get(), 0xF4, 0x6B, 0);
  // The stack pointer is 0xFC when entering NonMaskableInterrupt.
  const State start_state = State::FromEmulator(emu.get(), 0xFC);

  // TODO: Can get this from Assembly, though we should probably
  // verify that it matches the historic ROM.
  Modeling modeling(GetPRG());

  CHECK(assembly.banks.size() == 1) << "Only one bank is supported";

  // modeling.zoning = Zoning::FromFile("mario.zoning");
  // SourceMap source_map = SourceMap::FromFile("mario.sourcemap");

  modeling.zoning = assembly.banks[0].zoning;
  const SourceMap &source_map = assembly.banks[0].source_map;
  CHECK((modeling.zoning.addr[0x8e01] & Zoning::X) == 0);

  for (const Constraint &c : assembly.banks[0].constraints) {
    modeling.AddConstraint(c);
  }

  // TODO: Get these from annotations in the assembly file.
  modeling.ram_constraints[OPER_MODE] = ValueConstraint{
    .comment = "Oper Mode",
    .valid_values = ByteSet({0x00, 0x01, 0x02, 0x03}),
    };

  printf("Constraint: %s\n",
         modeling.ram_constraints[OPER_MODE].
         valid_values.DebugString().c_str());

  // These are the entry points that we actually care about:
  // NonMaskableInterrupt is the entry point for the frame,
  // which happens during vblank. We actually know that we
  // enter with the captured emulator state, the first time.
  modeling.EnterBlock(BlockTag("top", NONMASKABLE_INTERRUPT), start_state);

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

  // Write a diagnostic image to show the state of the model.
  static constexpr bool WRITE_IMAGES = false;
  // It's slow if you do it often!
  static constexpr int WRITE_EVERY = 50;

  Asynchronously save_async(0); // (12);

  auto MaybeRender = [&save_async](const Modeling &modeling, int frame) {
      if (!WRITE_IMAGES) return;
      if ((frame % WRITE_EVERY) != 0) return;
      printf("Render frame %d...\n", frame);

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

      auto Draw3 =
        [&img](int x, int y,
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

      uint16_t prev_addr = 0xFFFF;
      for (int y = 0; y < MAX_BLOCKS; y++) {
        int yy = y + TOP;
        if (y < indices.size()) {
          const auto &[addr, index] = indices[y];
          const BasicBlock &block = modeling.blocks[index];
          const State &state = block.state_in;

          // Make it clearer if we have split this block.
          uint32_t addr_color = (addr == prev_addr) ? 0x999999FF : 0xFFFFFFFF;
          // draw address
          for (int i = 0; i < 16; i++) {
            if (addr & (1 << (15 - i))) {
              img.SetPixel32(i + LEFT, yy, addr_color);
            }
          }
          prev_addr = addr;

          // Now draw machine state at entry.
          // A, X, Y in RGB.
          Draw3(REGS1, yy, state.A, state.X, state.Y);
          // S, P in RG.
          Draw3(REGS2, yy, state.S, state.P, zero);

          // Could fit memory as a count of bytes, I think.
          for (int m = 0; m < 2048; m++) {
            uint8_t b = MapCount(state.RAM(m).Size());
            img.SetPixel(RAM + m, yy, b, b, b, 0xFF);
          }
        }
      }

      save_async.Run([image = std::move(img), frame]() {
          std::string filename = std::format("model-{}.png", frame);
          image.Save(filename);
          printf("Wrote %s\n", filename.c_str());
        });
    };

  StatusBar status(1);
  Timer timer;
  Periodically status_per(2);
  int64_t iters = 0;
  std::vector<int> num_blocks_at_iter;
  modeling.verbose = 1;

  // modeling.verbose_addrs = {{0x8e04, 3}, {0x8e16, 3}};
  // The entire JumpEngine routine.
  /*
  for (uint16_t je : {
        0x8e04, 0x8e05, 0x8e06, 0x8e07,
        0x8e09, 0x8e0a, 0x8e0c, 0x8e0c, 0x8e0d, 0x8e0f, 0x8e11, 0x8e12,
        0x8e14, 0x8e16}) {
    modeling.verbose_addrs[je] = 3;
  }
  */

  auto Write = [&modeling, &source_map](const std::string &file) {
      modeling.WriteAnnotatedAssembly(source_map, file);
      printf("Wrote " AGREEN("%s") "\n", file.c_str());
    };

  while (!modeling.Done()) {
    MaybeRender(modeling, iters);
    // if (iters == 15000) break;

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
             (int64_t)modeling.blocks.size(),
             ANSI::Time(timer.Seconds()).c_str());
    }

    if (iters > VERBOSE_ITER_START) {
      printf(AWHITE("== starting iteration %lld ==") "\n", iters);
      printf("Number of blocks: " ACYAN("%d") "\n",
             (int)modeling.blocks.size());
      modeling.verbose = 2;
    }

    // Check assertions.
    // These should probably be imported from elsewhere (e.g. as annotations in
    // the source code) but it turns out C++ is a general purpose programming
    // language!

    constexpr uint16_t JUMP_ENGINE = 0x8e04;
    if (modeling.block_tags.contains(JUMP_ENGINE)) {
      // Relevant JumpEngine call graphs:
      //   top.8175.8215   (SkipSprite0 > OperModeExecutionTree)

      if (modeling.block_tags[JUMP_ENGINE].size() == 1 &&
          modeling.block_tags[JUMP_ENGINE][0].label == "top.8175.8215") {
        const auto &tag = modeling.block_tags[JUMP_ENGINE][0];
        auto idx_it = modeling.block_index.find(tag);
        CHECK(idx_it != modeling.block_index.end());
        const BasicBlock &block = modeling.blocks[idx_it->second];
        auto OK = [&]() {
            const auto &bs = block.state_in.RAM(OPER_MODE);
            for (int v = 4; v < 256; v++) {
              if (block.state_in.A.Contains(v)) return false;
              if (bs.Contains(v)) return false;
            }
            return true;
          };

        if (!OK()) {
          printf("At iteration " AWHITE("%lld") ", "
                 ARED("invariant violation") ":\n", iters);
          printf("%s\n", block.state_in.DebugString().c_str());
          Write("violation.asm");
          LOG(FATAL) << "Invariant violation.";
        }

      } else {
        printf("JumpEngine:");
        for (const BlockTag &tag : modeling.block_tags[JUMP_ENGINE]) {
          printf(" %s", Modeling::TagString(tag).c_str());
        }
        printf("\n");
        Write("violation.asm");
        LOG(FATAL) << "Unexpected jumpengine call graph.";
      }
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
