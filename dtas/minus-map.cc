
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "base/stringprintf.h"
#include "image.h"
#include "minus.h"
#include "ansi.h"

#include "../fceulib/emulator.h"

#include "mario-util.h"

using SolutionRow = MinusDB::SolutionRow;
using Pos = MarioUtil::Pos;

static constexpr const char *ROMFILE = "mario.nes";

static void MakeMap(LevelId level, const std::string &filename) {
  MinusDB db;

  if (const auto &osol = db.GetSolution(level)) {
    const auto &[major, minor] = UnpackLevel(level);
    std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
    MarioUtil::WarpTo(emu.get(), major, minor, 0);
    std::vector<uint8_t> start = emu->SaveUncompressed();

    ImageRGBA map = MarioUtil::MakeMap(emu.get(), osol.value());

    emu->LoadUncompressed(start);
    std::vector<MarioUtil::Pos> path =
      MarioUtil::GetPath(emu.get(), osol.value());

    MarioUtil::DrawPath(path, &map, 0xFF0000AA);

    map.Save(filename);
    printf("Wrote %s\n", filename.c_str());

  } else {
    printf("No solution for %s.\n", ColorLevel(level).c_str());
  }
}

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 3 || argc == 4) << "Usage:\n"
    "./minus-map.exe maj min [filename]\n"
    "\n"
    "Use raw level ids [0-255]. Default output filename uses hex.\n";

  int major = atoi(argv[1]);
  int minor = atoi(argv[2]);
  std::string filename =
    (argc == 4) ? std::string(argv[3]) :
    StringPrintf("map-%02x-%02x.png", major, minor);

  MakeMap(PackLevel(major, minor), filename);

  return 0;
}
