
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
#include "../../fceulib/simplefm7.h"
#include "emulator-pool.h"
#include "mario.h"
#include "mario-util.h"
#include "minus.h"
#include "evaluator.h"

using namespace std;

using int64 = int64_t;
using uint32 = uint32_t;

static constexpr bool TRACE = false;

static constexpr int EMULATOR_THREADS = 8;
static constexpr const char *ROMFILE = "../mario.nes";
static EmulatorPool *emulator_pool = nullptr;
static Evaluator *evaluator = nullptr;
static std::vector<uint8_t> level_start;

static constexpr const char *FONT_PNG = "../../cc-lib/sdl/font.png";
static Font *font = nullptr, *font2x = nullptr, *font4x = nullptr;

static SDL_Cursor *cursor_arrow = nullptr, *cursor_bucket = nullptr;
static SDL_Cursor *cursor_hand = nullptr, *cursor_hand_closed = nullptr;
static SDL_Cursor *cursor_eraser = nullptr;

static constexpr int EMUS_TALL = 6;
static constexpr int EMUS_WIDE = 8;
static constexpr int NUM_EMUS = EMUS_TALL * EMUS_WIDE;

#define SCREENW (EMUS_WIDE * (256 + 8))
#define SCREENH ((240 + 8) * EMUS_TALL)

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
  explicit Movie(std::vector<uint8_t> m) : inputs(std::move(m)) {
    cursor = (int)inputs.size();
  }

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
      } else {
        inputs.resize(cursor);
        inputs.push_back(b);
      }
    } else {
      inputs.push_back(b);
    }
    // No matter what, we move the cursor forward.
    CHECK(cursor < inputs.size());
    cursor++;
  }

  // Returns the size of the movie up to the current cursor.
  int Size() const {
    return cursor;
  }

  // Returns the size of the movie, including future inputs.
  int FullSize() const {
    return (int)inputs.size();
  }

  // Assuming the emulator is currently at the cursor, seek (up to)
  // delta frames forward or backward (for negative deltas).
  void Seek(int delta, Emulator *emu) {
    // PERF: Save checkpoints so that this is not linear time!
    emu->LoadUncompressed(level_start);
    int new_cursor = std::clamp(cursor + delta, 0, (int)inputs.size());
    cursor = 0;
    while (cursor < new_cursor) {
      emu->StepFull(inputs[cursor], 0);
      cursor++;
    }
  }

  auto begin() const { return inputs.begin(); }
  auto end() const { return inputs.begin() + cursor; }

  std::string FM7() const {
    CHECK(cursor >= 0 && cursor <= inputs.size()) << cursor << " vs "
                                                  << inputs.size();
    std::vector<uint8_t> trimmed = inputs;
    trimmed.resize(cursor);
    printf("There are %d inputs.\n", (int)cursor);
    return SimpleFM7::EncodeOneLine(trimmed);
  }

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

  void Seek(int delta) {
    MutexLock ml(&m);
    movie.Seek(delta, emu.get());
    img = MarioUtil::Screenshot(emu.get());
  }

  // Execute a step and get the emulator image.
  void Step(uint8_t b) {
    MutexLock ml(&m);
    movie.Push(b);
    emu->StepFull(b, 0);
    img = MarioUtil::Screenshot(emu.get());
  }

  std::string FM7() const {
    return movie.FM7();
  }

  Game &operator=(Game &other) {
    if (this == &other) {
      return *this;
    }

    MutexLock ml(&other.m);
    movie = other.movie;
    emu->LoadUncompressed(other.emu->SaveUncompressed());
    img = other.img;
    return *this;
  }

  Game(Game &other) : emu(emulator_pool->Acquire()) {
    MutexLock ml(&other.m);
    // This requires the movie to match the state, but that
    // is a representation invariant of Game.
    movie = other.movie;
    emu->LoadUncompressed(other.emu->SaveUncompressed());
    img = other.img;
  }

  #if 0
  Game() : emu(emulator_pool->Acquire()) {
    CHECK(!level_start.empty());
    emu->LoadUncompressed(level_start);
    img = MarioUtil::Screenshot(emu.get());
    if (TRACE) { printf("Created game (default ctor).\n"); }
  }
  #endif

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
  GameArray(Movie movie) : games(NUM_EMUS),
                           rc(StringPrintf("ga%lld", time(nullptr))) {
    CHECK((int)games.size() == NUM_EMUS);
    for (int i = 0; i < NUM_EMUS; i++) {
      games[i] = std::make_unique<Game>(movie);
    }

    for (int i = 0; i < EMULATOR_THREADS; i++) {
      workers.emplace_back(&GameArray::WorkerThread, this);
    }
  }

  // Emulation is relatively expensive, so we do it in multiple
  // separate threads. Each command in here is just a std::function to
  // be executed. It's expected (for performance) that these can run
  // in parallel and won't block on each other. Generally we just
  // have one std::function per Game, updating its state.
  void WorkerThread() {
    if (TRACE) { printf("GameArray thread start.\n"); fflush(stdout); }
    // TODO: Check exit condition.
    for (;;) {
      std::unique_lock<std::mutex> ml(m);
      // Wait for a task.
      cv.wait(ml, [this]() { return !work.empty(); });

      auto task = std::move(work.front());
      work.pop_front();
      running_tasks++;

      ml.unlock();

      task();

      ml.lock();
      running_tasks--;
      ml.unlock();

      // Parent needs to wake up if this is the last one.
      cv_done.notify_all();
    }
  }

  // Perform a step using the user input.
  // This runs the step, or modifications of it,
  // in each Game instance. When this returns, the
  // game states are all updated and have screenshots.
  void Step(uint8_t orig) {
    if (TRACE) { printf("Step %02x\n", orig); fflush(stdout); }
    for (int i = 0; i < NUM_EMUS; i++) {
      {
        std::unique_lock<std::mutex> ml(m);
        work.emplace_back([this, i, orig]() {
            uint8_t b = orig;
            if (i != focus_idx) {
              // XXX: These need to be stateful. The idea
              // should be more like to modify the frame
              // on which an input happens than to randomly
              // add noise to the inputs.
              if (rc.Byte() < 24) b ^= INPUT_B;
              if (rc.Byte() < 24) b &= ~INPUT_A;
              if (rc.Byte() < 24) b ^= INPUT_R;
              if (rc.Byte() < 24) b ^= INPUT_L;
            }

            games[i]->Step(b);
          });
      }
      cv.notify_all();
    }

    if (TRACE) { printf("Wait on work.\n"); fflush(stdout); }
    WaitThreads();
    if (TRACE) { printf("Step %02x done.\n", orig); fflush(stdout); }
  }

  void WaitThreads() {
    // Wait until done.
    for (;;) {
      std::unique_lock<std::mutex> ml(m);
      CHECK(work.empty() || !workers.empty()) << "There are no "
        "worker threads, so this will never make progress!";
      cv_done.wait(ml, [this]() {
          return work.empty();
        });

      // printf("work queue empty, tasks=%lld\n", running_tasks);
      // fflush(stdout);

      if (running_tasks == 0) break;
    }
  }

  void Seek(int delta) {
    CHECK(games.size() == NUM_EMUS);

    RandomGaussian g(&rc);
    for (int i = 0; i < NUM_EMUS; i++) {
      {
        int noise = g.Next() * 4;
        int d = delta;
        if (i != focus_idx) {
          // Add a little noise to the seek
          d += noise;
        }

        std::unique_lock<std::mutex> ml(m);
        work.emplace_back([this, i, d]() {
            games[i]->Seek(d);
          });
      }
      cv.notify_all();
    }

    WaitThreads();
  }

  std::string FocusedFM7() const {
    return games[focus_idx]->FM7();
  }

  void MoveFocus(int dx, int dy) {
    int fx = FocusX(), fy = FocusY();
    fx = std::clamp(fx + dx, 0, EMUS_WIDE - 1);
    fy = std::clamp(fy + dy, 0, EMUS_TALL - 1);
    focus_idx = fy * EMUS_WIDE + fx;
  }

  void CloneFocus() {
    for (int idx = 0; idx < NUM_EMUS; idx++) {
      if (idx != focus_idx) {
        *games[idx] = *games[focus_idx];
      }
    }
  }

  int FocusX() const { return focus_idx % EMUS_WIDE; }
  int FocusY() const { return focus_idx / EMUS_WIDE; }

  // Indicates one of the games that is the main focus. This one
  // always gets the unperturbed inputs and is never reclaimed.
  int focus_idx = 0;

  // Lock per game. The vector itself should not
  // be modified after construction.
  std::vector<std::unique_ptr<Game>> games;
  ArcFour rc;

  std::vector<std::thread> workers;

  std::mutex m;
  std::condition_variable cv;
  std::condition_variable cv_done;
  std::deque<std::function<void()>> work;
  int64_t running_tasks = 0;
};

