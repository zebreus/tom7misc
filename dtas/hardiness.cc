
#include <memory>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"

#include "image.h"
#include "timer.h"
#include "periodically.h"
#include "threadutil.h"

#include "base/stringprintf.h"
#include "base/logging.h"

using uint8 = uint8_t;

static constexpr int NUM_THREADS = 12;

static constexpr int MENU_FRAMES = 83;
static constexpr int WIN_FRAMES = 1500;


static std::mutex db_mutex;
static ImageRGBA *database = nullptr;
static Periodically *save_per = nullptr;

struct Map {
  // What row are we currently filling?
  int row = 0;
  ImageRGBA img;

  // XXX HERE
};


static void WorkThread(const std::vector<uint8> &movie, int address) {

  for (int i = 0; i < MENU_FRAMES; i++) {
    emu->Step(movie[i], 0);
  }
}

int main(int argc, char **argv) {

  std::unique_ptr<Emulator> emu{Emulator::Create("mario.nes")};
  CHECK(emu.get() != nullptr);

  std::vector<uint8> movie =
    SimpleFM7::ReadInputs("mario-long.fm7");
  CHECK(!movie.empty());




  return 0;
}
