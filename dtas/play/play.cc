
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <shared_mutex>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unistd.h>
#include <cmath>

#include "SDL_events.h"
#include "SDL_keyboard.h"
#include "SDL_keysym.h"
#include "SDL_video.h"
#include "threadutil.h"
#include "randutil.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"

#include "timer.h"

#include "SDL.h"
#include "SDL_main.h"
#include "SDL_mouse.h"

#include "lines.h"
#include "util.h"
#include "image.h"

#include "sdl/sdlutil.h"
#include "sdl/font.h"
#include "sdl/cursor.h"

/* there are some non-ascii symbols in the font */
#define CHECKMARK "\xF2"
#define ESC "\xF3"
#define HEART "\xF4"
/* here L means "long" */
#define LCMARK1 "\xF5"
#define LCMARK2 "\xF6"
#define LCHECKMARK LCMARK1 LCMARK2
#define LRARROW1 "\xF7"
#define LRARROW2 "\xF8"
#define LRARROW LRARROW1 LRARROW2
#define LLARROW1 "\xF9"
#define LLARROW2 "\xFA"
#define LLARROW LLARROW1 LLARROW2

/* BAR_0 ... BAR_10 are guaranteed to be consecutive */
#define BAR_0 "\xE0"
#define BAR_1 "\xE1"
#define BAR_2 "\xE2"
#define BAR_3 "\xE3"
#define BAR_4 "\xE4"
#define BAR_5 "\xE5"
#define BAR_6 "\xE6"
#define BAR_7 "\xE7"
#define BAR_8 "\xE8"
#define BAR_9 "\xE9"
#define BAR_10 "\xEA"
#define BARSTART "\xEB"

#define FONTCHARS " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789`-=[]\\;',./~!@#$%^&*()_+{}|:\"<>?" CHECKMARK ESC HEART LCMARK1 LCMARK2 BAR_0 BAR_1 BAR_2 BAR_3 BAR_4 BAR_5 BAR_6 BAR_7 BAR_8 BAR_9 BAR_10 BARSTART LRARROW LLARROW

#define FONTSTYLES 7

using namespace std;

using int64 = int64_t;
using uint32 = uint32_t;

#define FONTWIDTH 9
#define FONTHEIGHT 16
static Font *font = nullptr, *font2x = nullptr, *font4x = nullptr;

static SDL_Cursor *cursor_arrow = nullptr, *cursor_bucket = nullptr;
static SDL_Cursor *cursor_hand = nullptr, *cursor_hand_closed = nullptr;
static SDL_Cursor *cursor_eraser = nullptr;
#define VIDEOH 1080
#define STATUSH 128
#define SCREENW 1920
#define SCREENH (VIDEOH + STATUSH)

static constexpr int EMUS_TALL = 8;
static constexpr int EMUS_WIDE = 8;

static SDL_Surface *screen = nullptr;

static constexpr uint32 COLORS[] = {
  // black, white
  /* 1 */ 0xFF000000,
  /* 2 */ 0xFFFFFFFF,
  // 'nice' RGB
  /* 3 */ 0xFFbd3838, // R
  /* 4 */ 0xFF0ba112, // G
  /* 5 */ 0xFF1664CE, // B
  // intense
  /* 6 */ 0xFFf943ea, // magenta
  /* 7 */ 0xFFeef943, // yellow
  /* 8 */ 0xFF29edef, // cyan
  // misc
  /* 9 */ 0xFFc77e00, // orange/brown
  // Note zero is actually like "10"
  /* 0 */ 0xFF7500c7, // purple
};

enum class Mode {
  PLAY,
  PAUSE,
};

struct UI {
  Mode mode = Mode::PLAY;
  bool ui_dirty = true;

  UI();
  void Loop();
  void Draw();

  SDL_Surface *drawing = nullptr;
  int mousex = 0, mousey = 0;
  bool dragging = false;
  // If both are non-negative, represents a currently grabbed piece on
  // the board. As row, col.
  std::pair<int, int> drag_source = {-1, -1};
  int drag_handlex = 0, drag_handley = 0;
};

UI::UI() {
  drawing = sdlutil::makesurface(SCREENW, SCREENH, true);
  CHECK(drawing != nullptr);
  sdlutil::ClearSurface(drawing, 0, 0, 0, 0);
}

