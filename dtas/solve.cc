
// Solves Mario Bros.; kinda like playfun but with as much
// game-specific logic as desired.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"

#include "image.h"
#include "timer.h"
#include "periodically.h"
#include "threadutil.h"
#include "arcfour.h"
#include "randutil.h"
#include "ansi.h"

#include "base/stringprintf.h"
#include "base/logging.h"

#include "mario.h"
#include "mario-util.h"
#include "database.h"

using uint8 = uint8_t;

static constexpr const char *ROMFILE = "mario.nes";

// MAJOR:MINOR, big endian.
using LevelId = uint16_t;
static std::pair<uint8_t, uint8_t> UnpackLevel(LevelId id) {
  return std::make_pair((id >> 8) & 0xFF, id & 0xFF);
}

static std::string ColorLevel(LevelId id) {
  const auto &[major, minor] = UnpackLevel(id);
  return StringPrintf(ACYAN("%d") AGREY("-") ACYAN("%d"), major, minor);
}

struct MinusDB {
  static constexpr const char *DBFILE = "minus.sqlite";

  using Query = Database::Query;
  using Row = Database::Row;

  MinusDB() {
    db = Database::Open(DBFILE);
    CHECK(db.get() != nullptr);
    Init();
  }

  void Init() {
    db->ExecuteAndPrint("create table "
                      "if not exists "
                      "solutions ("
                      "id int primary key, "
                      "level int not null, "
                      "fm7 mediumtext not null"
                      ")");

    db->ExecuteAndPrint("create table "
                      "if not exists "
                      "attempts ("
                      "level int primary key, "
                      "num int not null"
                      ")");
  }

  // In an arbitrary order.
  std::unordered_set<LevelId> GetDone() {
    db->ExecuteAndPrint("select level from solutions");

    std::unordered_set<LevelId> done;
    std::unique_ptr<Query> q =
      db->ExecuteString("select level from solutions");
    while (std::unique_ptr<Row> r = q->NextRow()) {
      done.insert((uint16_t)r->GetInt(0));
    }
    return done;
  }

  void AddSolution(LevelId id, const std::vector<uint8_t> &sol) {
    std::string fm7 = SimpleFM7::EncodeInputs(sol);
    db->ExecuteString(
        StringPrintf("insert into solutions (level, fm7) "
                     "values (%d, \"%s\")",
                     id, fm7.c_str()));
  }

  std::unique_ptr<Database> db;
};


// We're not trying to find a particularly short solution, just a solution.

struct Evaluator {
  // Initialize with some emulator state; we use that to read the current
  // level and lives and so on.
  Evaluator(const Emulator *emu) {
    world_major = emu->ReadRAM(WORLD_MAJOR);
    world_minor = emu->ReadRAM(WORLD_MINOR);

    start_lives = emu->ReadRAM(NUMBER_OF_LIVES);
  }

  // True if we've beaten the level.
  bool Succeeded(const Emulator *emu) const {
    // XXX I think there is also something like "win flag" for when
    // you finish the game. We need to detect this since many levels
    // are won by defeating Bowser, king of the shell people.
    return emu->ReadRAM(WORLD_MAJOR) > world_major ||
      emu->ReadRAM(WORLD_MINOR) > world_minor;
  }

  bool Stuck(const Emulator *emu) const {
    // XXX Detect main menu.

    // Note: Occasionally dying can be useful to warp to the halfway
    // point.
    if (emu->ReadRAM(NUMBER_OF_LIVES) < start_lives) {
      return true;
    }

    return false;
  }

  double Eval(const Emulator *emu) const {
    uint8_t xhi = emu->ReadRAM(PLAYER_X_HI);
    uint8_t xlo = emu->ReadRAM(PLAYER_X_LO);

    uint16_t x = (uint16_t(xhi) << 8) | xlo;
    // double xfrac = x / (double)0xFFFF;

    // Decimal part of score is x position (unless penalized);
    // fractional part is heuristics.
    double score = x;

    // Heuristics:

    // More time is better.
    const int timer =
      emu->ReadRAM(TIMER1) * 100 +
      emu->ReadRAM(TIMER2) * 10 +
      emu->ReadRAM(TIMER3);

    const double tfrac = (timer / (double)400);

    score += tfrac;

    // TODO: Better to have high x velocity.

    // Heuristics: Better to be high on the screen.

    // 0 if above screen, 1 if on screen, 2+ if below
    uint8_t yscreen = emu->ReadRAM(PLAYER_Y_SCREEN);
    uint8_t ypos = emu->ReadRAM(PLAYER_Y);

    // 256-512 is on-screen.
    uint16_t y = (uint16_t(yscreen) << 8) | ypos;
    if (y > 511) {
      // Very bad to be off screen downward, as we will
      // definitely die.
      return -200000.0 + score;
    } else if (y > 256 + 176) {
      // When on screen, we're uncomfortable if we're below 176,
      // which is below the bottom two rows of bricks.

      int depth = y - (256 + 176);
      return -100000.0 - depth + score;
    } else {
      // Otherwise we're not falling off the screen. But give
      // some bonus when we're higher up (lower y coordinate).
      int height = -(y - (256 + 176));

      return score + (height * 0.05);
    }
  }

