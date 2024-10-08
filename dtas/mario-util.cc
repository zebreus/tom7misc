
#include "mario-util.h"

#include <cmath>
#include <string>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "image.h"

#include "mario.h"
#include "base/stringprintf.h"
#include "util.h"

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
  for (int i = 0; i < 33; i++) {
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
      return StringPrintf("%.1fT", m / 1'000'000.0);
    } else if (m >= 1000.0) {
      return StringPrintf("%.1fB", m / 1000.0);
    } else if (m >= 100.0) {
      return StringPrintf("%dM", (int)std::round(m));
    } else if (m > 10.0) {
      return StringPrintf("%.1fM", m);
    } else {
      // TODO: Integer division. color decimal place and suffix.
      return StringPrintf("%.2fM", m);
    }
  } else {
    return Util::UnsignedWithCommas(n);
  }
}
