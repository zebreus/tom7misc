
#include "mario-util.h"

#include <cmath>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"

#include "ansi-image.h"
#include "base/logging.h"
#include "image-resize.h"
#include "image.h"
#include "mario.h"
#include "util.h"

namespace {
struct MemoryMap {
  MemoryMap(const std::string &filename) : names(0x800, "") {
    for (const std::string &line : Util::NormalizeLines(
             Util::ReadFileToLines(filename))) {
      if (line.empty() || line[0] == '#') continue;

      std::vector<std::string> parts = Util::Split(line, ' ');
      CHECK(parts.size() == 2) << "Bad memory map: " << filename;

      int addr = strtol(parts[0].c_str(), nullptr, 16);
      CHECK(addr >= 0 && addr < 0x800) << "Bad address: " << filename;
      if (names[addr].empty()) names[addr] = parts[1];
    }

    // For the next part we want the invariant that there is always
    // some name of an earlier address.
    if (names[0].empty()) names[0] = "zero";
    int prev = 0;
    for (int i = 1; i < 0x800; i++) {
      if (names[i].empty()) {
        names[i] = std::format("{}+{}", names[prev], i - prev);
      } else {
        prev = i;
      }
    }
  }

  std::vector<std::string> names;
};

struct Labels {
  Labels(const std::string &filename) {
    for (const std::string &line : Util::NormalizeLines(
             Util::ReadFileToLines(filename))) {
      if (line.empty() || line[0] == '#') continue;
      std::vector<std::string> parts = Util::Split(line, ' ');
      CHECK(parts.size() == 2) << "Bad memory map: " << filename;

      int addr = strtol(parts[0].c_str(), nullptr, 16);
      names[addr] = parts[1];
    }
  }

  std::unordered_map<uint16_t, std::string> names;
};

static const MemoryMap &GetRamMap() {
  static const MemoryMap *mm = new MemoryMap("mario.map");
  return *mm;
}

static const Labels &GetRomLabels() {
  static const Labels *ls = new Labels("mario-prg.map");
  return *ls;
}

}  // namespace


// Place the emulator state at the standard beginning of world-level.
//
// There's a way to warp to world-1 which is semi-legitimate, using the
// "continue" feature (hold A on menu). You put the target world in RAM
// and then do a "warm boot" of the game. You have to meet some conditions
// for the warm start to be accepted:
//   - WARM_BOOT_VALIDATION needs to contain 0xA5.
//   - The "top score" display digits all need to be 0-9.
// This video explains the conditions:
// https://youtu.be/hrFHNgJlJSg?si=RkI_4qYsp-PDcXL7
//
// The problem with this is that it only lets you warp to the first level
// of the world. Instead we just modify the next major/minor world number
// so that we can access all 256^2 levels.

// PERF: In some use cases (e.g. validation) it would be nice to skip
// emulating the first 33 frames, and instead just restore from the
// title screen.
void MarioUtil::WarpTo(Emulator *emu,
                       uint8_t major, uint8_t minor, uint8_t halfway) {
  // XXXXX!
  static constexpr int MENU_FRAMES = 33;
  for (int i = 0; i < MENU_FRAMES; i++) {
    // Wait 33 frames for the menu.
    emu->StepFull(0, 0);
  }

  // This is the first frame that we can start the game. We need only
  // press start for one frame to do that. But first modify memory
  // to do the desired warp.
  emu->SetRAM(WORLD_MAJOR, major);
  emu->SetRAM(WORLD_MINOR, minor);
  // XXX This does not match the game, since it does not increment
  // after transition levels like 1-2. What would be the best way
  // to set it outside the main game?
  emu->SetRAM(WORLD_MINOR_DISPLAY, minor);
  emu->SetRAM(HALFWAY_PAGE, halfway);

  emu->StepFull(INPUT_T, 0);

}

MarioUtil::Pos MarioUtil::GetPos(const Emulator *emu) {
  uint8_t xhi = emu->ReadRAM(PLAYER_X_HI);
  uint8_t xlo = emu->ReadRAM(PLAYER_X_LO);

  uint16_t x = (uint16_t(xhi) << 8) | xlo;

  // 0 if above screen, 1 if on screen, 2+ if below
  uint8_t yscreen = emu->ReadRAM(PLAYER_Y_SCREEN);
  uint8_t ypos = emu->ReadRAM(PLAYER_Y);

  uint16_t y = (uint16_t(yscreen) << 8) | ypos;

  return Pos{.x = x, .y = y};
}

