
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
#include <unordered_set>
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

#include "mov.h"

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
static ImageRGBA *mario = nullptr;
static ImageRGBA *mario_ghost = nullptr;

enum class Speed {
  PLAY,
  PAUSE,
};

enum class View {
  GRID,
  GHOST,
};

namespace {
template<class F>
struct ScopeExit {
  ScopeExit(F &&f) : f(std::forward<F>(f)) {}
  ~ScopeExit() { f(); }
  F f;
};
}

// This wraps a movie (series of inputs), which is rooted at the
// WarpTo state. The movie also has a cursor somewhere in its
// bounds (because it may have been rewound) which is treated
// as the default length of the movie unless specified. It keeps
// save states periodically so that seeking is reasonably
// efficient.
//
// Should be managed from a single thread, but it can be
// copied fairly cheaply (sharing the save states).
struct Movie {
  // We only do saves at movie indices that are divisible by this
  // number.
  static constexpr int SAVE_EVERY = 64;

  // Empty.
  Movie() : Movie(std::vector<uint8_t>{}) {}
  explicit Movie(std::vector<uint8_t> m) : inputs(std::move(m)) {
    auto emu = emulator_pool->Acquire();
    emu->LoadUncompressed(level_start);
    for (int idx = 0; idx < inputs.size(); idx++) {
      // Save state comes before the step. Note that for simplicity,
      // we don't save a state until after we execute the step for
      // that frame. (We could do this on the last frame if it fall
      // on the right divisor.)
      if ((idx % SAVE_EVERY) == 0) {
        CHECK(saves.size() == idx / SAVE_EVERY);
        std::vector<uint8_t> *s = new std::vector<uint8_t>;
        emu->SaveUncompressed(s);
        saves.emplace_back(s);
      }

      emu->StepFull(inputs[idx], 0);
    }

    cursor = (int)inputs.size();
  }

  uint8_t operator [](size_t idx) const {
    return inputs[idx];
  }