static GameArray *game_array = nullptr;

struct UI {
  const LevelId level;
  Mode mode = Mode::PLAY;
  uint8_t current_gamepad = 0;
  int64_t frames_drawn = 0;
  uint8_t last_jhat = 0;
  uint8_t major = 0, minor = 0;

  UI(LevelId level);
  void Loop();
  void Draw();
  void PlayPause();

  void Seek(int delta);

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

UI::UI(LevelId level) : level(level), fps_per(1.0 / 59.94) {
  std::tie(major, minor) = UnpackLevel(level);
  drawing = sdlutil::makesurface(SCREENW, SCREENH, true);
  CHECK(drawing != nullptr);
  sdlutil::ClearSurface(drawing, 0, 0, 0, 0);
}

void UI::Seek(int delta) {
  game_array->Seek(delta);
}

bool UI::MaybeRunEmulators(uint8_t buttons) {

  if (game_array == nullptr) {
    printf("No game array!\n");
    return false;
  }

  if (mode == Mode::PLAY &&
      fps_per.ShouldRun()) {
    game_array->Step(buttons);
    return true;
  }

  return false;
}

void UI::PlayPause() {
  if (mode == Mode::PAUSE) {
    mode = Mode::PLAY;
  } else {
    mode = Mode::PAUSE;
  }
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
        PlayPause();
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

      case SDLK_s: {
        std::string fm7 = game_array->FocusedFM7();
        std::string filename = StringPrintf("manual-%02x-%02x.fm7",
                                            major, minor);
        Util::WriteFile(filename, fm7);
        printf("Saved " ACYAN("%s") ": " AGREY("%s") "\n",
               filename.c_str(), fm7.c_str());
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

    case SDL_JOYBUTTONDOWN:
      //    4              5
      //
      //
      //                   3
      //
      // 6    7        2       1
      //
      //                   0

      switch (event.jbutton.button) {
      case 2: game_array->CloneFocus(); ui_dirty = true; break;
      case 3: PlayPause(); ui_dirty = true; break;
      case 6: current_gamepad |= INPUT_S; break;
      case 7: current_gamepad |= INPUT_T; break;
      case 0: current_gamepad |= INPUT_B; break;
      case 1: current_gamepad |= INPUT_A; break;
      case 4:
        Seek(-60);
        ui_dirty = true;
        break;
      case 5:
        Seek(+10);
        ui_dirty = true;
        break;

      default:
        printf("Button %d unmapped.\n", event.jbutton.button);
      }
      break;

    case SDL_JOYBUTTONUP:
      switch (event.jbutton.button) {
      case 2: break;
      case 3: break;
      case 6: current_gamepad &= ~INPUT_S; break;
      case 7: current_gamepad &= ~INPUT_T; break;
      case 0: current_gamepad &= ~INPUT_B; break;
      case 1: current_gamepad &= ~INPUT_A; break;
      default:
        printf("Button %d unmapped.\n", event.jbutton.button);
      }
      break;

      break;

    case SDL_JOYHATMOTION:
      //    1
      //
      // 8     2
      //
      //    4
      if (TRACE)
        printf("Hat %d moved to %d.\n", event.jhat.hat, event.jhat.value);

      static constexpr uint8_t JHAT_UP = 1;
      static constexpr uint8_t JHAT_DOWN = 4;
      static constexpr uint8_t JHAT_LEFT = 8;
      static constexpr uint8_t JHAT_RIGHT = 2;

      if (mode == Mode::PLAY) {
        // When playing, this is just mapped to the controller.
        current_gamepad &= ~(INPUT_U | INPUT_D | INPUT_L | INPUT_R);
        if (event.jhat.value & JHAT_UP) current_gamepad |= INPUT_U;
        if (event.jhat.value & JHAT_DOWN) current_gamepad |= INPUT_D;
        if (event.jhat.value & JHAT_LEFT) current_gamepad |= INPUT_L;
        if (event.jhat.value & JHAT_RIGHT) current_gamepad |= INPUT_R;
      } else if (mode == Mode::PAUSE) {
        // When paused, this navigates the focus (on edges).

        auto RisingEdge = [&](int bit) {
            return !!(event.jhat.value & bit) && !(last_jhat & bit);
          };

        if (RisingEdge(JHAT_UP)) game_array->MoveFocus(0, -1);
        if (RisingEdge(JHAT_DOWN)) game_array->MoveFocus(0, 1);
        if (RisingEdge(JHAT_LEFT)) game_array->MoveFocus(-1, 0);
        if (RisingEdge(JHAT_RIGHT)) game_array->MoveFocus(1, 0);

        ui_dirty = true;
      }

      last_jhat = event.jhat.value;
      break;

    default:;
    }
  }