  uint8_t world_major = 0, world_minor = 0;
  uint8_t start_lives = 0;
};

struct State {
  // Inputs starting after the WarpTo.
  std::vector<uint8_t> movie;
  // The save state.
  std::vector<uint8_t> save;
  double eval = 0.0;
};

// First a quick-and-dirty solver. This is more or less the playfun
// algorithm, but special-cased to a mario-specific objective.
struct Solver {

  std::mutex should_die_m;
  bool should_die = false;

  std::mutex pop_m;
  std::vector<State> population;

  bool ShouldDie() {
    std::unique_lock<std::mutex> ml(should_die_m);
    return should_die;
  }

  void SetShouldDie() {
    std::unique_lock<std::mutex> ml(should_die_m);
    should_die = true;
  }

  // Thread safe, but needs a thread-local RNG.
  // Returns a copy so that we don't need to worry about lifetimes.
  State SamplePopulation(ArcFour *rc) {
    MutexLock ml(&pop_m);
    CHECK(!population.empty());

    // TODO: Weight this towards higher evals.
    int idx = RandTo(rc, population.size());
    return population[idx];
  }

  static std::vector<uint8_t> MoveGen(const Emulator &emu, ArcFour *rc, int len) {
    // Hard-coded for Mario.
    // TODO: Heuristics:
    //   - If low on the screen, prefer jumping
    //   - Don't move into enemies
    //   - Move left or jump if blocked
    //   - Move left or right if over nothing
    //   - Jump if on ground but down-right is empty
    //   - Press down on pipes?

    std::vector<uint8_t> ret;
    ret.reserve(len);
    while (ret.size() < len) {
      // We generate an action and a duration.
      int dur = 1 + RandTo(rc, 30);

      uint8_t act = 0;

      // Usually hold B. We move faster, for one thing.
      if (rc->Byte() < 200) {
        act |= INPUT_B;
      }

      // Usually hold right. The exit is always to the
      // right.
      if (rc->Byte() < 160) {
        act |= INPUT_R;
      }

      // Always Be Jumping.
      if (rc->Byte() < 128) {
        act |= INPUT_A;
      }

      // Rarely, turn around.
      if (rc->Byte() < 32) {
        act |= INPUT_L;
        act &= ~INPUT_R;
      }

      // In rare cases, press down to go down pipes.
      if (rc->Byte() < 4) {
        act = INPUT_D;
      }

      // In rare cases, hold up (to go up vines).
      // Since you need to go far up vines, we
      // set a long duration for this.
      if (rc->Byte() < 4) {
        act = INPUT_U;
        dur = len;
      }

      // Finally, in rare cases, briefly input an arbitrary collection
      // of buttons (but no pause/select).
      if (rc->Byte() < 4) {
        act = rc->Byte() & ~(INPUT_T | INPUT_S);
        dur = 1;
      }

      while (ret.size() < len && dur > 0) {
        ret.push_back(act);
        dur--;
      }
    }

    return ret;
  }

  struct Future {
    std::vector<uint8_t> moves;
    std::vector<uint8_t> save;
    double eval = 0.0;
    bool stuck = false;
    bool done = false;
    double score = 0.0;
  };

  static constexpr int MAX_POPULATION = 50;

