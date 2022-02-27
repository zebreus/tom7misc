

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

#include "tetris.h"
#include "nes-tetris.h"
#include "encoding.h"

#include "movie-maker.h"

using namespace std;
using uint8 = uint8_t;

static constexpr const char *SOLFILE = "solutions.txt";
static constexpr const char *ROMFILE = "tetris.nes";

// Save screenshot, cloning the const emulator and making a full step
// so that we have an image.
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

static void Retried(const Emulator &emu, int retry_count, Piece expected) {
  if (retry_count > 64) {
    static bool first = true;
    if (first) Screenshot(emu, "stuck.png");
    first = false;

    const uint8 cur_byte = emu.ReadRAM(MEM_CURRENT_PIECE);
    const Shape cur = (Shape)cur_byte;
    const Shape next_byte = (Shape)emu.ReadRAM(MEM_NEXT_PIECE);
    const uint8 rng1 = emu.ReadRAM(MEM_RNG1);
    const uint8 rng2 = emu.ReadRAM(MEM_RNG2);
    const uint8 last_drop = emu.ReadRAM(MEM_LAST_DROP);
    const uint8 drop_count = emu.ReadRAM(MEM_DROP_COUNT);
    printf("%d tries " // "(at %d/%d in %d). "
           "cur %c got nxt=%c want %c RNG %02x%02x.%02x.%02x\n",
           retry_count,
           // schedule_idx, (int)schedule.size(), (int)outmovie.size(),
           PieceChar(DecodePiece(cur)),
           PieceChar(DecodePiece(next_byte)),
           PieceChar(expected),
           rng1, rng2, last_drop, drop_count);
  }
} 

static void PlacedPiece(const Emulator &emu,
                        int pieces_done, int pieces_total) {
  if (pieces_done % 25 == 0) {
    Screenshot(emu, StringPrintf("encoded%d.png", pieces_done));

    printf("Saved after dropping %d/%d pieces.\n",
           pieces_done, pieces_total);
  }
}  

int main(int argc, char **argv) {
  Timer run_timer;

  // std::vector<uint8_t> pattern = {129, 0, 36, 44, 0, 68, 56, 0};
  // const std::vector<uint8_t> pattern =
  // {0xd7, 0xe7, 0xd3, 0x1b, 0x43, 0xcf, 0xf2, 0xde};
  // const int seed = 2510;

  const int seed = 2766;
  const std::vector<uint8_t> pattern =
    {0x18, 0x79, 0xf8, 0xaf, 0xe5, 0x36, 0x72, 0x35};

  MovieMaker movie_maker(SOLFILE, "tetris.nes", seed);

  int pieces = 0;
  MovieMaker::Callbacks callbacks;
  callbacks.game_start =
    [&pieces](const Emulator &emu, int num_pieces) {
      printf("Starting with schedule of size %d\n",
             num_pieces);
      pieces = num_pieces;
      // Screenshot(emu, "start.png");
    };
  callbacks.retried = Retried;
  callbacks.placed_piece = PlacedPiece;
  
  std::vector<uint8_t> movie = movie_maker.Play(pattern, callbacks);
  const Emulator *emu = movie_maker.GetEmu();
  Screenshot(*emu, "encoded-done.png");

  SimpleFM2::WriteInputs("encoded.fm2",
                         "tetris.nes",
                         "base64:Ww5XFVjIx5aTe5avRpVhxg==",
                         movie);
  printf("Done! Dropped %d pieces in %d frames, %lld steps, %.2f sec\n",
         pieces,
         (int)movie.size(),
         movie_maker.StepsExecuted(),
         run_timer.Seconds());
  
  return 0;
}