  // Take an emulator in any state, and put it at the state described
  // by this movie. Can be linear time if nothing is cached.
  void GoTo(Emulator *emu) {
    // Backwards from cursor until we find the save to start
    // from. Remember that the save state is the state *before*
    // the button press at this index.
    const int start_idx = [this, emu]() {
        if (cursor > 0) {
          for (int save_idx = (cursor - 1) / SAVE_EVERY;
               save_idx >= 0;
               save_idx --) {
            CHECK(save_idx >= 0 & save_idx < (int)saves.size());
            if (saves[save_idx].get() != nullptr) {
              emu->LoadUncompressed(*saves[save_idx]);
              return save_idx * SAVE_EVERY;
            }
          }
        }

        // From the beginning, then.
        emu->LoadUncompressed(level_start);
        return 0;
      }();

    // Now just execute frames until we reach the cursor.
    for (int idx = start_idx; idx < cursor; idx++) {
      CHECK(idx >= 0 && idx <= inputs.size());
      emu->StepFull(inputs[idx], 0);
    }

    // Done.
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

  // Assumes the emulator is currently at the cursor. Execute a step
  // and add it to the movie. Destroys future inputs unless they match
  // exactly. Saves state as appropriate.
  void Push(uint8_t b, Emulator *emu) {
    if ((int)inputs.size() > cursor) {
      if (inputs[cursor] == b) {
        // This is already the next input. In this case
        // we keep the future inputs.
        CHECK(cursor < inputs.size());
        cursor++;
        emu->StepFull(b, 0);
        return;
      } else {
        // Otherwise we need to delete future inputs and
        // future save states.
        inputs.resize(cursor);

        // We're about to push an input. Make sure we have the
        // right number of save states first. For example, if
        // this is input 0, we should have no save states yet.
        // If it's 1, then we should have the start state saved.
        // Similarly, if it is input 64 (with SAVE_EVERY=64), we
        // should have just the start state, but we're about
        // to save another.
        if (cursor == 0) {
          saves.clear();
        } else {
          const int new_size = 1 + ((cursor - 1) / SAVE_EVERY);
          CHECK(saves.size() >= new_size);
          saves.resize(new_size);
        }
      }
    }

    CHECK(cursor == inputs.size());
    // Now push a frame. We might need to save first.
    if (inputs.size() % SAVE_EVERY == 0) {
      const int num_saves_after = 1 + (inputs.size() / SAVE_EVERY);
      CHECK(saves.size() == num_saves_after - 1) <<
        "With inputs: " << inputs.size() << " saves are " <<
        saves.size() << " vs " << (inputs.size() / SAVE_EVERY);
      /*
      printf("OK: saves %d (want %d), inputs %d\n",
             (int)saves.size(), (int)(inputs.size() / SAVE_EVERY),
             (int)inputs.size());
      */
      std::vector<uint8_t> *s = new std::vector<uint8_t>;
      emu->SaveUncompressed(s);
      saves.emplace_back(s);
    }

    emu->StepFull(b, 0);
    inputs.push_back(b);

    // And advance.
    cursor++;
    CHECK(cursor == inputs.size());
  }

  // Returns the size of the movie up to the current cursor.
  int Size() const {
    return cursor;
  }

  // Returns the size of the movie, including future inputs.
  int FullSize() const {
    return (int)inputs.size();
  }

  // With the emulator in any state, seek (up to) delta frames forward
  // or backward (for negative deltas). Does not change the movie.
  void Seek(int delta, Emulator *emu) {
    cursor = std::clamp(cursor + delta, 0, (int)inputs.size());
    GoTo(emu);
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

  // Check that the movie is consistent with its savestates.
  void Validate() {
    CHECK(cursor >= 0 && cursor <= inputs.size());
    auto emu = emulator_pool->Acquire();
    emu->LoadUncompressed(level_start);
    // Check every index. Cursor doesn't actually matter as long as
    // it's in bounds.
    for (int idx = 0; idx < inputs.size(); idx++) {
      if (idx % SAVE_EVERY == 0) {
        const int sidx = idx / SAVE_EVERY;
        CHECK(sidx < saves.size());
        auto save_emu = emulator_pool->Acquire();
        CHECK(saves[sidx].get() != nullptr) << "We should be able "
          "to leave these out, but for now it is expected that "
          "they are always non-null.";
        save_emu->LoadUncompressed(*saves[sidx]);

        CHECK(save_emu->MachineChecksum() == emu->MachineChecksum());
      }

      emu->StepFull(inputs[idx], 0);
    }
  }

 private:
  using SharedSave = std::shared_ptr<const std::vector<uint8_t>>;

  // Two parallel arrays, but we only have saves every 1/SAVE_EVERY
  // frames.
  //
  // The executed button at each step.
  std::vector<uint8_t> inputs;
  // The save state at saves[idx] is from *before* the input at
  // inputs[idx * SAVE_EVERY].
  //
  // We use shared pointer to make it relatively cheap to copy movies
  // (we generate a lot of tree-structured shraring in this program).
  std::vector<SharedSave> saves;

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
    movie.Push(b, emu.get());
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

    movie.GoTo(emu.get());
    img = MarioUtil::Screenshot(emu.get());
  }

  // PERF: We could use RGB for these?
  ImageRGBA Screenshot() {
    MutexLock ml(&m);
    CHECK(img.Width() > 0 && img.Height() > 0);
    return img;
  }

  // TODO: Validate game state as well.
  void Validate() {
    movie.Validate();
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
    RandomGaussian gauss(&rc);
    const int movie_size = games[focus_idx]->movie.Size();
    uint8_t prev_button = movie_size == 0 ? 0 :
      games[focus_idx]->movie[movie_size - 1];
    for (int i = 0; i < NUM_EMUS; i++) {
      {
        int jitter = std::clamp((int)std::round(gauss.Next() * 2.0), -4, 4);
        std::unique_lock<std::mutex> ml(m);
        work.emplace_back([this, i, prev_button, orig, jitter]() {
            uint8_t b = orig;
            if (i == focus_idx) {
              // Always do the input faithfully on the focused
              // game.
              games[i]->Step(b);
            } else {

              if (prev_button == b) {
                // Rarely, fuzz the input a bit.
                if (rc.Byte() < 12) {
                  if (rc.Byte() < 8) b ^= INPUT_B;
                  if (rc.Byte() < 8) b &= ~INPUT_A;
                  if (rc.Byte() < 8) b ^= INPUT_R;
                  if (rc.Byte() < 8) b ^= INPUT_L;
                }

                games[i]->Step(b);

              } else {
                // When the button state has changed, add time jitter.
                // Add time jitter, forwards and backwards.
                if (jitter < 0) {
                  games[i]->Seek(jitter);
                  // XXX This will keep the games synchronized, but we
                  // do not need to do that.
                  for (int j = 0; j < -jitter; j++) {
                    games[i]->Step(b);
                  }
                } else if (jitter == 0) {
                  games[i]->Step(b);
                } else {
                  int sz = games[i]->movie.Size();
                  uint8_t oldb = sz == 0 ? 0 : games[i]->movie[sz - 1];
                  for (int j = 0; j < jitter - 1; j++) {
                    games[i]->Step(oldb);
                  }
                  games[i]->Step(b);
                }
              }
            }
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

  // Get the current (focused) movie as a checkpoint
  // that can be recalled later.
  Movie Checkpoint() const {
    return games[focus_idx]->movie;
  }

  // Load the checkpoint into all slots.
  void LoadCheckpoint(const Movie &movie) {
    // PERF in threads?
    for (int idx = 0; idx < NUM_EMUS; idx++) {
      games[idx].reset(new Game(movie));
    }
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

  void Validate() {
    ParallelAppi(games,
                 [](int idx, std::unique_ptr<Game> &g) {
                   CHECK(g.get() != nullptr);
                   printf("Validate %d\n", idx);
                   g->Validate();
                   printf("%d ok\n", idx);
                }, 12);
  }

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

struct Watchlist {
  struct MemLoc {
    enum class DisplayType {
      WORD,
      DASH,
      COLON,
    };
    MemLoc(const std::string &name, uint16_t addrhi, uint16_t addrlo,
           DisplayType display_type = DisplayType::WORD) :
      name(name), addrhi(addrhi), addrlo(addrlo), display_type(display_type) {}
    // For single-byte locations.
    MemLoc(const std::string &name, uint16_t addr) : MemLoc(name, addr, addr) {}

    std::string RenderValue(const Emulator *emu) const {
      if (addrhi == addrlo) {
        return StringPrintf("%02x", emu->ReadRAM(addrhi));
      } else {
        uint8_t hi = emu->ReadRAM(addrhi);
        uint8_t lo = emu->ReadRAM(addrlo);
        switch (display_type) {
        default:
        case DisplayType::WORD:
          return StringPrintf("%04x", (uint16_t(hi) << 8) | lo);
        case DisplayType::DASH:
          return StringPrintf("%02x-%02x", hi, lo);
        case DisplayType::COLON:
          return StringPrintf("%02x:%02x", hi, lo);
        }
      }
    }

    std::string name;
    // If addrhi = addrlo, then this is just a single byte quantity.
    uint16_t addrhi = 0;
    uint16_t addrlo = 0;
    DisplayType display_type = DisplayType::WORD;
  };

  Watchlist() {}
  Watchlist(std::vector<MemLoc> e) : entries(std::move(e)) {}
  std::vector<MemLoc> entries;
};

struct UI {
  const LevelId level;
  Speed speed = Speed::PLAY;
  View view = View::GRID;
  uint8_t current_gamepad = 0;
  int64_t frames_drawn = 0;
  uint8_t last_jhat = 0;
  uint8_t major = 0, minor = 0;

  std::optional<Movie> checkpoint = std::nullopt;

  UI(LevelId level);
  void Loop();
  void Draw();
  void DrawGrid();
  void DrawGhost();

  void DrawWatchlist();
  void DrawMemory();

  void PlayPause();

  void Seek(int delta);

  Periodically fps_per;
  // Returns true if dirty.
  bool MaybeRunEmulators(uint8_t buttons);

  enum class EventResult { NONE, DIRTY, EXIT, };
  EventResult HandleEvents();

  // SDL_Surface *drawing = nullptr;
  std::unique_ptr<ImageRGBA> drawing;
  int mousex = 0, mousey = 0;
  bool dragging = false;

  std::pair<int, int> drag_source = {-1, -1};
  int drag_handlex = 0, drag_handley = 0;
  Watchlist watchlist;

  std::unique_ptr<MOV::Out> mov;
};

UI::UI(LevelId level) : level(level), fps_per(1.0 / 59.94) {
  std::tie(major, minor) = UnpackLevel(level);
  drawing.reset(new ImageRGBA(SCREENW, SCREENH));
  CHECK(drawing != nullptr);
  drawing->Clear32(0x000000FF);

  using MemLoc = Watchlist::MemLoc;
  watchlist = Watchlist({
      MemLoc("Frame", FRAME_COUNTER),
      MemLoc("X", PLAYER_X_HI, PLAYER_X_LO),
      MemLoc("Y", PLAYER_Y_SCREEN, PLAYER_Y),
      MemLoc("Player", PLAYER_STATE),
      MemLoc("World", WORLD_MAJOR, WORLD_MINOR, MemLoc::DisplayType::DASH),
      MemLoc("Mode:Task", OPER_MODE, OPER_MODE_TASK,
             MemLoc::DisplayType::COLON),
      MemLoc("Sub", GAME_ENGINE_SUBROUTINE),
      MemLoc("Loop?", LOOP_COMMAND),
      MemLoc("Page:Col", CURRENT_PAGE_LOC, CURRENT_COLUMN_POS,
             MemLoc::DisplayType::COLON),
    });
}

void UI::Seek(int delta) {
  game_array->Seek(delta);
}

bool UI::MaybeRunEmulators(uint8_t buttons) {

  if (game_array == nullptr) {
    printf("No game array!\n");
    return false;
  }

  if (speed == Speed::PLAY &&
      fps_per.ShouldRun()) {
    game_array->Step(buttons);
    return true;
  }

  return false;
}

void UI::PlayPause() {
  if (speed == Speed::PAUSE) {
    speed = Speed::PLAY;
  } else {
    speed = Speed::PAUSE;
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

      case SDLK_HOME: {
        // TODO: Restart level...

        speed = Speed::PAUSE;
        ui_dirty = true;
        break;
      }

      // Delete, backspace, ...?

      case SDLK_LEFT: {
        // TODO: Rewind all movies one step, and pause.

        speed = Speed::PAUSE;
        ui_dirty = true;
        break;
      }

      case SDLK_RIGHT: {
        // TODO: Forward all movies (if possible) ?

        speed = Speed::PAUSE;
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

      case SDLK_RETURN:
      case SDLK_KP_ENTER:
        switch (view) {
        case View::GHOST: view = View::GRID; break;
        case View::GRID: view = View::GHOST; break;
        default: break;
        }
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

      case SDLK_v: {
        Timer timer;
        game_array->Validate();
        printf("Validated in %s.\n", ANSI::Time(timer.Seconds()).c_str());
        break;
      }

      case SDLK_r: {
        if (mov.get() != nullptr) {
          mov.reset();
          printf("Stopped recording.\n");
        } else {
          std::string filename = StringPrintf("rec-%02x-%02x.mov",
                                              major, minor);
          mov = MOV::OpenOut(filename, SCREENW, SCREENH, MOV::DURATION_60,
                             MOV::Codec::PNG_MINIZ);
        }
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
        if (checkpoint.has_value()) {
          game_array->LoadCheckpoint(checkpoint.value());
          ui_dirty = true;
        } else {
          Seek(-60);
          ui_dirty = true;
        }
        break;
      case 5:
        Seek(+10);
        ui_dirty = true;
        break;

      case 8:
        if (checkpoint.has_value()) {
          checkpoint.reset();
          printf("Cleared checkpoint.\n");
        } else {
          checkpoint = game_array->Checkpoint();
          printf("Saved checkpoint.\n");
        }

      default:
        printf("Button %d unmapped.\n", event.jbutton.button);
      }
      break;

    case SDL_JOYBUTTONUP:
      switch (event.jbutton.button) {
      case 2: break;
      case 3: break;
      case 4: break;
      case 5: break;
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

      if (speed == Speed::PLAY) {
        // When playing, this is just mapped to the controller.
        current_gamepad &= ~(INPUT_U | INPUT_D | INPUT_L | INPUT_R);
        if (event.jhat.value & JHAT_UP) current_gamepad |= INPUT_U;
        if (event.jhat.value & JHAT_DOWN) current_gamepad |= INPUT_D;
        if (event.jhat.value & JHAT_LEFT) current_gamepad |= INPUT_L;
        if (event.jhat.value & JHAT_RIGHT) current_gamepad |= INPUT_R;
      } else if (speed == Speed::PAUSE) {
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

void UI::DrawGrid() {
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
        drawing->CopyImageRect(dx + 1, dy + 1, shot, 0, 0, 256, 240);
      } else {
        drawing->FillRect32(dx + 1, dy + 1, 256, 240, 0x330000FF);
      }
    }
  }

  // Indicate focus.
  {
    int fx = game_array->FocusX();
    int fy = game_array->FocusY();
    for (int i = 0; i < 4; i++) {
      drawing->BlendBox32(fx * (256 + EMU_MARGIN) + i,
                          fy * (240 + EMU_MARGIN) + i,
                          256 - 2 * i, 240 - 2 * i,
                          0xFF0000FF, {0xFF0000AA});
    }
  }

}

void UI::DrawGhost() {
  static constexpr int PX = 6;

  // All ghosts; absolute coordinates.
  std::vector<MarioUtil::Pos> ghosts;
  ghosts.reserve(game_array->games.size());
  for (int i = 0; i < game_array->games.size(); i++) {
    // No ghost for the focused game, though.
    if (i != game_array->focus_idx) {
      const Game *game = game_array->games[i].get();
      ghosts.push_back(MarioUtil::GetPos(game->emu.get()));
    }
  }

  // 1x screenshot.
  Game *game = game_array->games[game_array->focus_idx].get();
  ImageRGBA shot = game->Screenshot();
  if (game != nullptr) {
    ImageRGBA shot = game->Screenshot();
  } else {
    shot = ImageRGBA(256, 240);
    shot.Clear32(0x330000FF);
  }

  int screenx = (game->emu->ReadRAM(SCREENLEFT_X_HI) << 8) |
    game->emu->ReadRAM(SCREENLEFT_X_LO);

  CHECK(mario_ghost != nullptr);
  // Now add ghosts...
  for (const MarioUtil::Pos &pos : ghosts) {

    int yy = (int)pos.y - 232 - 8;
    int xx = (int)pos.x - screenx;

    // XXX use the sprite from the game
    shot.BlendImage(xx, yy, *mario_ghost);
  }

  // XXX test: re-draw sprites from main game.
  {
    std::vector<Emulator::Sprite> sprites = game->emu->Sprites();
    shot.BlendText32(10, 10, 0xFF00FFFF, StringPrintf("%d sprites",
                                                      (int)sprites.size()));
    for (Emulator::Sprite &sprite : sprites) {
      if (true || sprite.y < 240) {
        ImageRGBA simg(std::move(sprite.rgba), sprite.Width(), sprite.Height());
        for (int y = 0; y < simg.Height(); y++) {
          for (int x = 0; x < simg.Width(); x++) {
            uint32_t c = simg.GetPixel32(x, y);
            c &= 0xFFFFFFAA;
            c |= 0x08000000;
            simg.SetPixel32(x, y, c);
          }
        }
        shot.BlendImage(sprite.x, sprite.y, simg);
      }
    }
  }

  // PERF Without an intermediate copy!
  drawing->CopyImage(0, 0, shot.ScaleBy(PX));
}

// TODO: These should probably take the destination position?
void UI::DrawWatchlist() {
  Game *game = game_array->games[game_array->focus_idx].get();
  CHECK(game != nullptr);
  const Emulator *emu = game->emu.get();

  using MemLoc = Watchlist::MemLoc;
  ImageRGBA img(256, watchlist.entries.size() * 10);
  img.Clear32(0x000000FF);
  int y = 0;
  int col2 = 0;
  for (const MemLoc &loc : watchlist.entries) {
    col2 = std::max(col2, 10 + (int)loc.name.size() * 9);
  }

  for (const MemLoc &loc : watchlist.entries) {
    img.BlendText32(0, y, 0xFFFF77FF, loc.name);
    img.BlendText32(col2, y, 0xFFFFFFFF, loc.RenderValue(emu));
    y += 10;
  }

  // PERF without intermediate copy.
  drawing->CopyImage(256 * 6 + 8, 8, img.ScaleBy(2));
  /*
  sdlutil::CopyRGBARectNX(img, 2,
                          0, 0, img.Width(), img.Height(),
                          256 * 6 + 8, 8, drawing);
  */
}

void UI::DrawMemory() {
  Game *game = game_array->games[game_array->focus_idx].get();
  CHECK(game != nullptr);
  const Emulator *emu = game->emu.get();

  // 2048 bytes of memory
  ImageRGBA img(64, 32);
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 64; x++) {
      uint8_t b = emu->ReadRAM(y * 64 + x);
      img.SetPixel(x, y, b, b, b, 0xFF);
    }
  }

  drawing->CopyImage(256 * 6 + 8, 480, img.ScaleBy(4));
  /*
  sdlutil::CopyRGBARectNX(img, 4,
                          0, 0, img.Width(), img.Height(),
                          256 * 6 + 8, 480, drawing);
  */
}

void UI::Draw() {
  if (TRACE) printf("Draw.\n");

  CHECK(font != nullptr);
  CHECK(drawing != nullptr);
  CHECK(screen != nullptr);

  CHECK(game_array != nullptr);
  CHECK(game_array->games.size() == NUM_EMUS);


  switch (view) {
  case View::GRID:
    DrawGrid();
    break;

  case View::GHOST:
    DrawGhost();
    DrawWatchlist();
    DrawMemory();
    break;

  default:
    break;
  }

  // XXX. Use ImageRGBA for off-screen image, then save to mov if active.

  drawing->BlendText32(5, 5, 0xFFFF00AA,
                       StringPrintf("Frames: %lld", frames_drawn));
  // font->drawto(drawing, 5, 5, StringPrintf("Frames: ^2%lld", frames_drawn));
  // sdlutil::blitall(drawing, screen, 0, 0);

  /*
  sdlutil::CopyRGBARect(*drawing,
                        0, 0, SCREENW, SCREENH,
                        0, 0, screen);
  */
  sdlutil::CopyRGBAToScreen(*drawing, screen);

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

// Simple interactive loop to pick a level.
static std::optional<LevelId> GetLevel(MinusDB *db) {
  std::unordered_set<LevelId> rejected = db->GetRejected();
  std::unordered_set<LevelId> solved = db->GetSolved();

  static constexpr int PX = 6;
  ImageRGBA img(256 * PX, 256 * PX);
  for (int y = 0; y < 256; y++) {
    for (int x = 0; x < 256; x++) {
      LevelId level = PackLevel(y, x);
      uint32_t c = 0x000044FF;
      if (rejected.contains(level)) {
        c = 0xAA0000FF;
      } else if (solved.contains(level)) {
        c = 0x00AA00FF;
      }

      for (int yy = 0; yy < PX; yy++) {
        for (int xx = 0; xx < PX; xx++) {
          img.SetPixel32(x * PX + xx, y * PX + yy, c);
        }
      }
    }
  }

  SDL_Surface *drawing = sdlutil::makesurface(SCREENW, SCREENH, true);
  ScopeExit se([drawing]() {
      SDL_FreeSurface(drawing);
    });

  sdlutil::CopyRGBARect(img, 0, 0, img.Width(), img.Height(),
                        0, 0, drawing);

  sdlutil::blitall(drawing, screen, 0, 0);
  SDL_Flip(screen);

  for (;;) {
    SDL_Event event;
    int mousex = 0, mousey = 0;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        printf("QUIT.\n");
        return std::nullopt;

      case SDL_KEYDOWN: {
        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
          printf("ESCAPE.\n");
          return std::nullopt;
        default:
          break;
        }
      }

      case SDL_MOUSEMOTION: {
        SDL_MouseMotionEvent *e = (SDL_MouseMotionEvent*)&event;

        [[maybe_unused]] const int oldx = mousex, oldy = mousey;

        mousex = e->x;
        mousey = e->y;

        break;
      }

      case SDL_MOUSEBUTTONDOWN: {
        SDL_MouseButtonEvent *e = (SDL_MouseButtonEvent*)&event;

        if (e->button == SDL_BUTTON_LEFT) {
          int major = e->y / PX;
          int minor = e->x / PX;
          printf("Click %d,%d\n", e->x, e->y);
          if (major >= 0 && major < 256 &&
              minor >= 0 && minor < 256) {
            return {PackLevel(major, minor)};
          }
        }
        break;
      }

      default:
        break;
      }
    }
  }

  LOG(FATAL) << "Unreachable";
  return std::nullopt;
}

void InitializeGraphics() {
  mario = ImageRGBA::Load("mario.png");
  CHECK(mario != nullptr);

  mario_ghost = new ImageRGBA(*mario);
  for (int y = 0; y < mario_ghost->Height(); y++) {
    for (int x = 0; x < mario_ghost->Width(); x++) {
      const auto [r, g, b, a] = mario_ghost->GetPixel(x, y);
      mario_ghost->SetPixel(x, y, g, r, b, a * 0.33);
    }
  }
}

int main(int argc, char **argv) {
  if (TRACE) fprintf(stderr, "In main...\n");
  ANSI::Init();

  if (TRACE) fprintf(stderr, "Try initialize SDL...\n");
  InitializeSDL();

  InitializeGraphics();

  MinusDB db;

  LevelId level = 0;
  if (argc < 3) {
    if (auto lo = GetLevel(&db)) {
      level = lo.value();
    } else {
      printf("Command-line usage:\n"
             "./play.exe major minor [startmovie.fm7]\n\n"
             "Major and minor are hex levels 00-ff.\n");
      return -1;
    }

  } else {
    const uint8_t major = strtol(argv[1], nullptr, 16);
    const uint8_t minor = strtol(argv[2], nullptr, 16);
    level = PackLevel(major, minor);
  }

  printf("Play %s\n", ColorLevel(level).c_str());

  db.ForEachRejected([level](const MinusDB::RejectedRow &row) {
      if (row.level == level) {
        printf("%s " ARED("REJECTED") " by %s on " AGREY("%lld") "\n",
               ColorLevel(level).c_str(),
               MinusDB::MethodName(row.method),
               row.createdate);
      }
    });

  if (db.HasSolution(level)) {
    printf(AWHITE("Note") ": %s has a solution already\n",
           ColorLevel(level).c_str());
  } else {
    printf("No solution in database.\n");
  }

  std::vector<uint8_t> startmovie;
  if (argc >= 4) {
    startmovie = SimpleFM7::ReadInputs(argv[3]);
    printf("Start movie has " AWHITE("%d") " inputs.\n",
           (int)startmovie.size());
  }

  const auto &[major, minor] = UnpackLevel(level);

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