  void WorkThread(uint64_t seed) {
    ArcFour rc(seed);
    std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));

    for (;;) {
      if (ShouldDie()) return;

      State start = SamplePopulation(&rc);

      // One second of gameplay.
      static constexpr int MOVES_PER_FUTURE = 60;
      static constexpr int NUM_FUTURES = 12;
      std::vector<Future> futures;
      futures.reserve(NUM_FUTURES);
      for (int i = 0; i < NUM_FUTURES; i++) {
        futures.push_back(Future{.moves = MoveGen(*emu, &rc, MOVES_PER_FUTURE)});
      }

      // Execute each and get stats.
      for (Future &future : futures) {
        emu->LoadUncompressed(start.save);
        for (const uint8_t b : future.moves) {
          emu->Step(b, 0);
        }

        future.save = emu->SaveUncompressed();
        future.eval = evaluator->Eval(emu.get());
        future.stuck = evaluator->Stuck(emu.get());
        future.done = evaluator->Succeeded(emu.get());
        // (could return immediately if one of them wins!)
      }

      std::erase_if(futures,
                    [](const Future &f) {
                      return f.stuck;
                    });

      // If any of these end up successful, we are done.
      for (const Future &f : futures) {
        if (f.done) {
          std::vector<uint8_t> full_sol = start.movie;
          for (uint8_t b : f.moves) full_sol.push_back(b);
          db->AddSolution(level_id, full_sol);
          printf("Solved %s: %s\n",
                 ColorLevel(level_id).c_str(),
                 SimpleFM7::EncodeInputs(full_sol).c_str());
          SetShouldDie();
          return;
        }
      }

      // Eliminate it if we've gone backwards...
      std::erase_if(futures,
                    [&start](const Future &f) {
                      return f.eval < start.eval;
                    });

      for (Future &f : futures) {
        // TODO: Score heuristics can include "random futures"
        // or "do I just die if I wait here".
        f.score = f.eval;
      }

      std::sort(futures.begin(), futures.end(),
                [](const auto &a, const auto &b) {
                  return a.score > b.score;
                });

      // Add one to the population.
      if (!futures.empty()) {
        // TODO: Use weighted random sampling. Include the possibility
        // of adding more than one, too?
        Future &best = futures[0];
        State ext = start;
        for (uint8_t b : best.moves) ext.movie.push_back(b);
        ext.save = std::move(best.save);
        ext.eval = best.eval;
        futures.clear();

        MutexLock ml(&pop_m);
        population.push_back(std::move(ext));
      }

      // Reduce population if it's big enough.
      {
        MutexLock ml(&pop_m);
        if (population.size() > MAX_POPULATION) {
          std::sort(population.begin(), population.end(),
                    [](const auto &a, const auto &b) {
                      return a.eval > b.eval;
                    });
          population.resize(MAX_POPULATION);
        }
      }
    }
  }

  Solver(MinusDB *db, uint8_t major, uint8_t minor, double solve_time = 3600.0,
         int num_threads = 8) :
    db(db),
    level_id((uint16_t(major) << 8) | minor) {
    std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
    CHECK(emu.get() != nullptr) << ROMFILE;
    MarioUtil::WarpTo(emu.get(), major, minor, 0);
    start_state = emu->SaveUncompressed();

    evaluator.reset(new Evaluator(emu.get()));

    CHECK(!evaluator->Stuck(emu.get()));

    {
      State empty;
      empty.movie = {};
      empty.save = emu->SaveUncompressed();
      empty.eval = evaluator->Eval(emu.get());
      population.push_back(std::move(empty));
    }

    std::vector<std::thread> workers;
    for (int i = 0; i < num_threads; i++) {
      workers.emplace_back([this, i]() {
          return this->WorkThread(i);
        });
    }

    Timer solve_timer;
    Periodically status_per(10.0);
    for (;;) {
      if (ShouldDie()) {
        break;
      } else if (solve_timer.Seconds() > solve_time) {
        printf("Solver on %s ran out of time (%s).\n",
               ColorLevel(level_id).c_str(),
               ANSI::Time(solve_timer.Seconds()).c_str());
        SetShouldDie();
        break;
      }

      {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(1s);
      }

      if (status_per.ShouldRun()) {
        MutexLock ml(&pop_m);
        printf("Running on %s. Population size %d. Elapsed %s/%s.\n",
               ColorLevel(level_id).c_str(),
               (int)population.size(),
               ANSI::Time(solve_timer.Seconds()).c_str(),
               ANSI::Time(solve_time).c_str());
        for (int i = 0; i < (int)population.size(); i++) {
          printf(AGREY("%02d") " " AWHITE("%d") " moves. Eval " ABLUE("%.5f") "\n",
                 i, (int)population[i].movie.size(),
                 population[i].eval);
        }
      }
    }

    printf("Waiting for solve threads to exit.\n");
    for (std::thread &t : workers) t.join();
    workers.clear();
  }

  // Not owned.
  MinusDB *db = nullptr;
  const LevelId level_id = 0;
  std::vector<uint8_t> start_state;
  std::unique_ptr<Evaluator> evaluator;
};

static constexpr double SOLVE_TIME = 3600.0 * 2.0;

static void Solve() {
  MinusDB db;
  ArcFour rc(StringPrintf("solve.%lld", time(nullptr)));
  std::unordered_set<LevelId> done = db.GetDone();
  CHECK(done.size() <= 65536);
  std::vector<LevelId> not_done;
  not_done.reserve(65536 - done.size());
  for (int level = 0; level < 65536; level++) {
    if (!done.contains(level)) not_done.push_back(level);
  }

  // In a random order.
  Shuffle(&rc, &not_done);

  printf("%d done. %d remain\n", (int)done.size(), (int)not_done.size());

  for (LevelId level : not_done) {
    auto [major, minor] = UnpackLevel(level);
    Solver solver(&db, major, minor, SOLVE_TIME, 8);
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  Solve();

  return 0;
}