void UI::Loop() {
  for (;;) {

    SDL_Event event;
    if (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        printf("QUIT.\n");
        return;

      case SDL_MOUSEMOTION: {
        SDL_MouseMotionEvent *e = (SDL_MouseMotionEvent*)&event;

        const int oldx = mousex, oldy = mousey;

        mousex = e->x;
        mousey = e->y;

        #if 0
        if (dragging) {
          switch (mode) {
          case Mode::PLAY:

            break;
          default:;
          }
          ui_dirty = true;
        }
        #endif
        break;
      }

      case SDL_KEYDOWN: {
        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
          printf("ESCAPE.\n");
          return;

        case SDLK_HOME: {
          // TODO: Restart level...

          mode = Mode::PAUSE;
          ui_dirty = true;
          break;
        }

        // Delete, backspace, ...?

        case SDLK_LEFT: {
          // TODO: Rewind all movies one step, and pause.

          mode = Mode::PAUSE;
          ui_dirty = true;
          break;
        }

        case SDLK_RIGHT: {
          // TODO: Forward all movies (if possible) ?

          mode = Mode::PAUSE;
          ui_dirty = true;
          break;
        }

        case SDLK_SPACE: {
          if (mode == Mode::PAUSE) {
            mode = Mode::PLAY;
          } else {
            mode = Mode::PLAY;
          }

          ui_dirty = true;
          break;
        }

        case SDLK_KP_PLUS:
        case SDLK_EQUALS:
        case SDLK_PLUS:
          // TODO: Speed up
          ui_dirty = true;
          break;


        case SDLK_KP_MINUS:
        case SDLK_MINUS:
          // TODO: Speed down
          ui_dirty = true;
          break;

        default:;
        }
        break;
      }

      case SDL_MOUSEBUTTONDOWN: {
        // LMB/RMB, drag, etc.
        SDL_MouseButtonEvent *e = (SDL_MouseButtonEvent*)&event;
        mousex = e->x;
        mousey = e->y;

        dragging = true;

        break;
      }

      case SDL_MOUSEBUTTONUP: {
        // LMB/RMB, drag, etc.
        dragging = false;


        ui_dirty = true;
        drag_source = {-1, -1};
        // SDL_SetCursor(cursor_hand);
        break;
      }

      default:;
      }
    }

    if (ui_dirty) {
      sdlutil::clearsurface(screen, 0xFFFFFFFF);
      Draw();
      SDL_Flip(screen);
      ui_dirty = false;
    }
  }

}

void UI::Draw() {

  for (int ey = 0; ey < EMUS_TALL; ey++) {
    for (int ex = 0; ex < EMUS_WIDE; ex++) {
      int eidx = ey * EMUS_WIDE + ex;



    }
  }


  sdlutil::blitall(drawing, screen, 0, 0);
}

int main(int argc, char **argv) {
  /* Initialize SDL and network, if we're using it. */
  CHECK(SDL_Init(SDL_INIT_VIDEO |
                 SDL_INIT_TIMER |
                 SDL_INIT_AUDIO) >= 0);
  fprintf(stderr, "SDL initialized OK.\n");

  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
                      SDL_DEFAULT_REPEAT_INTERVAL);

  SDL_EnableUNICODE(1);

  SDL_Surface *icon = SDL_LoadBMP("icon.bmp");
  if (icon != nullptr) {
    SDL_WM_SetIcon(icon, nullptr);
  }

  screen = sdlutil::makescreen(SCREENW, SCREENH);
  CHECK(screen);

  static constexpr const char *FONT_PNG = "../../cc-lib/sdl/font.png";

  font = Font::Create(screen,
                      FONT_PNG,
                      FONTCHARS,
                      FONTWIDTH, FONTHEIGHT, FONTSTYLES, 1, 3);
  CHECK(font != nullptr) << "Couldn't load font.";



  font2x = Font::CreateX(2,
                         screen,
                         FONT_PNG,
                         FONTCHARS,
                         FONTWIDTH, FONTHEIGHT, FONTSTYLES, 1, 3);
  CHECK(font2x != nullptr) << "Couldn't load font.";

  font4x = Font::CreateX(4,
                         screen,
                         FONT_PNG,
                         FONTCHARS,
                         FONTWIDTH, FONTHEIGHT, FONTSTYLES, 1, 3);
  CHECK(font4x != nullptr) << "Couldn't load font.";

  CHECK((cursor_arrow = Cursor::MakeArrow()));
  CHECK((cursor_bucket = Cursor::MakeBucket()));
  CHECK((cursor_hand = Cursor::MakeHand()));
  CHECK((cursor_hand_closed = Cursor::MakeHandClosed()));
  CHECK((cursor_eraser = Cursor::MakeEraser()));

  SDL_SetCursor(cursor_arrow);
  SDL_ShowCursor(SDL_ENABLE);

  UI ui;
  ui.Loop();

  SDL_Quit();
  return 0;
}

