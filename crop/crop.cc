
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
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include <cstdint>
#include <unistd.h>

#include "SDL_error.h"
#include "SDL_events.h"
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
#include "image-resize.h"

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

// XXX make configurable!
static constexpr int TARGET_W = 1280, TARGET_H = 1280;

struct PaddedOriginal {
  PaddedOriginal(const std::string &image_filename,
                 int target_width, int target_height) :
    image_filename(image_filename) {

    std::unique_ptr<ImageRGBA> original;

    original.reset(ImageRGBA::Load(image_filename));
    CHECK(original.get() != nullptr) << image_filename;

    int xover = target_width - original->Width();
    int yover = target_height - original->Height();

    // We will center the image, but with enough padding on each
    // side so that cropping can happen at any offset. (Normally
    // you would use xover/2 etc.)

    if (xover > 0) { pad_left = pad_right = xover; }
    if (yover > 0) { pad_top = pad_bottom = yover; }

    if (xover <= 0 && yover <= 0) {
      // Avoid copying if we don't need to pad.
      img = std::move(original);
    } else {
      img.reset(new ImageRGBA(original->Width() + pad_left + pad_right,
                              original->Height() + pad_top + pad_bottom));
      CHECK(img.get() != nullptr);
      // This should be configurable, or derived from the image?
      img->Clear32(0x000000FF);
      img->CopyImage(pad_left, pad_top, *original);
    }
  }

  int Width() const { return img->Width(); }
  int Height() const { return img->Height(); }

  std::unique_ptr<ImageRGBA> img;
  std::string image_filename;
  int pad_top = 0, pad_right = 0, pad_bottom = 0, pad_left = 0;
};

struct UI {
  UI(const PaddedOriginal &original,
     const std::string &output_filename);
  void Loop();
  void Draw();
  void Redraw();

  Periodically fps_per;

  enum class EventResult { NONE, DIRTY, EXIT, };
  EventResult HandleEvents();

  int screenw = 0, screenh = 0;

  const PaddedOriginal &original;
  const std::string output_filename;
  std::unique_ptr<ImageRGBA> drawing;
  int mousex = 0, mousey = 0;
  bool dragging = false;

  int cropx = 0, cropy = 0;
  int cropw = TARGET_W, croph = TARGET_H;
  void NormalizeCrop();

  std::pair<int, int> drag_source = {-1, -1};
  int drag_handlex = 0, drag_handley = 0;
};

void UI::NormalizeCrop() {
  cropx = std::max(mousex, 0);
  cropy = std::max(mousey, 0);
  int xover = (cropx + cropw) - original.Width();
  int yover = (cropy + croph) - original.Height();
  if (xover > 0) cropx -= xover;
  if (yover > 0) cropy -= yover;

  CHECK(cropx >= 0 && cropy >= 0)
      << "Can't handle the "
         "case where the original is smaller than the crop "
         "rectangle yet!";
}

UI::UI(const PaddedOriginal &original, const std::string &output_filename) :
  fps_per(1.0 / 59.94),
  screenw(original.Width()), screenh(original.Height()),
  original(original),
  output_filename(output_filename) {
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

      cropx = mousex;
      cropy = mousey;
      NormalizeCrop();
      ui_dirty = true;

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
        // TODO: Prompt filename?
        ImageRGBA crop(cropw, croph);
        crop.Clear32(0x00000000);
        crop.CopyImageRect(0, 0, *original.img, cropx, cropy, cropw, croph);

        if (cropw == TARGET_W && croph == TARGET_H) {
          crop.SaveJPG(output_filename, 90);
        } else {
          ImageRGBA resized = ImageResize::Resize(crop, TARGET_W, TARGET_H);
          resized.SaveJPG(output_filename, 90);
        }
        printf("Wrote " AGREEN("%s") "\n", output_filename.c_str());
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

      printf("Mouse button: %d\n", e->button);
      if (e->button == 4) {
        // mousewheel up
        if (cropw >= 16 &&
            croph >= 16) {
          cropw -= 8;
          croph -= 8;

          NormalizeCrop();
          ui_dirty = true;
        }

      } else if (e->button == 5) {
        // mousewheel down
        if (cropw + 8 <= original.Width() &&
            croph + 8 <= original.Height()) {
          cropw += 8;
          croph += 8;

          NormalizeCrop();
          ui_dirty = true;
        }
      }

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
  Redraw();

  for (;;) {
    bool ui_dirty = false;

    switch (HandleEvents()) {
    case EventResult::EXIT: return;
    case EventResult::NONE: break;
    case EventResult::DIRTY: ui_dirty = true; break;
    }

    if (ui_dirty) {
      Redraw();
      ui_dirty = false;
    }
    SDL_Delay(1);
  }
}

void UI::Redraw() {
  Draw();
  SDL_Flip(screen);
}

void UI::Draw() {
  CHECK(font != nullptr);
  CHECK(drawing != nullptr);
  CHECK(screen != nullptr);

  drawing->CopyImage(0, 0, *original.img);

  drawing->BlendBox32(cropx, cropy, cropw, croph,
                      0xFF0000AA, {0xFF000077});

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

  std::string_view output_file_base = Util::FileBaseOf(image_filename);
  // TODO: Derive from input type? Save with 'j' vs 'p'? Something else?
  std::string output_filename =
    StringPrintf("%s-crop.jpg", string(output_file_base).c_str());

  PaddedOriginal original(image_filename, TARGET_W, TARGET_H);

  InitializeSDL(original.Width(), original.Height());

  UI ui(original, output_filename);
  ui.Loop();

  SDL_Quit();
  return 0;
}

