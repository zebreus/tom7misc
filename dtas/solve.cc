
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
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "auto-histo.h"
#include "image.h"
#include "periodically.h"
#include "randutil.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"
#include "hashing.h"
#include "status-bar.h"

#include "base/stringprintf.h"
#include "base/logging.h"

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"

#include "emulator-pool.h"
#include "mario-util.h"
#include "mario.h"
#include "minus.h"

using uint8 = uint8_t;
using Pos = MarioUtil::Pos;

static constexpr const char *ROMFILE = "mario.nes";
static constexpr double SOLVE_TIME = 600.0; // 3600.0 * 2.0;
static constexpr double MAZE_TIME = 1200.0; // 3600.0 * 2.0;

#define VIZ_MAP 1

DECLARE_COUNTERS(emu_steps_total,
                 emu_steps_attempt,
                 levels_attempted,
                 futures_stuck,
                 levels_solved,
                 levels_skipped,
                 u2_, u3_);

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

    const bool next_level =
      emu->ReadRAM(WORLD_MAJOR) > world_major ||
      emu->ReadRAM(WORLD_MINOR) > world_minor;

    const uint8_t oper_mode = emu->ReadRAM(OPER_MODE);
    const uint8_t oper_task = emu->ReadRAM(OPER_MODE_TASK);

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

  double Eval(const Emulator *emu) const {
    const Pos pos = MarioUtil::GetPos(emu);
    // double xfrac = x / (double)0xFFFF;

    // Decimal part of score is x position (unless penalized);
    // fractional part is heuristics.
    double score = pos.x;

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
    if (pos.y > 511) {
      // Very bad to be off screen downward, as we will
      // definitely die.
      return -200000.0 + score;
    } else if (pos.y > 256 + 176) {
      // When on screen, we're uncomfortable if we're below 176,
      // which is below the bottom two rows of bricks.

      int depth = pos.y - (256 + 176);
      return -100000.0 - depth + score;
    } else {
      // Otherwise we're not falling off the screen. But give
      // some bonus when we're higher up (lower y coordinate).
      int height = -(pos.y - (256 + 176));

      return score + (height * 0.05);
    }
  }

  uint8_t world_major = 0, world_minor = 0;
  uint8_t start_lives = 0;
};

struct MazeSolver {

  struct CellData {
    // Inputs starting after the WarpTo.
    std::vector<uint8_t> movie;
    // The save state.
    std::vector<uint8_t> save;
    double eval = 0.0;
    int expansions = 0;
  };

  struct CellId {
    CellId(int x, int y) : x(x), y(y) {}
    int x;
    int y;

    inline bool operator==(const CellId &other) const {
      return x == other.x && y == other.y;
    }
    inline bool operator!=(const CellId &other) const {
      return !(*this == other);
    }
  };

  struct HashCell {
    inline size_t operator ()(const CellId &c) const {
      return c.x * 31337 ^ c.y;
    }
  };

  std::mutex mutex;
  // Maps from cells to a movie that reaches that cell.
  std::unordered_map<CellId, CellData, HashCell> cells;
  int64_t cells_expanded = 0;

  static inline CellId Cell(const Pos &pos) {
    return CellId((pos.x + 8) / 16, (pos.y + 8) / 16);
  }

  enum class Outcome {
    RUNNING,
    TIMEOUT,
    SUCCESS,
  };

  std::mutex outcome_m;
  Outcome outcome;

  Outcome GetOutcome() {
    std::unique_lock<std::mutex> ml(outcome_m);
    return outcome;
  }

  void SetOutcome(Outcome out) {
    std::unique_lock<std::mutex> ml(outcome_m);
    outcome = out;
  }

  static inline bool OnScreen(const CellId &cell) {
    return cell.x >= 0 &&
      cell.y >= (256 / 16) &&
      cell.y < (512 / 16);
  }

