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

int main(int argc, char **argv) {
  Timer run_timer;
  

  std::vector<uint8_t> pattern = { 0x2e, 0x00, 0x00 };
  
  std::vector<std::vector<uint8_t>> patterns;

  vector<uint8> moves =
    [&pattern]() {
      MovieMaker movie_maker(SOLFILE, ROMFILE, 0xCAFE);
      MovieMaker::Callbacks callbacks;
      auto PlacedPiece = [](const Emulator &emu,
                            int pieces_done,
                            int pieces_total) {
          printf("Placed %d/%d.\n",
                 pieces_done, pieces_total);
        };
      callbacks.placed_piece = PlacedPiece;
      return movie_maker.Play(pattern, callbacks);
    }();

  printf("Planned in %.2f sec\n", run_timer.Seconds());

  std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
  
  Asynchronously async(16);

  std::mutex m;
  
  const int do_frames = (int)moves.size();
  int frame_idx = 0;
  for (int frame = 0; frame < do_frames; frame++) {
    ImageRGBA img(1920, 1080);
    img.Clear32(0x000000FF);
    
    emu->StepFull(moves[frame], 0);

    // And draw to frame.
    ImageRGBA eimg(emu->GetImage(), 256, 256);

    // Only the middle 240 pixels are interesting, so
    // this leaves a gap of 16. 8 on each side.

    img.BlendImage((1920 - 246 * 4) / 2, 8 * 4, eimg.ScaleBy(4));


    // For cosmetic reasons, don't save black-out frames (pauses)
    auto [r, g, b, a] = eimg.GetPixel(0, 0);
    if (b > 0x30) {
      frame_idx++;
      // write image frame in background
      async.Run([&m, do_frames, &frame_idx, img = std::move(img)]() {
          img.Save(StringPrintf("rendered%05d.png", frame_idx - 1));

          if (frame_idx % 10 == 0) {
            MutexLock ml(&m);
            printf("Wrote frame %d/%d\n", frame_idx, do_frames);
          }
        });
    }
  }
  
  return 0;
}