ImageRGBA MarioUtil::Screenshot(Emulator *emu) {
  std::vector<uint8_t> rgba8 = emu->GetImage();
  return ImageRGBA(std::move(rgba8), 256, 256);
}

ImageRGBA MarioUtil::ScreenshotAny(Emulator *emu) {
  std::vector<uint8_t> save = emu->SaveUncompressed();
  emu->StepFull(0, 0);
  std::vector<uint8_t> rgba8 = emu->GetImage();
  ImageRGBA img(std::move(rgba8), 256, 256);
  emu->LoadUncompressed(save);
  return img;
}

std::string MarioUtil::ScreenshotANSI(Emulator *emu) {
  ImageRGBA ss = ScreenshotAny(emu).Crop32(0, 0, 256, 240);
  return ANSIImage::HalfChar(ImageResize::Resize(ss, 256 / 4, 240 / 4));
}

ImageRGBA MarioUtil::MakeMap(Emulator *emu, const std::vector<uint8_t> &movie) {
  std::unique_ptr<ImageRGBA> map(new ImageRGBA(1024, 256));
  auto GrowToFit = [&map](int width) {
      if (map->Width() < width) {
        std::unique_ptr<ImageRGBA> wider(new ImageRGBA(map->Width() * 2, 256));
        wider->CopyImage(0, 0, *map);
        map = std::move(wider);
      }
    };

  int max_width = 256;
  for (int idx = 0; idx < (int)movie.size(); idx++) {
    emu->StepFull(movie[idx], 0);

    // XXX: There is some small error here, probably due to some
    // trick with the scroll when mario is moving fast. It's
    // serviceable but we should figure it out to make it look
    // perfect!
    int screenx = (emu->ReadRAM(SCREENLEFT_X_HI) << 8) |
      emu->ReadRAM(SCREENLEFT_X_LO);

    GrowToFit(screenx + 256);
    max_width = std::max(max_width, screenx + 256);
    ImageRGBA screen = Screenshot(emu);
    map->CopyImage(screenx, 0, screen);
  }

  return map->Crop32(0, 0, max_width, 256);
}

static inline bool EqPos(const MarioUtil::Pos &a,
                         const MarioUtil::Pos &b) {
  return a.x == b.x && a.y == b.y;
}

std::vector<MarioUtil::Pos> MarioUtil::GetPath(
    Emulator *emu,
    const std::vector<uint8_t> &movie) {
  std::vector<Pos> positions;
  positions.reserve(movie.size() + 1);

  Pos last_pos{.x = 9999, .y = 9998};

  for (int idx = 0; idx < (int)movie.size(); idx++) {
    Pos p = GetPos(emu);
    if (!EqPos(p, last_pos)) {
      positions.push_back(p);
    }
    last_pos = p;

    emu->StepFull(movie[idx], 0);
  }

  Pos p = GetPos(emu);
  if (!EqPos(p, last_pos)) {
    positions.push_back(p);
  }
  return positions;
}

void MarioUtil::DrawPath(const std::vector<Pos> &path,
                         ImageRGBA *img,
                         uint32_t color) {
  for (int i = 1; i < path.size(); i++) {
    const Pos &a = path[i - 1];
    const Pos &b = path[i];
    // The y offset may be for top-left of tall mario. This
    // places the y position at about small mario's moustache.
    img->BlendThickLine32(a.x, a.y - 232, b.x, b.y - 232, 2.5f, color);
  }
}

std::string MarioUtil::FormatNum(uint64_t n) {
  if (n > 1'000'000) {
    double m = n / 1'000'000.0;
    if (m >= 1'000'000.0) {
      return std::format("{:.1f}T", m / 1'000'000.0);
    } else if (m >= 1000.0) {
      return std::format("{:.1f}B", m / 1000.0);
    } else if (m >= 100.0) {
      return std::format("{}M", (int)std::round(m));
    } else if (m > 10.0) {
      return std::format("{:.1f}M", m);
    } else {
      // TODO: Integer division. color decimal place and suffix.
      return std::format("{:.2f}M", m);
    }
  } else {
    return Util::UnsignedWithCommas(n);
  }
}

std::string MarioUtil::DescribeAddress(uint16_t addr) {
  if (addr >= 0x800) return std::format("{:04x}", addr);
  return GetRamMap().names[addr];
}

std::optional<std::string> MarioUtil::GetLabel(uint16_t addr) {
  const auto &l = GetRomLabels().names;
  auto it = l.find(addr);
  if (it == l.end()) return std::nullopt;
  return {it->second};
}