  std::optional<std::pair<CellId, CellId>> GetCell() {
    // Find a good cell to expand.

    MutexLock ml(&mutex);
    CHECK(!cells.empty());
    // status.Printf("There are %d cells.\n", (int)cells.size());
    std::optional<std::pair<CellId, CellId>> best;
    double best_score = -9999.0;
    for (const auto &[src, data] : cells) {
      // A good cell has an adjacent (or nearby) cell that
      // is not yet solved. It also hasn't been expanded
      // too many times.

      double score = 0.0;
      score -= data.expansions;
      score += src.x * 5;

      std::optional<CellId> best_dst;

      for (int dy = -3; dy < 3; dy++) {
        for (int dx = -3; dx < 3; dx++) {
          if (dy != 0 || dy != 0) {
            CellId dst(src.x + dx, src.y + dy);
            /*
            status.Printf("Dst %d,%d. OnScreen: %s\n",
                          dst.x, dst.y, OnScreen(dst) ? "Y" : "N");
            */
            if (OnScreen(dst) && !cells.contains(dst)) {
              score += 1.0;
              if (!best_dst.has_value() || best_dst.value().x < dst.x) {
                best_dst = {dst};
              }
            }
          }
        }
      }

      if (best_dst.has_value() &&
          (!best.has_value() || score > best_score)) {
        best = {std::make_pair(src, best_dst.value())};
        best_score = score;
      }
    }

    return best;
  }

  // Euclidean distance, in cells.
  static double CellDist(const CellId &a, const CellId &b) {
    int dx = a.x - b.x, dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
  }

