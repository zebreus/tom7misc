

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>
#include <unordered_set>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"
#include "../fceulib/x6502.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "image.h"
#include "timer.h"
#include "threadutil.h"

#include "tetris.h"
#include "nes-tetris.h"
#include "encoding.h"

#include "movie-maker.h"

using namespace std;
using uint8 = uint8_t;

static constexpr const char *SOLFILE = "best-solutions.txt";
static constexpr const char *ROMFILE = "tetris.nes";

// Save screenshot, cloning the const emulator and making a full step
// so that we have an image.
[[maybe_unused]]
static void Screenshot(const Emulator &emu, const std::string &filename) {
  std::vector<uint8> save = emu.SaveUncompressed();
  std::unique_ptr<Emulator> clone(Emulator::Create(ROMFILE));
  CHECK(clone.get() != nullptr);
  clone->LoadUncompressed(save);
  clone->StepFull(0, 0);

  ImageRGBA img(clone->GetImage(), 256, 256);
  img.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}

int main(int argc, char **argv) {
  Timer run_timer;

  
  std::unique_ptr<ImageRGBA> graphic(
      ImageRGBA::Load("graphic.png"));
  
  CHECK(graphic->Width() % 8 == 0);
  CHECK(graphic->Height() % 8 == 0);
  const int TILESW = graphic->Width() / 8;
  const int TILESH = graphic->Height() / 8;
  const int NUM = TILESW * TILESH;

  std::vector<std::vector<uint8_t>> patterns;
  for (int y = 0; y < TILESH; y++) {
    for (int x = 0; x < TILESW; x++) {
      vector<uint8> pat;
      for (int yy = 0; yy < 8; yy++) {
        uint8 b = 0;
        for (int xx = 0; xx < 8; xx++) {
          b <<= 1;
          auto [r, g_, b_, a_] =
            graphic->GetPixel(x * 8 + xx, y * 8 + yy);
          const bool on = r > 0x80;
          b |= on ? 1 : 0;
        }
        pat.push_back(b);
      }
      patterns.push_back(std::move(pat));
    }
  }
  CHECK((int)patterns.size() == NUM);

  // Now plan in parallel.

  std::mutex m;
      
  vector<vector<uint8>> moves =
    ParallelMapi(patterns,
                 [&m](int64_t idx, const vector<uint8> &pat) {
                   MovieMaker movie_maker(SOLFILE, ROMFILE, idx);
                   MovieMaker::Callbacks callbacks;
                   auto PlacedPiece = [&idx, &m](const Emulator &emu,
                                                 int pieces_done,
                                                 int pieces_total) {
                       MutexLock ml(&m);
                       printf("[%lld] Placed %d/%d.\n",
                              idx, pieces_done, pieces_total);
                     };
                   callbacks.placed_piece = PlacedPiece;
                   return movie_maker.Play(pat, callbacks);
                 },
                 12);

  printf("Planned in %.2f sec\n", run_timer.Seconds());

  int maxlen = 0;
  for (const auto &m : moves) maxlen = std::max(maxlen, (int)m.size());

  const int do_frames = maxlen + 8;  
  printf("%d nes frames\n", do_frames);
  
  std::vector<std::unique_ptr<Emulator>> emus;
  for (int i = 0; i < NUM; i++)
    emus.emplace_back(Emulator::Create(ROMFILE));
  
  Asynchronously async(16);

  for (int frame = 0; frame < do_frames; frame++) {
    ImageRGBA img(1920, 1080);
    img.Clear32(0x000000FF);
    
    for (int i = 0; i < NUM; i++) {
      CHECK(i < (int)emus.size());

      // Step each emu.
      // Would be nice to go into some holding pattern
      // after it ends...
      if (frame < (int)moves[i].size()) {
        uint8 input = moves[i][frame];
        emus[i]->StepFull(input, 0);
      }

      // And draw to frame.
      ImageRGBA eimg(emus[i]->GetImage(), 256, 256);

      const int MARGINW = 1920 - (256 * 7);
      const int GAPW = MARGINW / 8;
      const int MARGINH = 1080 - (256 * 3);
      const int GAPH = MARGINH / 4;
      int x = i % TILESW;
      int y = i / TILESW;
      img.BlendImage(GAPW + (x * (256 + GAPW)),
                     GAPH + (y * (256 + GAPH)),
                     eimg);
    }
    // write image frame in background
    async.Run([&m, do_frames, frame, img = std::move(img)]() {
        img.Save(StringPrintf("frame%05d.png", frame));

        if (frame % 10 == 0) {
          MutexLock ml(&m);
          printf("Wrote frame %d/%d\n", frame, do_frames);
        }
      });
  }
  
  return 0;
}
