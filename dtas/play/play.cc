
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
#include "randutil.h"
#include "arcfour.h"
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

#include "../../fceulib/emulator.h"
#include "../../fceulib/simplefm2.h"
#include "emulator-pool.h"
#include "mario.h"
#include "mario-util.h"
#include "minus.h"

using namespace std;

using int64 = int64_t;
using uint32 = uint32_t;

static constexpr int EMULATOR_THREADS = 8;
static constexpr const char *ROMFILE = "../mario.nes";
static EmulatorPool *emulator_pool = nullptr;
static std::vector<uint8_t> level_start;

static constexpr const char *FONT_PNG = "../../cc-lib/sdl/font.png";
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
static constexpr int NUM_EMUS = EMUS_TALL * EMUS_WIDE;

static SDL_Joystick *joystick = nullptr;
static SDL_Surface *screen = nullptr;

enum class Mode {
  PLAY,
  PAUSE,
};

// This wraps a movie (series of inputs), which is rooted at the
// WarpTo state. The movie also has a cursor somewhere in its
// bounds (because it may have been rewound) which is treated
// as the default length of the movie unless specified.
//
// In the future, we can store these more efficiently
// as a DAG, since they are generally related and we want to
// be able to seek efficiently (i.e. by storing save states periodically).
// The general notion of "series of inputs rooted at some save state"
// is probably right.
struct Movie {
  // Empty.
  Movie() {}

  // Rewind up to n steps. Never rewinds past the beginning, of
  // course. The future is preserved: Rewind(n); Forward(n); is
  // a no-op when n is in bounds. Returns the number of inputs
  // rewound.
  int Rewind(int n) {
    int dist = std::min(n, cursor);
    cursor -= dist;
    return dist;
  }

  int Forward(int n) {
    int dist = std::min(n, (int)inputs.size() - cursor);
    cursor += dist;
    return dist;
  }

  // Push at the cursor. Destroys future inputs unless they
  // match exactly.
  void Push(uint8_t b) {
    if ((int)inputs.size() > cursor) {
      if (inputs[cursor] == b) {
        // This is already the next input. In this case
        // we keep the future inputs.
        cursor++;
      } else {
        inputs.resize(cursor);
        inputs.push_back(b);
      }
    } else {
      inputs.push_back(b);
    }
  }

  // Returns the size of the movie up to the current cursor.
  int Size() const {
    return cursor;
  }

  // Returns the size of the movie, including future inputs.
  int FullSize() const {
    return (int)inputs.size();
  }

  auto begin() const { return inputs.begin(); }
  auto end() const { return inputs.begin() + cursor; }

 private:
  std::vector<uint8_t> inputs;
  // This is the size of the used region, or the index of
  // the next position that we would write.
  int cursor = 0;
};

// A single on-screen game. This has an emulator
// lease in the current state and a movie that
// gets us to that state.
//
// TODO: Maybe we don't want to deal with the locking
// overhead? The lock could be held by GameArray, and
// only used during the parallel Step call there.
struct Game {
  std::mutex m;
  Movie movie;
  EmulatorPool::Lease emu;
  ImageRGBA img;
  // TODO: Should have mario-specific metadata here that
  // allows us to display, etc.

  // Execute a step and get the emulator image.
  void Step(uint8_t b) {
    MutexLock ml(&m);
    movie.Push(b);
    emu->StepFull(b, 0);
    img = MarioUtil::Screenshot(emu.get());
  }

  Game(Game &other) : emu(emulator_pool->Acquire()) {
    MutexLock ml(&other.m);
    // This requires the movie to match the state, but that
    // is a representation invariant of Game.
    movie = other.movie;
    emu->LoadUncompressed(other.emu->SaveUncompressed());
    img = other.img;
  }

  Game() : emu(emulator_pool->Acquire()) {
    CHECK(!level_start.empty());
    emu->LoadUncompressed(level_start);
    img = MarioUtil::Screenshot(emu.get());
    printf("Created game (default ctor).\n");
  }

  Game(Movie movie_in) : movie(std::move(movie_in)),
                         emu(emulator_pool->Acquire()) {
    CHECK(!level_start.empty());
    emu->LoadUncompressed(level_start);

    for (uint8_t b : movie) {
      emu->StepFull(b, 0);
    }
    img = MarioUtil::Screenshot(emu.get());
  }

  // PERF: We could use RGB for these?
  ImageRGBA Screenshot() {
    MutexLock ml(&m);
    CHECK(img.Width() > 0 && img.Height() > 0);
    return img;
  }
};


