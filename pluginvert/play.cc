
#include "SDL.h"
#include "SDL_main.h"
#include "sdl/sdlutil.h"
#include "sdl/font.h"

#include <CL/cl.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <cmath>
#include <chrono>
#include <algorithm>
#include <tuple>
#include <utility>
#include <set>
#include <vector>
#include <map>
#include <unordered_set>
#include <deque>
#include <shared_mutex>

#include "base/stringprintf.h"
#include "base/logging.h"
#include "arcfour.h"
#include "util.h"
#include "vector-util.h"
#include "threadutil.h"
#include "randutil.h"
#include "base/macros.h"
#include "color-util.h"
#include "image.h"
#include "lines.h"
#include "rolling-average.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/emulator.h"

#include "network.h"

#include "clutil.h"
#include "timer.h"
#include "top.h"

#include "ntsc2d.h"
#include "problem.h"

#include "../bit7/embed9x9.h"

#define FONTCHARS " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789`-=[]\\;',./~!@#$%^&*()_+{}|:\"<>?" /* removed icons */
#define FONTSTYLES 7

static constexpr int VERBOSE = 2;

using namespace std;

using uint8 = uint8_t;
// Better compatibility with CL.
using uchar = uint8_t;

using uint32 = uint32_t;
using uint64 = uint64_t;



// Graphics.
#define FONTWIDTH 9
#define FONTHEIGHT 16
static Font *font = nullptr;
#define SCREENW (NES_WIDTH * 2)
#define SCREENH (NES_HEIGHT * 2)
static SDL_Surface *screen = nullptr;

// Thread-safe, so shared between train and ui threads.
// static CL *global_cl = nullptr;

std::shared_mutex print_mutex;
#define Printf(fmt, ...) do {                           \
    WriteMutexLock Printf_ml(&print_mutex);             \
    printf(fmt, ##__VA_ARGS__);                         \
    fflush(stdout);                                     \
  } while (0);

static void BlitImage(const ImageRGBA &img, int xpos, int ypos) {
  // PERF should invest in fast blit of ImageRGBA to SDL screen
  for (int y = 0; y < img.Height(); y++) {
    for (int x = 0; x < img.Width(); x++) {
      int xx = xpos + x;
      int yy = ypos + y;
      if (yy >= 0 && yy < SCREENH &&
          xx >= 0 && xx < SCREENW) {
        auto [r, g, b, _] = img.GetPixel(x, y);
        sdlutil::drawpixel(screen, xpos + x, ypos + y, r, g, b);
      }
    }
  }
}

static uint8 FloatByte(float f) {
  int x = roundf(f * 255.0f);
  return std::clamp(x, 0, 255);
}

static std::tuple<uint8, uint8, uint8> inline FloatColor(float f) {
  if (f > 0.0f) {
    uint8 v = FloatByte(f);
    return {0, v, 20};
  } else {
    uint8 v = FloatByte(-f);
    return {v, 0, 20};
  }
}

static std::tuple<uint8, uint8, uint8> inline ErrorFloatColor(float f) {
  if (f > 0.0f) {
    if (f > 1.0f) return {0, 255, 0};
    uint8 v = FloatByte(sqrt(f));
    return {0, v, 0};
  } else {
    if (f < -1.0f) return {255, 0, 0};
    uint8 v = FloatByte(sqrt(-f));
    return {v, 0, 0};
  }
}

static constexpr float ByteFloat(uint8 b) {
  return b * (1.0 / 255.0f);
}

// Fill UV floats straight from the indices
static void FillFromIndices(const NTSC2D &ntsc2d,
                            const ImageA &img,
                            vector<float> *f) {
  CHECK(img.Width() == NES_WIDTH);
  CHECK(img.Height() == NES_HEIGHT);
  f->resize(NES_WIDTH * NES_HEIGHT * 2, 0.0f);
  int idx = 0;
  for (int y = 0; y < NES_HEIGHT; y++) {
    for (int x = 0; x < NES_WIDTH; x++) {
      const int nes_idx = img.GetPixel(x, y);
      CHECK(nes_idx >= 0 && nes_idx < 64);
      const auto [u, v] = ntsc2d.IndexToUV(nes_idx);
      (*f)[idx++] = u;
      (*f)[idx++] = v;
    }
  }
}

static ImageRGBA Render(const NTSC2D &ntsc2d,
                        const vector<float> &values) {
  constexpr int width = NES_WIDTH;
  constexpr int height = NES_HEIGHT;
  CHECK(values.size() == width * height * 2);
  ImageRGBA out(width, height);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const int cidx = (y * width + x) * 2;
      const float u = values[cidx + 0];
      const float v = values[cidx + 1];
      const auto [r, g, b] = ntsc2d.UVColorSmooth(u, v);
      out.SetPixel(x, y, r, g, b, 0xFF);
    }
  }
  return out;
}

