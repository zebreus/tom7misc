
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

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
    "Use raw level ids [00-ff].\n";

  int major = strtol(argv[1], nullptr, 16);
  int minor = strtol(argv[2], nullptr, 16);
  CHECK(major >= 0 && major < 256 &&
        minor >= 0 && minor < 256) << major << "-" << minor << "?";
  std::string filename =
    (argc == 4) ? std::string(argv[3]) :
    std::format("map-{:02x}-{:02x}.png", major, minor);

  MakeMap(PackLevel(major, minor), filename);

  return 0;
}
