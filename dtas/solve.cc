
// Solves Mario Bros.; kinda like playfun but with as much
// game-specific logic as desired.

#include <algorithm>
#include <chrono>
#include <cmath>
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

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "image.h"
#include "periodically.h"
#include "randutil.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

#include "base/stringprintf.h"
#include "base/logging.h"

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"

#include "mario.h"
#include "mario-util.h"
#include "minus.h"

using uint8 = uint8_t;

static constexpr const char *ROMFILE = "mario.nes";

DECLARE_COUNTERS(emu_steps_total,
                 emu_steps_attempt,
                 levels_attempted,
                 futures_stuck,
                 levels_solved, u1_, u2_, u3_);

// We're not trying to find a particularly short solution, just a solution.

static std::string FormatNum(uint64_t n) {
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

    const bool next_level =
      emu->ReadRAM(WORLD_MAJOR) > world_major ||
      emu->ReadRAM(WORLD_MINOR) > world_minor;

    const uint8_t oper_mode = emu->ReadRAM(OPER_MODE);
    const uint8_t oper_task = emu->ReadRAM(OPER_MODE_TASK);

    // XXX Check this: It might be triggered when going into
    // a pipe like before world 1-2?

    // We could also count tasks 0, 1, as these are normal
    // speedrun rules?
    const bool princess =
      oper_mode == 2 &&
      (oper_task == 3 || oper_task == 4);

    const uint8_t subroutine =
      emu->ReadRAM(GAME_ENGINE_SUBROUTINE);
    const bool flagpole =
      // slide down flagpole
      subroutine == 0x04 ||
      // walk to exit
      subroutine == 0x05;

    return next_level || princess || flagpole;
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

  // Get global x,y coordinates of the player.
  // For y, 256-512 is on-screen.
  static std::pair<uint16_t, uint16_t> GetXY(const Emulator *emu) {
    uint8_t xhi = emu->ReadRAM(PLAYER_X_HI);
    uint8_t xlo = emu->ReadRAM(PLAYER_X_LO);

    uint16_t x = (uint16_t(xhi) << 8) | xlo;

    // 0 if above screen, 1 if on screen, 2+ if below
    uint8_t yscreen = emu->ReadRAM(PLAYER_Y_SCREEN);
    uint8_t ypos = emu->ReadRAM(PLAYER_Y);

    uint16_t y = (uint16_t(yscreen) << 8) | ypos;

    return std::make_pair(x, y);
  }

  double Eval(const Emulator *emu) const {
    const auto &[x, y] = GetXY(emu);
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
    // 256-512 is on-screen.
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

  enum class Outcome {
    RUNNING,
    TIMEOUT,
    SUCCESS,
  };

  Outcome GetOutcome() {
    std::unique_lock<std::mutex> ml(outcome_m);
    return outcome;
  }

  void SetOutcome(Outcome out) {
    std::unique_lock<std::mutex> ml(outcome_m);
    outcome = out;
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

  static std::vector<uint8_t> MoveGen(const Emulator &emu,
                                      ArcFour *rc, int len) {
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
      if (GetOutcome() != Outcome::RUNNING) return;

      State start = SamplePopulation(&rc);

      // One second of gameplay.
      static constexpr int MOVES_PER_FUTURE = 60;
      static constexpr int NUM_FUTURES = 12;
      std::vector<Future> futures;
      futures.reserve(NUM_FUTURES);
      for (int i = 0; i < NUM_FUTURES; i++) {
        futures.push_back(
            Future{.moves = MoveGen(*emu, &rc, MOVES_PER_FUTURE)});
      }

      // Execute each and get stats.
      int64_t steps_run = 0;
      for (Future &future : futures) {
        emu->LoadUncompressed(start.save);
        for (const uint8_t b : future.moves) {
          emu->Step(b, 0);
        }
        steps_run += future.moves.size();

        future.save = emu->SaveUncompressed();
        future.eval = evaluator->Eval(emu.get());
        future.stuck = evaluator->Stuck(emu.get());
        future.done = evaluator->Succeeded(emu.get());
        // (could return immediately if one of them wins!)
      }
      emu_steps_total += steps_run;
      emu_steps_attempt += steps_run;

      futures_stuck +=
        std::erase_if(futures,
                      [](const Future &f) {
                        return f.stuck;
                      });

      // If any of these end up successful, we are done.
      for (const Future &f : futures) {
        if (f.done) {
          SetOutcome(Outcome::SUCCESS);
          std::vector<uint8_t> full_sol = start.movie;
          for (uint8_t b : f.moves) full_sol.push_back(b);
          db->AddSolution(level_id, full_sol, MinusDB::METHOD_SOLVE);
          printf("Solved %s: %s\n",
                 ColorLevel(level_id).c_str(),
                 SimpleFM7::EncodeOneLine(full_sol).c_str());
          levels_solved++;
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
    emu_steps_attempt.Reset();

    solver_emu.reset(Emulator::Create(ROMFILE));
    CHECK(solver_emu.get() != nullptr) << ROMFILE;
    MarioUtil::WarpTo(solver_emu.get(), major, minor, 0);
    start_state = solver_emu->SaveUncompressed();

    evaluator.reset(new Evaluator(solver_emu.get()));

    levels_attempted++;

    CHECK(!evaluator->Stuck(solver_emu.get()));

    {
      State empty;
      empty.movie = {};
      empty.save = solver_emu->SaveUncompressed();
      empty.eval = evaluator->Eval(solver_emu.get());
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
    // Every ten minutes, write images.
    Periodically images_per(10.0 * 60.0);
    // Not useful to write immediately, though.
    images_per.SetPeriodOnce(60.0);
    int image_counter = 0;
    for (;;) {
      if (GetOutcome() != Outcome::RUNNING) {
        break;
      } else if (solve_timer.Seconds() > solve_time) {
        printf("Solver on %s ran out of time (%s).\n",
               ColorLevel(level_id).c_str(),
               ANSI::Time(solve_timer.Seconds()).c_str());
        SetOutcome(Outcome::TIMEOUT);
        break;
      }

      {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(1s);
      }

      if (status_per.ShouldRun()) {
        MutexLock ml(&pop_m);
        uint64_t total_steps = emu_steps_total.Read();
        uint64_t attempt_steps = emu_steps_attempt.Read();
        double sps = attempt_steps / elapsed.Seconds();
        printf("%lld attempted. %s steps (%lld here; %.1f/sec). "
               "%lld stuck futures.\n",
               levels_attempted.Read(),
               FormatNum(total_steps).c_str(),
               attempt_steps, sps,
               futures_stuck.Read());
        printf("Running on %s. Population size %d. Elapsed %s/%s.\n",
               ColorLevel(level_id).c_str(),
               (int)population.size(),
               ANSI::Time(solve_timer.Seconds()).c_str(),
               ANSI::Time(solve_time).c_str());
        for (int i = 0; i < (int)population.size(); i++) {
          printf(AGREY("%02d") " " AWHITE("%d") " moves. "
                 "Eval " ABLUE("%.5f") "\n",
                 i, (int)population[i].movie.size(),
                 population[i].eval);
        }
      }

      images_per.RunIf([&]() {
          printf("Save images...\n");
          image_counter++;
          WriteImage(image_counter);
        });
    }

    printf("Waiting for solve threads to exit.\n");
    for (std::thread &t : workers) t.join();
    workers.clear();

    MutexLock mlo(&outcome_m);
    if (outcome != Outcome::SUCCESS) {
      MutexLock ml(&pop_m);
      std::sort(population.begin(), population.end(),
                [](const auto &a, const auto &b) {
                  return a.eval > b.eval;
                });
      if (!population.empty()) {
        db->AddPartial(level_id, population[0].movie);
        printf("Saved one best solution: %d moves, %.3f eval\n",
               (int)population[0].movie.size(), population[0].eval);
      }
    }

  }

  // From driver thread.
  void WriteImage(int counter) {
    std::vector<State> pop;
    {
      MutexLock ml(&pop_m);
      pop = population;
    }

    const auto &[major, minor] = UnpackLevel(level_id);

    constexpr int MARGIN_TOP = 32;
    constexpr int ACROSS = 10;
    constexpr int DOWN = 5;
    constexpr int BOX_W = 260;
    constexpr int BOX_H = 260;
    ImageRGBA img(ACROSS * BOX_W, MARGIN_TOP + DOWN * BOX_H);
    ImageRGBA text(ACROSS * BOX_W, MARGIN_TOP + DOWN * BOX_H);
    img.Clear32(0x000022FF);
    text.Clear32(0x00000000);

    double best_eval = -999999.9;
    for (const State &state : pop) {
      best_eval = std::max(state.eval, best_eval);
    }

    text.BlendText2x32(1, 1, 0xFFFFFFFF,
                       StringPrintf("Solving %d-%d. Best eval: %.4f    "
                                    "Elapsed: %d sec",
                                    major, minor, best_eval,
                                    (int)elapsed.Seconds()).c_str());

    for (int i = 0; i < (int)pop.size(); i++) {
      const int sy = i / ACROSS;
      const int sx = i % ACROSS;
      const State &state = pop[i];
      solver_emu->LoadUncompressed(state.save);

      const auto &[playerx, playery] =
        Evaluator::GetXY(solver_emu.get());
      // For screenshot, we need to make one step.
      solver_emu->StepFull(0, 0);
      emu_steps_total++;
      emu_steps_attempt++;
      ImageRGBA screen = MarioUtil::Screenshot(solver_emu.get());

      const int ix = sx * BOX_W;
      const int iy = MARGIN_TOP + sy * BOX_H;
      img.BlendImage(ix, iy, screen);

      text.BlendText32(ix + 2, iy + 240,
                       0xFF00FFCC,
                       StringPrintf("^ %d,%d", playerx, playery));
      text.BlendText32(ix + 2, iy + 250,
                       0x00FFFFCC,
                       StringPrintf("%d moves, eval %.4f",
                                    (int)state.movie.size(),
                                    state.eval));
    }

    img.BlendImage(0, 0, text);

    std::string filename =
      StringPrintf("solve%d-%d_%d.png", major, minor, counter);
    img.Save(filename);
    printf("Wrote %s.\n", filename.c_str());
  }

  Timer elapsed;
  // Not owned.
  MinusDB *db = nullptr;
  const LevelId level_id = 0;
  std::vector<uint8_t> start_state;
  std::unique_ptr<Evaluator> evaluator;

  // Each thread has its own emulator, but the driver thread
  // also has one for initialization, image generation, etc.
  std::unique_ptr<Emulator> solver_emu;

  std::mutex outcome_m;
  Outcome outcome = Outcome::RUNNING;

  std::mutex pop_m;
  std::vector<State> population;
};

static constexpr double SOLVE_TIME = 900.0; // 3600.0 * 2.0;

static std::vector<LevelId> GetTodo(MinusDB *db, ArcFour *rc) {
  std::unordered_set<LevelId> done = db->GetDone();
  CHECK(done.size() <= 65536);
  std::vector<LevelId> not_done;
  not_done.reserve(65536 - done.size());

  // Some levels that are easily solvable, for runs from scratch.
  std::unordered_set<LevelId> do_first = {
    PackLevel(187, 106),
    // Princess end condition (underwater)
    PackLevel(0x0B, 0x00),
    // The first level is easy
    PackLevel(0, 0),
  };

  #if 0
  // Also, the whole main game.
  for (int maj = 0; maj < 8; maj++) {
    for (int min = 0; min < 4; min++) {
      do_first.insert(PackLevel(maj, min));
    }
  }
  #endif

  // Also, the whole left column.
  for (int maj = 0; maj < 256; maj++) {
    do_first.insert(PackLevel(maj, 0));
  }

  for (LevelId level : do_first) {
    if (!done.contains(level)) not_done.push_back(level);
  }

  std::vector<LevelId> rest;
  for (int level = 0; level < 65536; level++) {
    if (!done.contains(level) && !do_first.contains(level)) {
      rest.push_back(level);
    }
  }
  // In a random order.
  Shuffle(rc, &rest);
  for (LevelId level : rest) not_done.push_back(level);

  printf("%d done. %d remain\n", (int)done.size(), (int)not_done.size());

  return not_done;
}

static void Solve() {
  MinusDB db;
  ArcFour rc(StringPrintf("solve.%lld", time(nullptr)));

  std::vector<LevelId> todo = GetTodo(&db, &rc);

  for (LevelId level : todo) {
    auto [major, minor] = UnpackLevel(level);
    Solver solver(&db, major, minor, SOLVE_TIME, 8);
  }
}

static void Cross() {
  MinusDB db;
  ArcFour rc(StringPrintf("cross.%lld", time(nullptr)));

  std::vector<LevelId> todo = GetTodo(&db, &rc);
  // TODO: Should dedupe these; now that I've used this
  // strategy there are loads of duplicates in there.
  std::vector<std::pair<LevelId, std::vector<uint8_t>>> sols =
    db.GetSolutions();

  Timer elapsed;
  Periodically status_per(1.0);
  ParallelApp(
      todo,
      [&db, &todo, &sols, &elapsed, &status_per](LevelId level) {
        const auto &[major, minor] = UnpackLevel(level);
        // PERF if we didn't have to keep creating emulator
        std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
        CHECK(emu.get() != nullptr) << ROMFILE;
        MarioUtil::WarpTo(emu.get(), major, minor, 0);
        std::vector<uint8_t> start_state = emu->SaveUncompressed();
        Evaluator eval(emu.get());

        levels_attempted++;

        for (const auto &[source_level, movie] : sols) {
          emu->LoadUncompressed(start_state);
          for (int i = 0; i < (int)movie.size(); i++) {
            emu->Step(movie[i], 0);
            emu_steps_total++;
            if (eval.Succeeded(emu.get())) {
              std::vector<uint8_t> prefix = movie;
              prefix.resize(i + 1);
              db.AddSolution(level, prefix, MinusDB::METHOD_CROSS);
              printf("Solved %s (via %s): %s\n",
                     ColorLevel(level).c_str(),
                     ColorLevel(source_level).c_str(),
                     SimpleFM7::EncodeOneLine(prefix).c_str());
              levels_solved++;
              return;
            }
          }

          status_per.RunIf([&]() {
              printf("Attempted %d/%d, solving %d. Elapsed %s\n",
                     (int)levels_attempted.Read(),
                     (int)todo.size(),
                     (int)levels_solved.Read(),
                     ANSI::Time(elapsed.Seconds()).c_str());
            });
        }

      },
      8);

  printf("Finished cross in %s. New sols: " AGREEN("%d") "\n",
         ANSI::Time(elapsed.Seconds()).c_str(),
         (int)levels_solved.Read());
}

int main(int argc, char **argv) {
  ANSI::Init();

  // Solve();
  Cross();

  return 0;
}