struct GameArray {
  GameArray() : games(NUM_EMUS),
                rc(StringPrintf("ga%lld", time(nullptr))) {
    CHECK((int)games.size() == NUM_EMUS);
    for (int i = 0; i < NUM_EMUS; i++) {
      games[i] = std::make_unique<Game>();
    }

    /*
    for (int i = 0; i < EMULATOR_THREADS; i++) {
      workers.emplace_back();
    }
    */
  }

  // Emulation is relatively expensive, so we do it in multiple
  // separate threads. Each command in here is just a std::function to
  // be executed. It's expected (for performance) that these can run
  // in parallel and won't block on each other. Generally we just
  // have one std::function per Game, updating its state.
  void WorkerThread() {
    printf("GameArray thread start.\n");
    // TODO: Check exit condition.
    for (;;) {
      std::unique_lock<std::mutex> ml(m);
      // Wait for a task.
      cv.wait(ml, [this]() { return !work.empty(); });

      auto task = std::move(work.front());
      work.pop_front();

      ml.unlock();

      task();
    }
  }

  // Perform a step using the user input.
  // This runs the step, or modifications of it,
  // in each Game instance. When this returns, the
  // game states are all updated and have screenshots.
  void Step(uint8_t orig) {
    for (int i = 0; i < NUM_EMUS; i++) {
      {
        std::unique_lock<std::mutex> ml(m);
        work.emplace_back([this, i, orig]() {
            uint8_t b = orig;
            // XXX: These need to be stateful. The idea
            // should be more like to modify the frame
            // on which an input happens than to randomly
            // add noise to the inputs.
            if (rc.Byte() < 24) b ^= INPUT_B;
            if (rc.Byte() < 24) b &= ~INPUT_A;
            if (rc.Byte() < 24) b ^= INPUT_R;
            if (rc.Byte() < 24) b ^= INPUT_L;

            games[i]->Step(b);
          });
      }
      cv.notify_one();
    }

    // Wait until done.
    {
      std::unique_lock<std::mutex> ml(m);
      cv.wait(ml, [this]() { return work.empty(); });
    }
  }

  // Lock per game. The vector itself should not
  // be modified after construction.
  std::vector<std::unique_ptr<Game>> games;
  ArcFour rc;

  std::vector<std::thread> workers;

  std::mutex m;
  std::condition_variable cv;
  std::deque<std::function<void()>> work;
};

static GameArray *game_array = nullptr;

struct UI {
  Mode mode = Mode::PLAY;
  uint8_t current_gamepad = 0;
  int64_t frames_drawn = 0;

  UI();
  void Loop();
  void Draw();

  Periodically fps_per;
  // Returns true if dirty.
  bool MaybeRunEmulators(uint8_t buttons);

  enum class EventResult { NONE, DIRTY, EXIT, };
  EventResult HandleEvents();

  SDL_Surface *drawing = nullptr;
  int mousex = 0, mousey = 0;
  bool dragging = false;

  std::pair<int, int> drag_source = {-1, -1};
  int drag_handlex = 0, drag_handley = 0;
};

UI::UI() : fps_per(1.0 / 59.94) {
  drawing = sdlutil::makesurface(SCREENW, SCREENH, true);
  CHECK(drawing != nullptr);
  sdlutil::ClearSurface(drawing, 0, 0, 0, 0);
}

bool UI::MaybeRunEmulators(uint8_t buttons) {
  printf("Run emulators? XXX NO\n");
  return false;

  if (game_array == nullptr) {
    printf("No game array!\n");
    return false;
  }

  if (fps_per.ShouldRun()) {
    game_array->Step(buttons);
    return true;
  }

  return false;
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
        return EventResult::EXIT;

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

    case SDL_JOYBUTTONDOWN:
      printf("Button %d pressed.\n", event.jbutton.button);
      break;
    case SDL_JOYBUTTONUP:
      printf("Button %d released.\n", event.jbutton.button);
      break;
    case SDL_JOYHATMOTION:
      printf("Hat %d moved to %d.\n", event.jhat.hat, event.jhat.value);
      break;

    default:;
    }
  }

  return ui_dirty ? EventResult::DIRTY : EventResult::NONE;
}

void UI::Loop() {
  for (;;) {
    bool ui_dirty = false;

    printf("Handle events.\n");

    switch (HandleEvents()) {
    case EventResult::EXIT: return;
    case EventResult::NONE: break;
    case EventResult::DIRTY: ui_dirty = true; break;
    }

    if (MaybeRunEmulators(current_gamepad))
      ui_dirty = true;

    if (ui_dirty) {
      sdlutil::clearsurface(screen, 0xFFFFFFFF);
      Draw();
      printf("Flip.\n");
      SDL_Flip(screen);
      ui_dirty = false;
    }
    SDL_Delay(1);
  }
}