  void WorkThread(uint64_t seed) {
    ArcFour rc(seed);
    std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));


    for (;;) {
      if (GetOutcome() != Outcome::RUNNING) return;

      // Repeatedly: Pick a cell that hasn't been expanded
      // much. Pick a nearby destination cell that is empty.

      const auto obest = GetCell();
      if (!obest.has_value()) {
        status.Printf(ARED("No cells remain??") "\n");
        return;
      }
      const auto &[src, dst] = obest.value();

      // Load the source state. Copy the src movie so that
      // we don't need to worry about modifications to the
      // cells.
      std::vector<uint8_t> src_movie;
      {
        MutexLock ml(&mutex);
        auto it = cells.find(src);
        CHECK(it != cells.end());
        src_movie = it->second.movie;
        emu->LoadUncompressed(it->second.save);
        it->second.expansions++;
        cells_expanded++;
      }

      CHECK(src == Cell(MarioUtil::GetPos(emu.get()))) << "The "
        "save state was not in the expected cell!";

      // Generate moves that try to get us towards the
      // cell. As we execute moves, populate cells if new
      // (or better).
      int depth_left =
        std::clamp((int)std::ceil(16.0 * 2.0 * CellDist(src, dst)),
                   1, 120);

      CellId cur_cell = src;

      std::vector<uint8_t> edge_movie;
      edge_movie.reserve(depth_left);

      int move_time = 0;
      uint8_t move_input = 0;

      while (depth_left--) {

        if (move_time == 0) {
          // Choose a new move and duration.

          move_time = 1 + RandTo(&rc, 60);

          move_input = 0;

          const uint8_t left_p = dst.x < cur_cell.x ? 220 : 32;
          if (rc.Byte() < left_p)
            move_input |= INPUT_L;

          const uint8_t right_p = dst.x > cur_cell.x ? 220 : 32;
          if (rc.Byte() < right_p) {
            move_input |= INPUT_R;
          }

          // Generally try to jump (or extend the jump) if we want to
          // move up. But it's often helpful to hold A to extend jumps
          // or avoid enemies anyway.
          const uint8_t jump_p = dst.y < cur_cell.y ? 200 : 128;
          if (rc.Byte() < jump_p) {
            move_input |= INPUT_A;
          }

          // Usually helpful to hold B.
          if (rc.Byte() < 200)
            move_input |= INPUT_B;

          // These are generally harmless, but
          // they allow us to go down pipes and
          // up vines.
          if (dst.y > cur_cell.y) {
            move_input |= INPUT_D;
          }
          if (dst.y < cur_cell.y) {
            move_input |= INPUT_U;
          }

        } else {
          move_time--;
        }

        emu->Step(move_input, 0);
        edge_movie.push_back(move_input);

        if (evaluator->Succeeded(emu.get())) {
          if (GetOutcome() == Outcome::SUCCESS) {
            printf(AYELLOW("Simultaneously solved")
                   " %s! Ignored.\n", ColorLevel(level_id).c_str());
            return;
          }

          SetOutcome(Outcome::SUCCESS);

          std::vector<uint8_t> full_sol = src_movie;
          for (uint8_t b : edge_movie) full_sol.push_back(b);
          db->AddSolution(level_id, full_sol, MinusDB::METHOD_MAZE);
          printf(AGREEN("Solved") " %s: " AGREY("%s") "\n",
                 ColorLevel(level_id).c_str(),
                 SimpleFM7::EncodeOneLine(full_sol).c_str());
          levels_solved++;
          return;
        }

        Pos pos = MarioUtil::GetPos(emu.get());
        CellId new_cell = Cell(pos);
        if (new_cell != cur_cell) {
          cur_cell = new_cell;
          bool is_new = false;
          {
            MutexLock ml(&mutex);
            auto it = cells.find(cur_cell);
            is_new = it == cells.end();
          }

          if (is_new) {
            // PERF: We only ever add to this solution, so we
            // could just push directly onto it and copy it here?
            CellData data;
            data.movie = src_movie;
            for (uint8_t b : edge_movie) data.movie.push_back(b);
            data.save = emu->SaveUncompressed();
            data.eval = evaluator->Eval(emu.get());

            {
              MutexLock ml(&mutex);
              cells[cur_cell] = std::move(data);
            }

            // We made some progress so we could just break here.
            // But we continue since we may be "on a roll" :)
          }
        }

        if (cur_cell == dst) {
          // (We would have inserted this above unless someone else
          //  got to it in the meantime.)
          break;
        }
      }

      emu_steps_total += edge_movie.size();
      emu_steps_attempt += edge_movie.size();
    }
  }

  static constexpr int STATUS_LINES = 2;
  StatusBar status;
  // Not owned.
  MinusDB *db = nullptr;
  LevelId level_id = 0;
  std::unique_ptr<Emulator> solver_emu;
  std::vector<uint8_t> start_state;
  std::unique_ptr<Evaluator> evaluator;

  MazeSolver(MinusDB *db, uint8_t major, uint8_t minor,
             double solve_time = 3600.0,
             int num_threads = 8) :
    status(STATUS_LINES),
    db(db),
    level_id((uint16_t(major) << 8) | minor) {

    SetOutcome(Outcome::RUNNING);

    emu_steps_attempt.Reset();

    solver_emu.reset(Emulator::Create(ROMFILE));
    CHECK(solver_emu.get() != nullptr) << ROMFILE;
    MarioUtil::WarpTo(solver_emu.get(), major, minor, 0);

    // Wait the typical amount of time before reading
    // the start position.
    std::vector<uint8_t> preamble;
    for (int i = 0; i < 166; i++) {
      preamble.push_back(0);
      solver_emu->Step(0, 0);
    }

    start_state = solver_emu->SaveUncompressed();

    evaluator.reset(new Evaluator(solver_emu.get()));

    levels_attempted++;

    // Add initial cell.
    Pos start_pos = MarioUtil::GetPos(solver_emu.get());
    CellId start_cell = Cell(start_pos);

    {
      CellData cell;
      cell.save = start_state;
      cell.movie = preamble;
      cell.eval = evaluator->Eval(solver_emu.get());
      cells[start_cell] = std::move(cell);
      status.Printf(
          "Start pos: %d,%d = cell: %d,%d\n",
          start_pos.x, start_pos.y,
          start_cell.x, start_cell.y);
    }

    std::vector<std::thread> workers;
    for (int i = 0; i < num_threads; i++) {
      workers.emplace_back([this, i]() {
          return this->WorkThread(i);
        });
    }

    Timer solve_timer;
    Periodically status_per(5.0);
    // Every 20 minutes, write images.
    Periodically images_per(20.0 * 60.0);
    // Not useful to write immediately, though.
    images_per.SetPeriodOnce(60.0);
    int image_counter = 0;
    for (;;) {
      if (GetOutcome() != Outcome::RUNNING) {
        break;
      } else if (solve_timer.Seconds() > solve_time) {
        status.Printf("Solver on %s ran out of time with " AYELLOW("%d")
                      "cells done (%s).\n",
                      ColorLevel(level_id).c_str(),
                      (int)cells.size(),
                      ANSI::Time(solve_timer.Seconds()).c_str());
        SetOutcome(Outcome::TIMEOUT);
        break;
      }

      {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(1s);
      }

      if (status_per.ShouldRun()) {
        uint64_t total_steps = emu_steps_total.Read();
        uint64_t attempt_steps = emu_steps_attempt.Read();
        double sps = attempt_steps / solve_timer.Seconds();

        MutexLock ml(&mutex);
        int num_cells = (int)cells.size();

        std::string lines;

        StringAppendF(
            &lines,
            "%lld attempted. %s steps (%lld here; %.1f/sec). "
            "Solved " AGREEN("%d") "\n",
            levels_attempted.Read(),
            MarioUtil::FormatNum(total_steps).c_str(),
            attempt_steps, sps,
            (int)levels_solved.Read());
        StringAppendF(
            &lines,
            "Maze solver on %s. " AYELLOW("%d") " cells from "
            "%s expansions in "
            " %s/%s.\n",
            ColorLevel(level_id).c_str(),
            num_cells,
            MarioUtil::FormatNum(cells_expanded).c_str(),
            ANSI::Time(solve_timer.Seconds()).c_str(),
            ANSI::Time(solve_time).c_str());

        status.EmitStatus(lines);
      }

      images_per.RunIf([&]() {
          status.Printf("Save images...\n");
          image_counter++;
          WriteImage(image_counter);
        });
    }

    status.Printf("Waiting for solve threads to exit.\n");
    for (std::thread &t : workers) t.join();
    workers.clear();

    status.Printf("Save final images:\n");
    image_counter++;
    WriteImage(image_counter);

    // Is there something we could save here?
  }

  void WriteImage(int counter) {
    // Find rightmost cell so we can make the map.
    std::vector<uint8_t> movie;
    std::unordered_map<CellId, int, HashCell> done;
    {
      MutexLock ml(&mutex);
      std::optional<CellId> r;
      for (const auto &[cellid, data] : cells) {
        done[cellid] = data.expansions;
        if (!r.has_value() || cellid.x > r.value().x) {
          r = {cellid};
        }
      }
      if (!r.has_value()) {
        status.Printf(ARED("No cells!?") "\n");
        return;
      }
      movie = cells[r.value()].movie;
    }

    solver_emu->LoadUncompressed(start_state);
    ImageRGBA map = MarioUtil::MakeMap(solver_emu.get(), movie);

    // Mark complete cells.
    for (const auto &[cellid, expansions] : done) {
      int cx = cellid.x * 16;
      int cy = (cellid.y - (256 / 16)) * 16;
      map.BlendRect32(cx, cy,
                      16, 16, 0xFF000044);
      map.BlendText32(cx, cy, 0xFF000066,
                      StringPrintf("%d", expansions));
    }

    const auto &[major, minor] = UnpackLevel(level_id);
    map.BlendText32(1, 1, 0xFFFFFFFF,
                    StringPrintf("Maze (%02x-%02x): %d cells",
                                 major, minor, (int)done.size()));

    std::string filename = StringPrintf("maze-%02x-%02x-%d.png",
                                        major, minor, counter);
    map.Save(filename);
    status.Printf("Wrote " ABLUE("%s") "\n", filename.c_str());
  }

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

  static constexpr int MAX_POPULATION = 40;

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
        if (future.stuck) futures_stuck++;
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
          // This seems unlikely at first, but it happens in practice.
          // Two workers might simultaneously pick states that are
          // right before the exit, for example.
          if (GetOutcome() == Outcome::SUCCESS) {
            printf(AYELLOW("Simultaneously solved")
                   " %s! Ignored.\n", ColorLevel(level_id).c_str());
            return;
          }
          SetOutcome(Outcome::SUCCESS);
          std::vector<uint8_t> full_sol = start.movie;
          for (uint8_t b : f.moves) full_sol.push_back(b);
          db->AddSolution(level_id, full_sol, MinusDB::METHOD_SOLVE);
          printf(AGREEN("Solved") " %s: " AGREY("%s") "\n",
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

  Solver(MinusDB *db, uint8_t major, uint8_t minor,
         double solve_time = 3600.0,
         int num_threads = 8) :
    status(STATUS_LINES),
    db(db),
    level_id((uint16_t(major) << 8) | minor) {

    SetOutcome(Outcome::RUNNING);

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
    Periodically status_per(5.0);
    // Every 20 minutes, write images.
    Periodically images_per(20.0 * 60.0);
    // Not useful to write immediately, though.
    images_per.SetPeriodOnce(60.0);
    int image_counter = 0;
    for (;;) {
      if (GetOutcome() != Outcome::RUNNING) {
        break;
      } else if (solve_timer.Seconds() > solve_time) {
        status.Printf("Maze solver on %s ran out of time (%s).\n",
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

        std::string lines;

        StringAppendF(
            &lines,
            "%lld attempted. %s steps (%lld here; %.1f/sec). "
            "Solved " AGREEN("%d") "\n",
            levels_attempted.Read(),
            MarioUtil::FormatNum(total_steps).c_str(),
            attempt_steps, sps,
            (int)levels_solved.Read());
        StringAppendF(
            &lines,
            "Running on %s. Pop size %d. %d stuck. "
            "Elapsed %s/%s.\n",
            ColorLevel(level_id).c_str(),
            (int)population.size(),
            (int)futures_stuck.Read(),
            ANSI::Time(solve_timer.Seconds()).c_str(),
            ANSI::Time(solve_time).c_str());

        AutoHisto eval;
        AutoHisto moves;
        for (const State &state : population) {
          eval.Observe(state.eval);
          moves.Observe(state.movie.size());
          /*
            printf(AGREY("%02d") " " AWHITE("%d") " moves. "
            "Eval " ABLUE("%.5f") "\n",
            i, (int)population[i].movie.size(),
            population[i].eval);
          */
        }

        StringAppendF(&lines,
                      AWHITE("Eval") " %s:\n"
                      "%s\n",
                      eval.IsIntegral() ? "(integral)" : "(float)",
                      eval.SimpleHorizANSI(12).c_str());
        StringAppendF(&lines,
                      AWHITE("Moves") " %s:\n"
                      "%s\n",
                      moves.IsIntegral() ? "(integral)" : "(float)",
                      moves.SimpleHorizANSI(10).c_str());

        status.EmitStatus(lines);
      }

      images_per.RunIf([&]() {
          status.Printf("Save images...\n");
          image_counter++;
          WriteImage(image_counter);
        });
    }

    status.Printf("Waiting for solve threads to exit.\n");
    for (std::thread &t : workers) t.join();
    workers.clear();

    status.Printf("Save final images:\n");
    image_counter++;
    WriteImage(image_counter);

    MutexLock mlo(&outcome_m);
    if (outcome != Outcome::SUCCESS) {
      MutexLock ml(&pop_m);
      std::sort(population.begin(), population.end(),
                [](const auto &a, const auto &b) {
                  return a.eval > b.eval;
                });
      if (!population.empty()) {
        db->AddPartial(level_id, population[0].movie);
        status.Printf("Saved one best solution: %d moves, %.3f eval\n",
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

    if (pop.empty()) {
      status.Printf("No population! So no images.\n");
      return;
    }

    const auto &[major, minor] = UnpackLevel(level_id);

    std::string filename =
      StringPrintf("solve%02x-%02x_%d.png", major, minor, counter);

    double best_eval = pop[0].eval;
    int best_idx = 0;
    for (int idx = 0; idx < (int)pop.size(); idx++) {
      const State &state = pop[idx];
      if (state.eval > best_eval) {
        best_eval = state.eval;
        best_idx = idx;
      }
    }

    #if VIZ_MAP
    // Generate map for best eval. Even better would be if we
    // took the union of all maps (but this is also more expensive).
    solver_emu->LoadUncompressed(start_state);
    ImageRGBA map =
      MarioUtil::MakeMap(solver_emu.get(), pop[best_idx].movie);

    // PERF: Instead, do this in parallel?
    for (int i = 0; i < (int)pop.size(); i++) {
      solver_emu->LoadUncompressed(start_state);
      std::vector<Pos> path = MarioUtil::GetPath(solver_emu.get(),
                                                 pop[i].movie);

      // Could use rainbows?
      MarioUtil::DrawPath(path, &map, 0xFF000044);
    }

    map.BlendText2x32(1, 1, 0xFFFFFFFF,
                      StringPrintf("Solving %02x-%02x. Best eval: %.4f    "
                                   "Elapsed: %d sec",
                                   major, minor, best_eval,
                                   (int)elapsed.Seconds()).c_str());

    map.Save(filename);
    status.Printf("Wrote %s.\n", filename.c_str());

    #else
    constexpr int MARGIN_TOP = 32;
    constexpr int ACROSS = 10;
    constexpr int DOWN = 5;
    constexpr int BOX_W = 260;
    constexpr int BOX_H = 260;
    ImageRGBA img(ACROSS * BOX_W, MARGIN_TOP + DOWN * BOX_H);
    ImageRGBA text(ACROSS * BOX_W, MARGIN_TOP + DOWN * BOX_H);
    img.Clear32(0x000022FF);
    text.Clear32(0x00000000);

    text.BlendText2x32(1, 1, 0xFFFFFFFF,
                       StringPrintf("Solving %02x-%02x. Best eval: %.4f    "
                                    "Elapsed: %d sec",
                                    major, minor, best_eval,
                                    (int)elapsed.Seconds()).c_str());

    for (int i = 0; i < (int)pop.size(); i++) {
      const int sy = i / ACROSS;
      const int sx = i % ACROSS;
      const State &state = pop[i];
      solver_emu->LoadUncompressed(state.save);

      const Pos pos = MarioUtil::GetPos(solver_emu.get());
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
                       StringPrintf("^ %d,%d", pos.x, pos.y));
      text.BlendText32(ix + 2, iy + 250,
                       0x00FFFFCC,
                       StringPrintf("%d moves, eval %.4f",
                                    (int)state.movie.size(),
                                    state.eval));
    }

    img.BlendImage(0, 0, text);
    std::string filename =
      StringPrintf("solve%02x-%02x_%d.png", major, minor, counter);
    img.Save(filename);
    status.Printf("Wrote %s.\n", filename.c_str());
    #endif
  }

  // two status lines; two histos, each two lines
  static constexpr int STATUS_LINES = 8;
  StatusBar status;

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

static std::vector<LevelId> GetTodo(MinusDB *db, ArcFour *rc) {
  std::unordered_set<LevelId> done = db->GetDone();
  CHECK(done.size() <= 65536);
  std::vector<LevelId> todo;
  todo.reserve(65536 - done.size());

  // Some levels that are easily solvable, for runs from scratch.
  std::unordered_set<LevelId> do_first = {
    PackLevel(187, 106),
    // Princess end condition (underwater)
    PackLevel(0x0B, 0x00),
    // The first level is easy
    PackLevel(0, 0),
  };

  // This requires some backtracking.
  // do_first.insert(PackLevel(0x00, 0x12));

  #if 0
  // Also, the whole main game.
  for (int maj = 0; maj < 8; maj++) {
    // You might think there are four minor levels per
    // major world, but the transition screen at the
    // beginning of e.g. 1-2 is actually its own level!
    for (int min = 0; min < 5; min++) {
      do_first.insert(PackLevel(maj, min));
    }
  }
  #endif

  #if 1
  // Whole second row.
  for (int min = 255; min >= 0; min--) {
    do_first.insert(PackLevel(1, min));
  }
  #endif

  #if 0
  // Also, the whole left column.
  for (int maj = 0; maj < 256; maj++) {
    do_first.insert(PackLevel(maj, 0));
  }
  #endif

  #if 1
  for (int maj = 0; maj < 256; maj++) {
    do_first.insert(PackLevel(maj, 3));
  }
  #endif

  if (!do_first.empty()) {
    printf(AWHITE("Do first:"));
    for (LevelId level : do_first) {
      if (!done.contains(level)) {
        printf(" %s", ColorLevel(level).c_str());
        todo.push_back(level);
      }
    }
    printf("\n");
  }

  std::vector<LevelId> rest;
  for (int level = 0; level < 65536; level++) {
    if (!done.contains(level) && !do_first.contains(level)) {
      rest.push_back(level);
    }
  }
  // In a random order.
  Shuffle(rc, &rest);
  for (LevelId level : rest) todo.push_back(level);

  printf("%d done. %d remain\n", (int)done.size(), (int)todo.size());

  return todo;
}

[[maybe_unused]]
static void Solve() {
  MinusDB db;
  ArcFour rc(StringPrintf("solve.%lld", time(nullptr)));

  std::vector<LevelId> todo = GetTodo(&db, &rc);
  std::unordered_set<LevelId> attempted = db.GetAttempted();

  static constexpr bool ONLY_FRESH = true;

  for (LevelId level : todo) {
    if (ONLY_FRESH && attempted.contains(level)) {
      continue;
    } else if (db.HasSolution(level)) {
      levels_skipped++;
      printf("Level %s was solved in the meantime!\n",
             ColorLevel(level).c_str());
      continue;
    }
    auto [major, minor] = UnpackLevel(level);
    Solver solver(&db, major, minor, SOLVE_TIME, 8);
  }
}

[[maybe_unused]]
static void Maze() {
  MinusDB db;
  ArcFour rc(StringPrintf("maze.%lld", time(nullptr)));

  std::vector<LevelId> todo = GetTodo(&db, &rc);
  std::unordered_set<LevelId> attempted = db.GetAttempted();

  static constexpr bool ONLY_FRESH = false;

  for (LevelId level : todo) {
    if (ONLY_FRESH && attempted.contains(level)) {
      continue;
    } else if (db.HasSolution(level)) {
      levels_skipped++;
      printf("Level %s was solved in the meantime!\n",
             ColorLevel(level).c_str());
      continue;
    }
    auto [major, minor] = UnpackLevel(level);
    MazeSolver maze_solver(&db, major, minor, MAZE_TIME, 8);
  }
}

[[maybe_unused]]
static void Cross() {
  MinusDB db;
  ArcFour rc(StringPrintf("cross.%lld", time(nullptr)));

  StatusBar status(1);
  status.Statusf("Preparing solutions.\n");

  std::vector<LevelId> todo = GetTodo(&db, &rc);

  // Get all the solutions. There are many duplicates (owing for example
  // to this very strategy!) so we deduplicate them.
  std::vector<MinusDB::SolutionRow> all_sols = db.GetSolutions();
  status.Printf("There are " AGREEN("%d") " existing solutions.\n",
                (int)all_sols.size());

  // Index of the earliest instance of that solution.
  std::unordered_map<std::vector<uint8_t>, int,
                     Hashing<std::vector<uint8_t>>> provenance;
  for (int idx = 0; idx < all_sols.size(); idx++){
    const MinusDB::SolutionRow &row = all_sols[idx];
    // Assume it's a duplicate (or actually a prefix) of an original
    // solution, and skip it.
    if (row.method == MinusDB::METHOD_CROSS)
      continue;

    auto it = provenance.find(row.movie);
    if (it == provenance.end()) {
      provenance[row.movie] = idx;
    } else if (row.createdate < all_sols[it->second].createdate) {
      it->second = idx;
    }
  }

  status.Printf("There are " APURPLE("%d") " distinct original sols.\n",
                (int)provenance.size());

  // Now put it in the form we like.
  std::vector<std::pair<int, std::vector<uint8_t>>> sols;
  sols.reserve(provenance.size());
  for (const auto &[m_, idx] : provenance)
    sols.emplace_back(all_sols[idx].level, all_sols[idx].movie);

  EmulatorPool emulator_pool(ROMFILE);
  std::vector<uint8_t> start_state;
  {
    EmulatorPool::Lease emu = emulator_pool.Acquire();
    start_state = emu->SaveUncompressed();
  }

  // These might be big. No need to keep them around.
  provenance.clear();
  all_sols.clear();

  // const int64_t total_work = sols.size() * todo.size();

  Timer elapsed;
  Periodically status_per(1.0);
  ParallelApp(
      todo,
      [&db, &todo, &sols, &elapsed, &status, &status_per,
       &emulator_pool, &start_state](LevelId level) {
        const auto &[major, minor] = UnpackLevel(level);

        if (db.HasSolution(level)) {
          levels_skipped++;
          status.Printf("Level %s was solved in the meantime!\n",
                        ColorLevel(level).c_str());
          return;
        }

        EmulatorPool::Lease emu = emulator_pool.Acquire();
        CHECK(emu.get() != nullptr) << ROMFILE;
        emu->LoadUncompressed(start_state);
        MarioUtil::WarpTo(emu.get(), major, minor, 0);
        std::vector<uint8_t> level_start_state = emu->SaveUncompressed();
        Evaluator eval(emu.get());

        for (const auto &[source_level, movie] : sols) {
          emu->LoadUncompressed(level_start_state);
          for (int i = 0; i < (int)movie.size(); i++) {
            emu->Step(movie[i], 0);
            emu_steps_total++;
            if (eval.Succeeded(emu.get())) {
              std::vector<uint8_t> prefix = movie;
              prefix.resize(i + 1);
              db.AddSolution(level, prefix, MinusDB::METHOD_CROSS);
              status.Printf("Solved %s (via %s): %s\n",
                            ColorLevel(level).c_str(),
                            ColorLevel(source_level).c_str(),
                            SimpleFM7::EncodeOneLine(prefix).c_str());
              levels_attempted++;
              levels_solved++;
              return;
            }
          }

          status_per.RunIf([&]() {
              int64_t attempted = levels_attempted.Read();
              int64_t solved = levels_solved.Read();
              int64_t failed = attempted - solved;
              int64_t skipped = levels_skipped.Read();
              int64_t numer = skipped + attempted;
              const int64_t denom = todo.size();

              const int WIDTH = 65;
              double ff = failed / (double)denom;
              double sf = solved / (double)denom;
              double kf = skipped / (double)denom;
              // TODO: Skipped

              std::string msg =
                StringPrintf(
                    AWHITE("%.1f%% ")
                    AGREEN("%lld") " + "
                    ARED("%lld") " + "
                    AYELLOW("%lld")
                    " / "
                    "%d × %d",
                    (numer * 100.0) / denom,
                    solved, failed, skipped,
                    (int)sols.size(),
                    (int)todo.size());

              // eta
              double seconds = elapsed.Seconds();
              double spe = numer > 0 ? seconds / numer : 1.0;
              double remaining_sec = (denom - numer) * spe;
              std::string eta = ANSI::Time(remaining_sec);

              int fd = (int)std::round(ff * WIDTH);
              int sd = std::clamp((int)std::round(sf * WIDTH), 0, WIDTH - fd);
              int kd = std::clamp((int)std::round(kf * WIDTH), 0,
                                  WIDTH - fd - sd);
              int rd = WIDTH - fd - sd - kd;
              auto bgcolor =
                ANSI::Rasterize({
                      {0x6e1200FF, fd},
                      {0x1e660fFF, sd},
                      {0x857f1bFF, kd},
                      {0x111111FF, rd}},
                  WIDTH);

              const auto &[text, fgs, bgs_] = ANSI::Decompose(msg);
              std::string bar = ANSI::Composite(text, fgs, bgcolor);

              status.Statusf(AWHITE("[") "%s" AWHITE("]") " %s\n",
                             bar.c_str(),
                             eta.c_str());
            });
        }

        levels_attempted++;
      },
      12);

  printf("Finished cross in %s. New sols: " AGREEN("%d") "\n",
         ANSI::Time(elapsed.Seconds()).c_str(),
         (int)levels_solved.Read());
}

static void Manual(const std::string &fm2file) {
  MinusDB db;

  std::vector<uint8_t> movie = SimpleFM2::ReadInputs(fm2file);
  // But filter out start from the beginning.
  for (int i = 0; i < (int)movie.size() && i < 60; i++) {
    movie[i] &= ~INPUT_T;
  }

  uint8_t major = 0x14, minor = 0xC3;
  const LevelId level = PackLevel(major, minor);
  if (db.HasSolution(level)) {
    printf(AORANGE("Already solved: ") "%s" "\n",
           ColorLevel(level).c_str());
    return;
  }

  std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
  CHECK(emu.get() != nullptr);
  MarioUtil::WarpTo(emu.get(), major, minor, 0);
  std::vector<uint8_t> level_start_state = emu->SaveUncompressed();
  Evaluator eval(emu.get());

  static constexpr int MAX_PADDING = 48;
  Timer timer;
  Periodically status_per(1.0);
  StatusBar status(1);
  for (int p = -MAX_PADDING; p < MAX_PADDING; p++) {
    emu->LoadUncompressed(level_start_state);

    if (p > 0) {
      for (int i = 0; i < p; i++) emu->Step(0, 0);
    }

    int start_idx = 0;
    if (p < 0) start_idx = -p;

    for (int idx = start_idx; idx < (int)movie.size(); idx++) {
      if (idx == 340) {
        MarioUtil::ScreenshotAny(emu.get()).Save(
            StringPrintf("manual340-%d.png", p));
      }

      uint8_t b = movie[idx];
      emu->Step(b, 0);
      if (eval.Succeeded(emu.get())) {
        std::vector<uint8_t> out;
        if (p > 0) {
          for (int i = 0; i < p; i++) out.push_back(0);
        }
        for (int i = start_idx; i <= idx; i++) out.push_back(movie[i]);
        std::string fm7 = SimpleFM7::EncodeOneLine(out);
        printf(AGREEN("Success!") " [pad %d] on %s: %s\n",
               p, ColorLevel(level).c_str(),
               fm7.c_str());
        db.AddSolution(level, out, MinusDB::METHOD_MANUAL);
        return;
      }
    }

    MarioUtil::Screenshot(emu.get()).Save(
        StringPrintf("manual-%d.png", p));

    status_per.RunIf([&]() {
        status.Emit(ANSI::ProgressBar(p + MAX_PADDING,
                                      MAX_PADDING * 2 + 1,
                                      "try offsets",
                                      timer.Seconds()));
      });
  }

  printf(ARED("Ended without solving.") "\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  if (argc > 1) {
    if (argv[1] == (std::string)"cross") {
      Cross();
    } else if (argv[1] == (std::string)"solve") {
      Solve();
    } else if (argv[1] == (std::string)"maze") {
      Maze();
    } else if (argv[1] == (std::string)"manual") {
      CHECK(argc >= 3) << "Need fm2 file.";
      std::string file = argv[2];
      Manual(argv[2]);
    } else {
      LOG(FATAL) << "Usage:\n"
        "./solve.exe [cross|solve]\n"
        "The default is solve.\n";
    }
  } else {
    Solve();
  }

  return 0;
}
