
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include <cstdint>
#include <unistd.h>

#include "SDL_error.h"
#include "SDL_events.h"
#include "SDL_joystick.h"
#include "SDL_keyboard.h"
#include "SDL_keysym.h"
#include "SDL_timer.h"
#include "SDL_video.h"

#include "threadutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"

#include "timer.h"
#include "ansi.h"
#include "periodically.h"

#include "SDL.h"
#include "SDL_main.h"
#include "SDL_mouse.h"

#include "util.h"
#include "image.h"

#include "sdl/sdlutil.h"
#include "sdl/font.h"
#include "sdl/cursor.h"
#include "sdl/chars.h"

using namespace std;

using int64 = int64_t;
using uint32 = uint32_t;

// XXX probably use linked-in font instead.
static constexpr const char *FONT_PNG = "../cc-lib/sdl/font.png";
static Font *font = nullptr;

static SDL_Cursor *cursor_arrow = nullptr, *cursor_bucket = nullptr;
static SDL_Cursor *cursor_hand = nullptr, *cursor_hand_closed = nullptr;
static SDL_Cursor *cursor_eraser = nullptr;

static SDL_Surface *screen = nullptr;

struct UI {
  UI(const ImageRGBA &original);
  void Loop();
  void Draw();

  Periodically fps_per;

  enum class EventResult { NONE, DIRTY, EXIT, };
  EventResult HandleEvents();

  int screenw = 0, screenh = 0;

  const ImageRGBA &original;
  std::unique_ptr<ImageRGBA> drawing;
  int mousex = 0, mousey = 0;
  bool dragging = false;

  std::pair<int, int> drag_source = {-1, -1};
  int drag_handlex = 0, drag_handley = 0;
};

UI::UI(const ImageRGBA &original) :
  fps_per(1.0 / 59.94),
  screenw(original.Width()), screenh(original.Height()),
  original(original) {
  drawing.reset(new ImageRGBA(screenw, screenh));
  CHECK(drawing != nullptr);
  drawing->Clear32(0x000000FF);

}

UI::EventResult UI::HandleEvents() {
  bool ui_dirty = false;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT:
      printf("QUIT.\n");
      return EventResult::EXIT;

    case SDL_MOUSEMOTION: {
      SDL_MouseMotionEvent *e = (SDL_MouseMotionEvent*)&event;

      [[maybe_unused]] const int oldx = mousex, oldy = mousey;

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
        return EventResult::EXIT;

      case SDLK_RETURN:
      case SDLK_KP_ENTER:
        // Save?
        break;

      case SDLK_s: {
        // Save?
        break;
      }

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

  return ui_dirty ? EventResult::DIRTY : EventResult::NONE;
}

void UI::Loop() {
  Draw();
  for (;;) {
    bool ui_dirty = false;

    switch (HandleEvents()) {
    case EventResult::EXIT: return;
    case EventResult::NONE: break;
    case EventResult::DIRTY: ui_dirty = true; break;
    }

    if (ui_dirty) {
      Draw();
      // printf("Flip.\n");
      SDL_Flip(screen);
      ui_dirty = false;
    }
    SDL_Delay(1);
  }
}

void UI::Draw() {
  CHECK(font != nullptr);
  CHECK(drawing != nullptr);
  CHECK(screen != nullptr);

  drawing->CopyImage(0, 0, original);

  sdlutil::CopyRGBAToScreen(*drawing, screen);
}

static void InitializeSDL(int screen_width,
                          int screen_height) {
  // Initialize SDL.
  CHECK(SDL_Init(SDL_INIT_VIDEO |
                 SDL_INIT_TIMER |
                 SDL_INIT_JOYSTICK |
                 SDL_INIT_AUDIO) >= 0);
  fprintf(stderr, "SDL initialized OK.\n");

  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
                      SDL_DEFAULT_REPEAT_INTERVAL);

  SDL_EnableUNICODE(1);

  // TODO: linked-in icon
  SDL_Surface *icon = SDL_LoadBMP("icon.bmp");
  if (icon != nullptr) {
    SDL_WM_SetIcon(icon, nullptr);
  }

  screen = sdlutil::makescreen(screen_width, screen_height);
  CHECK(screen != nullptr);

  font = Font::Create(screen,
                      FONT_PNG,
                      FONTCHARS,
                      FONTWIDTH, FONTHEIGHT, FONTSTYLES, 1, 3);
  CHECK(font != nullptr) << "Couldn't load font.";

  CHECK((cursor_arrow = Cursor::MakeArrow()));
  CHECK((cursor_bucket = Cursor::MakeBucket()));
  CHECK((cursor_hand = Cursor::MakeHand()));
  CHECK((cursor_hand_closed = Cursor::MakeHandClosed()));
  CHECK((cursor_eraser = Cursor::MakeEraser()));

  SDL_SetCursor(cursor_arrow);
  SDL_ShowCursor(SDL_ENABLE);
}

int main(int argc, char **argv) {
  ANSI::Init();

  // TODO: Allow specifying crop rectangle, etc.
  // TODO: On windows, it's pretty easy to call
  // GetOpenFilename so that you don't have to
  // type this on the filename.
  CHECK(argc == 2) << "./crop.exe image.png\n";

  std::string image_filename = argv[1];
  std::unique_ptr<ImageRGBA> original(
      ImageRGBA::Load(image_filename));
  CHECK(original.get() != nullptr) << image_filename;

  InitializeSDL(original->Width(), original->Height());

  UI ui(*original);
  ui.Loop();

  SDL_Quit();
  return 0;
}