void UI::Draw() {
  printf("Draw.\n");
  if (false) {
  CHECK(font != nullptr);
  CHECK(drawing != nullptr);
  CHECK(screen != nullptr);

  CHECK(game_array != nullptr);
  CHECK(game_array->games.size() == NUM_EMUS);

  for (int ey = 0; ey < EMUS_TALL; ey++) {
    for (int ex = 0; ex < EMUS_WIDE; ex++) {
      int eidx = ey * EMUS_WIDE + ex;
      CHECK(eidx >=0 && eidx < NUM_EMUS);

      static constexpr int MARGIN = 4;
      const int dx = ex * (256 + MARGIN);
      const int dy = ey * (240 + MARGIN);

      // Copy game image to screen.
      // PERF: Too much copying here!!
      Game *game = game_array->games[eidx].get();
      CHECK(game != nullptr);
      if (game != nullptr) {
        ImageRGBA shot = game->Screenshot();
        sdlutil::CopyRGBARect(shot, 0, 0, 256, 240, dx, dy, drawing);
      } else {
        sdlutil::FillRectRGB(drawing, dx, dy, 256, 240, 0x33, 0x00, 0x00);
      }
    }
  }
  }

  font->drawto(drawing, 5, 5, StringPrintf("Frames: ^2%lld", frames_drawn));
  sdlutil::blitall(drawing, screen, 0, 0);
  frames_drawn++;
  printf("Drew %lld.\n", frames_drawn);
}

static void InitializeSDL() {
  // Initialize SDL.
  CHECK(SDL_Init(SDL_INIT_VIDEO |
                 SDL_INIT_TIMER |
                 SDL_INIT_JOYSTICK |
                 SDL_INIT_AUDIO) >= 0);
  fprintf(stderr, "SDL initialized OK.\n");

  if (SDL_NumJoysticks() == 0) {
    printf("No joysticks were found.\n");
  } else {
    joystick = SDL_JoystickOpen(0);
    if (joystick == nullptr) {
      printf("Could not open joystick: %s\n", SDL_GetError());
    }
  }

  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY,
                      SDL_DEFAULT_REPEAT_INTERVAL);

  SDL_EnableUNICODE(1);

  SDL_Surface *icon = SDL_LoadBMP("icon.bmp");
  if (icon != nullptr) {
    SDL_WM_SetIcon(icon, nullptr);
  }

  screen = sdlutil::makescreen(SCREENW, SCREENH);
  CHECK(screen != nullptr);
  printf("Created screen.\n");

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
  printf("Created fonts.\n");

  CHECK((cursor_arrow = Cursor::MakeArrow()));
  CHECK((cursor_bucket = Cursor::MakeBucket()));
  CHECK((cursor_hand = Cursor::MakeHand()));
  CHECK((cursor_hand_closed = Cursor::MakeHandClosed()));
  CHECK((cursor_eraser = Cursor::MakeEraser()));
  printf("Created cursors.\n");

  SDL_SetCursor(cursor_arrow);
  SDL_ShowCursor(SDL_ENABLE);
}

int main(int argc, char **argv) {
  fprintf(stderr, "In main...\n");
  ANSI::Init();

  CHECK(argc == 3) << "Usage:\n"
    "./play.exe major minor\n\n"
    "Major and minor are hex levels 00-ff.\n";

  fprintf(stderr, "Try initialize SDL...\n");
  InitializeSDL();

  const uint8_t major = strtol(argv[1], nullptr, 16);
  const uint8_t minor = strtol(argv[1], nullptr, 16);
  const LevelId level = PackLevel(major, minor);
  printf("Play %s\n", ColorLevel(level).c_str());


  emulator_pool = new EmulatorPool(ROMFILE);
  printf("Created emulator pool.\n");
  {
    CHECK(emulator_pool != nullptr);
    auto emu = emulator_pool->AcquireClean();
    MarioUtil::WarpTo(emu.get(), major, minor, 0);
    level_start = emu->SaveUncompressed();
  }
  printf("Initialized emulator pool.\n");

  game_array = new GameArray;
  printf("Created GameArray.\n");

  sleep(1);

  UI ui;
  ui.Loop();

  SDL_Quit();
  return 0;
}

