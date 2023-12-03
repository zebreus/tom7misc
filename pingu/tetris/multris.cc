

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

static constexpr const char *ROMFILE = "tetris.nes";

int main(int argc, char **argv) {
  Timer run_timer;

  static constexpr int NUM = 51200 / 8;
  std::vector<Emulator *> emus;
  std::vector<std::vector<uint8_t>> saves;

  for (int i = 0; i < NUM; i++) {
	Emulator *emu = Emulator::Create(ROMFILE);
	CHECK(emu != nullptr) << i;
	emu->StepFull(0, 0);
	emus.push_back(emu);
	saves.push_back(emu->SaveUncompressed());
	// Leak some fds. 
	// CHECK(nullptr != fopen("multris.o", "r"));

	auto v = Encoding::ParseSolutions("solutions.txt");
  }

  printf("Loaded %d in %.2fs\n", NUM, run_timer.Seconds());

  Timer delete_timer;
  for (Emulator *emu : emus) delete emu;
  printf("and deleted in %.2fs\n", delete_timer.Seconds());  
  
  return 0;
}