struct UI {
  std::unique_ptr<Emulator> emu;
  NTSC2D ntsc2d;
  uint8 player1 = 0;
  const Network &net;

  UI(const Network &net, const std::string &romfile) : net(net) {
    emu.reset(Emulator::Create(romfile));
    CHECK(emu.get()) << romfile;
  }

  // Supposedly SDL prefers this to be called from the main thread.
  void Loop() {
    [[maybe_unused]]
    int mousex = 0, mousey = 0;

    double fwd_ms = 0.0f;
    int frames = 0;

    uint32 last_draw = SDL_GetTicks();
    for (;;) {

      // Update controls, maybe end.
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
          Printf("QUIT.\n");
          return;

        } else if (event.type == SDL_MOUSEMOTION) {
          SDL_MouseMotionEvent *e = (SDL_MouseMotionEvent*)&event;

          mousex = e->x;
          mousey = e->y;
          // (and immediately redraw)

        } else if (event.type == SDL_KEYDOWN) {
          switch (event.key.keysym.sym) {
          case SDLK_ESCAPE:
            Printf("ESCAPE.\n");
            return;

          case SDLK_DOWN: player1 |= INPUT_D; break;
          case SDLK_UP: player1 |= INPUT_U; break;
          case SDLK_LEFT: player1 |= INPUT_L; break;
          case SDLK_RIGHT: player1 |= INPUT_R; break;
          case SDLK_z: player1 |= INPUT_B; break;
          case SDLK_x: player1 |= INPUT_A; break;
          case SDLK_a: player1 |= INPUT_S; break;
          case SDLK_s: player1 |= INPUT_T; break;
          default:
            break;
          }
        } else if (event.type == SDL_KEYUP) {
          switch (event.key.keysym.sym) {
          case SDLK_DOWN: player1 &= ~INPUT_D; break;
          case SDLK_UP: player1 &= ~INPUT_U; break;
          case SDLK_LEFT: player1 &= ~INPUT_L; break;
          case SDLK_RIGHT: player1 &= ~INPUT_R; break;
          case SDLK_z: player1 &= ~INPUT_B; break;
          case SDLK_x: player1 &= ~INPUT_A; break;
          case SDLK_a: player1 &= ~INPUT_S; break;
          case SDLK_s: player1 &= ~INPUT_T; break;
          default:
            break;
          }
        }
      }

      sdlutil::clearsurface(screen, 0x0);

      emu->StepFull(player1, 0);

      ImageA imga(emu->IndexedImage(), 256, 240);

      Stimulation stim(net);
      // vector<float> frame;
      FillFromIndices(ntsc2d, imga, &stim.values[0]);
      Timer fwd;
      net.RunForward(&stim);
      fwd_ms += fwd.MS();
      frames++;

      const vector<float> &frame = stim.values.back();
      ImageRGBA imgo = Render(ntsc2d, frame).ScaleBy(2);
      BlitImage(imgo, 0, 0);

      // But don't draw too fast!
      const uint32 ticks_now = SDL_GetTicks();
      const uint32 ticks_elapsed = ticks_now - last_draw;
      constexpr float target_ms = 16.0f + 2.0f / 3.0f;
      const float delay_ms = target_ms - (float)ticks_elapsed;
      if (delay_ms > 1.0f) SDL_Delay(floorf(delay_ms));
      SDL_Flip(screen);
      last_draw = SDL_GetTicks();

      if (frames % 500 == 0) {
        printf("%d frames, %.1fms in fwd each\n",
               frames,
               fwd_ms / (double)frames);
      }
    }
  }

};

int SDL_main(int argc, char **argv) {
  string romfile = "mario.nes";
  if (argc > 1)
    romfile = argv[1];

  /* Initialize SDL and network, if we're using it. */
  CHECK(SDL_Init(SDL_INIT_VIDEO |
                 SDL_INIT_TIMER |
                 SDL_INIT_AUDIO) >= 0);
  fprintf(stderr, "SDL initialized OK.\n");

  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
                      SDL_DEFAULT_REPEAT_INTERVAL);

  SDL_EnableUNICODE(1);

  SDL_Surface *icon = SDL_LoadBMP("play-icon.bmp");
  if (icon != nullptr) {
    SDL_WM_SetIcon(icon, nullptr);
  }

  screen = sdlutil::makescreen(SCREENW, SCREENH);
  CHECK(screen);

  font = Font::Create(screen,
                      "font.png",
                      FONTCHARS,
                      FONTWIDTH, FONTHEIGHT, FONTSTYLES, 1, 3);
  CHECK(font != nullptr) << "Couldn't load font.";

  // global_cl = new CL;

  std::unique_ptr<Network> net;
  net.reset(Network::ReadNetworkBinary("net0.val"));

  UI ui(*net, romfile);
  ui.Loop();

  Printf("Done.\n");

  SDL_Quit();
  return 0;
}