  return ui_dirty ? EventResult::DIRTY : EventResult::NONE;
}

void UI::Loop() {
  for (;;) {
    bool ui_dirty = false;

    if (TRACE) printf("Handle events.\n");

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
      // printf("Flip.\n");
      SDL_Flip(screen);
      ui_dirty = false;
    }
    SDL_Delay(1);
  }
}

void UI::Draw() {
  if (TRACE) printf("Draw.\n");

  CHECK(font != nullptr);
  CHECK(drawing != nullptr);
  CHECK(screen != nullptr);

  CHECK(game_array != nullptr);
  CHECK(game_array->games.size() == NUM_EMUS);

  static constexpr int EMU_MARGIN = 4;

  for (int ey = 0; ey < EMUS_TALL; ey++) {
    for (int ex = 0; ex < EMUS_WIDE; ex++) {
      int eidx = ey * EMUS_WIDE + ex;
      CHECK(eidx >= 0 && eidx < NUM_EMUS);

      const int dx = ex * (256 + EMU_MARGIN);
      const int dy = ey * (240 + EMU_MARGIN);

      // Copy game image to screen.
      // PERF: Too much copying here!!
      Game *game = game_array->games[eidx].get();
      CHECK(game != nullptr);
      if (game != nullptr) {
        ImageRGBA shot = game->Screenshot();
        sdlutil::CopyRGBARect(shot, 0, 0, 256, 240,
                              dx + 1, dy + 1, drawing);
      } else {
        sdlutil::FillRectRGB(drawing,
                             dx + 1, dy + 1, 256, 240, 0x33, 0x00, 0x00);
      }
    }
  }

  // Indicate focus.
  {
    int fx = game_array->FocusX();
    int fy = game_array->FocusY();
    for (int i = 0; i < 4; i++) {
      sdlutil::DrawBox32(drawing,
                         fx * (256 + EMU_MARGIN) + i,
                         fy * (240 + EMU_MARGIN) + i,
                         256 - 2 * i, 240 - 2 * i,
                         0xFFFF0000);
    }
  }

  font->drawto(drawing, 5, 5, StringPrintf("Frames: ^2%lld", frames_drawn));
  sdlutil::blitall(drawing, screen, 0, 0);
  frames_drawn++;
  if (TRACE) printf("Drew %lld.\n", frames_drawn);
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
  if (TRACE) printf("Created screen.\n");

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
  if (TRACE) printf("Created fonts.\n");

  CHECK((cursor_arrow = Cursor::MakeArrow()));
  CHECK((cursor_bucket = Cursor::MakeBucket()));
  CHECK((cursor_hand = Cursor::MakeHand()));
  CHECK((cursor_hand_closed = Cursor::MakeHandClosed()));
  CHECK((cursor_eraser = Cursor::MakeEraser()));
  if (TRACE) printf("Created cursors.\n");

  SDL_SetCursor(cursor_arrow);
  SDL_ShowCursor(SDL_ENABLE);
}

int main(int argc, char **argv) {
  if (TRACE) fprintf(stderr, "In main...\n");
  ANSI::Init();

  CHECK(argc >= 3) << "Usage:\n"
    "./play.exe major minor [startmovie.fm7]\n\n"
    "Major and minor are hex levels 00-ff.\n";

  if (TRACE) fprintf(stderr, "Try initialize SDL...\n");
  InitializeSDL();

  const uint8_t major = strtol(argv[1], nullptr, 16);
  const uint8_t minor = strtol(argv[2], nullptr, 16);
  const LevelId level = PackLevel(major, minor);
  if (TRACE) printf("Play %s\n", ColorLevel(level).c_str());

  {
    MinusDB db;
    if (db.HasSolution(level)) {
      printf(AWHITE("Note") ": %s has a solution already\n",
             ColorLevel(level).c_str());
    } else {
      printf("No solution in database.\n");
    }
  }

  std::vector<uint8_t> startmovie;
  if (argc >= 4) {
    startmovie = SimpleFM7::ReadInputs(argv[3]);
    printf("Start movie has " AWHITE("%d") " inputs.\n",
           (int)startmovie.size());
  }

  emulator_pool = new EmulatorPool(ROMFILE);
  if (TRACE) printf("Created emulator pool.\n");
  {
    CHECK(emulator_pool != nullptr);
    auto emu = emulator_pool->AcquireClean();
    MarioUtil::WarpTo(emu.get(), major, minor, 0);

    level_start = emu->SaveUncompressed();

    evaluator = new Evaluator(emulator_pool, emu.get());
  }
  if (TRACE) printf("Initialized emulator pool and evaluator.\n");

  // PERF: We end up replaying the same movie for each game in
  // the array here.
  game_array = new GameArray(Movie(startmovie));
  if (TRACE) printf("Created GameArray.\n");


  printf("Begin UI loop.\n");

  UI ui(level);
  ui.Loop();

  SDL_Quit();
  return 0;
}

