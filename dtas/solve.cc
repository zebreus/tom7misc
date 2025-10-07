
// Solves Mario Bros.; kinda like playfun but with as much
// game-specific logic as desired.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
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
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "color-util.h"
#include "hashing.h"
#include "image.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"

#include "emulator-pool.h"
#include "mario-util.h"
#include "mario.h"
#include "minus.h"
#include "evaluator.h"

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

static EmulatorPool *emulator_pool = nullptr;


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
    // status.Print("There are {} cells.\n", cells.size());
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
          if (dx != 0 || dy != 0) {
            CellId dst(src.x + dx, src.y + dy);
            /*
            status.Print("Dst {},{}. OnScreen: {}\n",
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
        status.Print(ARED("No cells remain??") "\n");
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

        if (evaluator->Stuck(emu.get()))
          break;

        if (evaluator->Succeeded(emu.get())) {
          if (GetOutcome() == Outcome::SUCCESS) {
            Print(AYELLOW("Simultaneously solved")
                   " {}! Ignored.\n", ColorLevel(level_id));
            return;
          }

          SetOutcome(Outcome::SUCCESS);

          std::vector<uint8_t> full_sol = src_movie;
          for (uint8_t b : edge_movie) full_sol.push_back(b);
          db->AddSolution(level_id, full_sol, MinusDB::METHOD_MAZE);
          Print(AGREEN("Solved") " {}: " AGREY("{}") "\n",
                ColorLevel(level_id),
                SimpleFM7::EncodeOneLine(full_sol));
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

    evaluator.reset(new Evaluator(emulator_pool, solver_emu.get()));

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
      status.Print(
          "Start pos: {},{} = cell: {},{}\n",
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
        status.Print("Solver on {} ran out of time with "
                     AYELLOW("{}") " cells done ({}).\n",
                     ColorLevel(level_id),
                     (int)cells.size(),
                     ANSI::Time(solve_timer.Seconds()));
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

        AppendFormat(
            &lines,
            "{} attempted. {} steps ({} here; {:.1f}/sec). "
            "Solved " AGREEN("{}") "\n",
            levels_attempted.Read(),
            MarioUtil::FormatNum(total_steps),
            attempt_steps, sps,
            (int)levels_solved.Read());
        AppendFormat(
            &lines,
            "Maze solver on {}. " AYELLOW("{}") " cells from "
            "{} expansions in "
            " {}/{}.\n",
            ColorLevel(level_id),
            num_cells,
            MarioUtil::FormatNum(cells_expanded),
            ANSI::Time(solve_timer.Seconds()),
            ANSI::Time(solve_time));

        status.EmitStatus(lines);
      }

      images_per.RunIf([&]() {
          status.Print("Save images...\n");
          image_counter++;
          WriteImage(image_counter);
        });
    }

    status.Print("Waiting for solve threads to exit.\n");
    for (std::thread &t : workers) t.join();
    workers.clear();

    status.Print("Save final images:\n");
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
        status.Print(ARED("No cells!?") "\n");
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
                      std::format("{}", expansions));
    }

    const auto &[major, minor] = UnpackLevel(level_id);
    map.BlendText32(1, 1, 0xFFFFFFFF,
                    std::format("Maze ({:02x}-{:02x}): {} cells",
                                 major, minor, (int)done.size()));

    std::string filename = std::format("maze-{:02x}-{:02x}-{}.png",
                                       major, minor, counter);
    map.Save(filename);
    status.Print("Wrote " ABLUE("{}") "\n", filename);
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
            Print(AYELLOW("Simultaneously solved")
                  " {}! Ignored.\n", ColorLevel(level_id));
            return;
          }
          SetOutcome(Outcome::SUCCESS);
          std::vector<uint8_t> full_sol = start.movie;
          for (uint8_t b : f.moves) full_sol.push_back(b);
          db->AddSolution(level_id, full_sol, MinusDB::METHOD_SOLVE);
          Print(AGREEN("Solved") " {}: " AGREY("{}") "\n",
                ColorLevel(level_id),
                SimpleFM7::EncodeOneLine(full_sol));
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
    status(SCREENSHOT_LINES + STATUS_LINES),
    db(db),
    level_id((uint16_t(major) << 8) | minor) {

    SetOutcome(Outcome::RUNNING);

    emu_steps_attempt.Reset();

    solver_emu.reset(Emulator::Create(ROMFILE));
    CHECK(solver_emu.get() != nullptr) << ROMFILE;
    MarioUtil::WarpTo(solver_emu.get(), major, minor, 0);
    start_state = solver_emu->SaveUncompressed();

    evaluator.reset(new Evaluator(emulator_pool, solver_emu.get()));

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
        status.Print("Maze solver on {} ran out of time ({}).\n",
                     ColorLevel(level_id),
                     ANSI::Time(solve_timer.Seconds()));
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

        std::string lines = ANSIScreenshotWithLock();

        AppendFormat(
            &lines,
            "{} attempted. {} steps ({} here; {:.1f}/sec). "
            "Solved " AGREEN("{}") "\n",
            levels_attempted.Read(),
            MarioUtil::FormatNum(total_steps),
            attempt_steps, sps,
            levels_solved.Read());
        AppendFormat(
            &lines,
            "Running on {}. Pop size {}. {} stuck. "
            "Elapsed {}/{}.\n",
            ColorLevel(level_id),
            (int)population.size(),
            (int)futures_stuck.Read(),
            ANSI::Time(solve_timer.Seconds()),
            ANSI::Time(solve_time));

        AutoHisto eval;
        AutoHisto moves;
        for (const State &state : population) {
          eval.Observe(state.eval);
          moves.Observe(state.movie.size());
        }

        AppendFormat(&lines,
                     AWHITE("Eval") " {}:\n"
                     "{}\n",
                     eval.IsIntegral() ? "(integral)" : "(float)",
                     eval.SimpleHorizANSI(12));
        AppendFormat(&lines,
                     AWHITE("Moves") " {}:\n"
                     "{}\n",
                     moves.IsIntegral() ? "(integral)" : "(float)",
                     moves.SimpleHorizANSI(10));

        status.EmitStatus(lines);
      }

      images_per.RunIf([&]() {
          status.Print("Save images...\n");
          image_counter++;
          WriteImage(image_counter);
        });
    }

    status.Print("Waiting for solve threads to exit.\n");
    for (std::thread &t : workers) t.join();
    workers.clear();

    status.Print("Save final images:\n");
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
        status.Print("Saved one best solution: {} moves, {:.3f} eval\n",
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
      status.Print("No population! So no images.\n");
      return;
    }

    const auto &[major, minor] = UnpackLevel(level_id);

    std::string filename =
      std::format("solve{:02x}-{:02x}_{}.png", major, minor, counter);

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
                      std::format("Solving {:02x}-{:02x}. Best eval: {:.4f}  "
                                  "Elapsed: {} sec",
                                  major, minor, best_eval,
                                  (int)elapsed.Seconds()));

    map.Save(filename);
    status.Print("Wrote {}.\n", filename);

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
                       std::format("Solving {:02x}-{:02x}. Best eval: {:.4f}  "
                                   "Elapsed: {} sec",
                                   major, minor, best_eval,
                                   (int)elapsed.Seconds()));

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
                       std::format("^ {},{}", pos.x, pos.y));
      text.BlendText32(ix + 2, iy + 250,
                       0x00FFFFCC,
                       std::format("{} moves, eval {:.4f}",
                                   (int)state.movie.size(),
                                   state.eval));
    }

    img.BlendImage(0, 0, text);
    std::string filename =
      std::format("solve{:02x}-{:02x}_{}.png", major, minor, counter);
    img.Save(filename);
    status.Print("Wrote {}.\n", filename);
    #endif
  }

  // Must hold lock.
  std::string ANSIScreenshotWithLock() {
    State state;
    if (population.empty()) {
      return std::string(SCREENSHOT_LINES, '\n');
    }

    solver_emu->LoadUncompressed(population[0].save);
    return MarioUtil::ScreenshotANSI(solver_emu.get());
  }

  static constexpr int SCREENSHOT_LINES = 30;
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
  std::unordered_set<LevelId> done = db->GetSolved();
  std::unordered_set<LevelId> rejected = db->GetRejected();
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

  #if 0
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

  #if 0
  for (int maj = 0; maj < 256; maj++) {
    do_first.insert(PackLevel(maj, 3));
  }
  #endif

  if (!do_first.empty()) {
    Print(AWHITE("Do first:"));
    for (LevelId level : do_first) {
      if (!done.contains(level) && !rejected.contains(level)) {
        Print(" {}", ColorLevel(level));
        todo.push_back(level);
      }
    }
    Print("\n");
  }

  std::vector<LevelId> rest;
  for (int level = 0; level < 65536; level++) {
    if (!done.contains(level) && !rejected.contains(level) &&
        !do_first.contains(level)) {
      rest.push_back(level);
    }
  }
  // In a random order.
  Shuffle(rc, &rest);
  for (LevelId level : rest) todo.push_back(level);

  Print("{} done. {} remain\n", done.size(), todo.size());

  return todo;
}

[[maybe_unused]]
static void Solve() {
  MinusDB db;
  ArcFour rc(std::format("solve.{}", time(nullptr)));

  std::vector<LevelId> todo = GetTodo(&db, &rc);
  std::unordered_set<LevelId> attempted = db.GetHasPartial();

  static constexpr bool ONLY_FRESH = true;

  for (LevelId level : todo) {
    if (ONLY_FRESH && attempted.contains(level)) {
      continue;
    } else if (db.HasSolution(level)) {
      levels_skipped++;
      Print("Level {} was solved in the meantime!\n",
            ColorLevel(level));
      continue;
    }
    auto [major, minor] = UnpackLevel(level);
    Solver solver(&db, major, minor, SOLVE_TIME, 8);
  }
}

[[maybe_unused]]
static void Maze() {
  MinusDB db;
  ArcFour rc(std::format("maze.{}", time(nullptr)));

  std::vector<LevelId> todo = GetTodo(&db, &rc);
  std::unordered_set<LevelId> attempted = db.GetHasPartial();

  static constexpr bool ONLY_FRESH = false;

  for (LevelId level : todo) {
    if (ONLY_FRESH && attempted.contains(level)) {
      continue;
    } else if (db.HasSolution(level)) {
      levels_skipped++;
      Print("Level {} was solved in the meantime!\n",
            ColorLevel(level));
      continue;
    }
    auto [major, minor] = UnpackLevel(level);
    MazeSolver maze_solver(&db, major, minor, MAZE_TIME, 8);
  }
}

[[maybe_unused]]
static void Cross(int64_t start_time) {
  MinusDB db;
  ArcFour rc(std::format("cross.{}", time(nullptr)));

  StatusBar status(1);
  status.Status("Preparing solutions.\n");

  std::vector<LevelId> todo = GetTodo(&db, &rc);

  // Get all the solutions. There are many duplicates (owing for example
  // to this very strategy!) so we deduplicate them. We also only consider
  // solutions after the start_time.
  std::vector<MinusDB::SolutionRow> all_sols = db.GetAllSolutions();
  status.Print("There are " AGREEN("{}") " existing solutions.\n",
               all_sols.size());

  // Index of the earliest instance of that solution.
  std::unordered_map<std::vector<uint8_t>, int,
                     Hashing<std::vector<uint8_t>>> provenance;
  for (int idx = 0; idx < all_sols.size(); idx++){
    const MinusDB::SolutionRow &row = all_sols[idx];
    // Assume it's a duplicate (or actually a prefix) of an original
    // solution, and skip it.
    if (row.method == MinusDB::METHOD_CROSS)
      continue;

    if (row.createdate < start_time)
      continue;

    auto it = provenance.find(row.movie);
    if (it == provenance.end()) {
      provenance[row.movie] = idx;
    } else if (row.createdate < all_sols[it->second].createdate) {
      it->second = idx;
    }
  }

  status.Print("There are " APURPLE("{}") " distinct original sols "
               "after the start time of " ABLUE("{}") ".\n",
               provenance.size(),
               start_time);

  // Now put it in the form we like.
  std::vector<std::pair<int, std::vector<uint8_t>>> sols;
  sols.reserve(provenance.size());
  for (const auto &[m_, idx] : provenance)
    sols.emplace_back(all_sols[idx].level, all_sols[idx].movie);

  CHECK(emulator_pool != nullptr);

  // These might be big. No need to keep them around.
  provenance.clear();
  all_sols.clear();

  // const int64_t total_work = sols.size() * todo.size();

  Timer elapsed;
  Periodically status_per(1.0);
  ParallelApp(
      todo,
      [&db, &todo, &sols, &elapsed, &status, &status_per](LevelId level) {
        const auto &[major, minor] = UnpackLevel(level);

        if (db.HasSolution(level)) {
          levels_skipped++;
          status.Print("Level {} was solved in the meantime!\n",
                       ColorLevel(level));
          return;
        }

        EmulatorPool::Lease emu = emulator_pool->AcquireClean();
        CHECK(emu.get() != nullptr) << ROMFILE;
        MarioUtil::WarpTo(emu.get(), major, minor, 0);
        std::vector<uint8_t> level_start_state = emu->SaveUncompressed();
        Evaluator eval(emulator_pool, emu.get());

        for (const auto &[source_level, movie] : sols) {
          emu->LoadUncompressed(level_start_state);
          for (int i = 0; i < (int)movie.size(); i++) {
            emu->Step(movie[i], 0);
            emu_steps_total++;
            if (eval.Stuck(emu.get())) {
              futures_stuck++;
              break;
            } else if (eval.Succeeded(emu.get())) {
              std::vector<uint8_t> prefix = movie;
              prefix.resize(i + 1);
              db.AddSolution(level, prefix, MinusDB::METHOD_CROSS);
              status.Print("Solved {} (via {}): {}\n",
                           ColorLevel(level),
                           ColorLevel(source_level),
                           SimpleFM7::EncodeOneLine(prefix));
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
                std::format(
                    AWHITE("{:.1f}% ")
                    AGREEN("{}") " + "
                    ARED("{}") " + "
                    AYELLOW("{}")
                    " / "
                    "{} × {}",
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

              status.Status(AWHITE("[") "{}" AWHITE("]") " {}\n",
                            bar,
                            eta);
            });
        }

        levels_attempted++;
      },
      12);

  Print("Finished cross in {}. New sols: " AGREEN("{}") "\n",
        ANSI::Time(elapsed.Seconds()),
        levels_solved.Read());
}

static void Manual(LevelId level,
                   const std::string &fm7file) {
  MinusDB db;

  std::vector<uint8_t> movie = SimpleFM7::ReadInputs(fm7file);

  static constexpr int FRAMES_DENOUMENT = 4 * 60;
  Print("Trying manual solution from " AWHITE("{}")
        " ({} inputs + {} denoument) on {}.\n",
        fm7file, (int)movie.size(), FRAMES_DENOUMENT,
        ColorLevel(level));

  for (int i = 0; i < FRAMES_DENOUMENT; i++) movie.push_back(0);

  const auto &[major, minor] = UnpackLevel(level);
  bool do_write = true;

  if (db.HasSolution(level)) {
    Print(AORANGE("Already solved: ") "{}" "\n",
          ColorLevel(level));
    do_write = false;
    // return;
  }

  EmulatorPool::Lease emu = emulator_pool->AcquireClean();
  CHECK(emu.get() != nullptr);

  MarioUtil::WarpTo(emu.get(), major, minor, 0);
  std::vector<uint8_t> level_start_state = emu->SaveUncompressed();

  Evaluator eval(emulator_pool, emu.get());

  Timer timer;
  Periodically status_per(0.5);
  StatusBar status(1);

  static constexpr bool WRITE_IMAGES = false;

  bool solved = false;
  std::vector<uint8_t> sol;
  int frame = 0;
  for (uint8_t b : movie) {
    if (eval.Stuck(emu.get())) {
      LOG(FATAL) << "Failed: Player is stuck/dead.";
    }

    if (eval.Succeeded(emu.get())) {
      std::string fm7 = SimpleFM7::EncodeOneLine(sol);
      status.Print(AGREEN("Success!") " on {}: {}\n",
                   ColorLevel(level), fm7);
      if (do_write) {
        db.AddSolution(level, sol, MinusDB::METHOD_MANUAL);
      }

      solved = true;

      break;
    }

    emu->StepFull(b, 0);
    sol.push_back(b);

    if (WRITE_IMAGES) {
      MarioUtil::Screenshot(emu.get()).Save(
          std::format("manual/manual{}.png", frame));
      frame++;
    }

    status_per.RunIf([&]() {
        status.Emit(ANSI::ProgressBar(sol.size(), movie.size(),
                                      "replay",
                                      timer.Seconds()));
      });
  }

  {
    auto map_emu = emulator_pool->Acquire();
    map_emu->LoadUncompressed(level_start_state);
    ImageRGBA map = MarioUtil::MakeMap(map_emu.get(), sol);

    map_emu->LoadUncompressed(level_start_state);
    std::vector<Pos> path = MarioUtil::GetPath(map_emu.get(), sol);

    MarioUtil::DrawPath(path, &map, 0xFF000044);
    map.Save("manual-map.png");
    status.Print("Wrote manual-map.png.");
  }

  MarioUtil::ScreenshotAny(emu.get()).Save("manual.png");
  status.Print("Wrote manual.png");

  if (!solved) {
    status.Print(ARED("Ended without solving."));
  }
}


static void Never() {
  MinusDB db;
  const std::unordered_set<LevelId> solved = db.GetSolved();
  const std::unordered_set<LevelId> rejected = db.GetRejected();

  Print("{} already done, {} already rejected.\n",
        solved.size(), rejected.size());

  Timer timer;
  Periodically status_per(5.0);
  StatusBar status(1);
  ParallelComp(
      65536,
      [&db, &solved, &rejected, &timer, &status_per, &status](int idx) {
        int major = idx / 256;
        int minor = idx % 256;
        const LevelId level = PackLevel(major, minor);
        levels_attempted++;
        if (solved.contains(level) || rejected.contains(level)) {
          levels_skipped++;
          return;
        }

        auto emu = emulator_pool->AcquireClean();
        CHECK(emu.get() != nullptr);
        MarioUtil::WarpTo(emu.get(), major, minor, 0);

        if (false) {
          const uint8_t oper_mode = emu->ReadRAM(OPER_MODE);
          const uint8_t oper_task = emu->ReadRAM(OPER_MODE_TASK);
          status.Print("[{}] {:02x}.{:02x}\n",
                       ColorLevel(level),
                       oper_mode, oper_task);
        }
        // std::vector<uint8_t> level_start_state = emu->SaveUncompressed();
        Evaluator eval(emulator_pool, emu.get());

        if (eval.NeverStarted()) {
          levels_solved++;
          db.AddRejected(level, MinusDB::REJECT_NEVER);
        }

        status_per.RunIf([&](){
            const int numer = levels_attempted.Read();
            std::string bar =
              ANSI::ProgressBar(
                  numer, 65536,
                  std::format(
                      "[{}] Rejected " ARED("{}") " skipped "
                      AWHITE("{}"),
                      ColorLevel(level),
                      levels_solved.Read(),
                      levels_skipped.Read()),
                  timer.Seconds());

            status.EmitStatus(bar);
          });
      }, 16);

  Print("\n"
        "Finished. " ARED("{}") " were rejected as unsolvable.\n"
        AYELLOW("{}") " were skipped (already done).\n"
        "Took: {}\n",
        levels_solved.Read(),
        levels_skipped.Read(),
        ANSI::Time(timer.Seconds()));
}

// max_states is the largest number of states in the hash set before
// we give up on this strategy.
//
// When the player is totally stuck, we want to detect this
// by running out the clock. This takes about 10,000 frames.
// On other levels we might be able to manipulate our state
// slightly, but always die quickly.
static bool IsAlwaysDead(int max_states,
                         LevelId level,
                         Emulator *emu,
                         int status_index,
                         StatusBar *status) {
  std::vector<uint8_t> start_state = emu->SaveUncompressed();
  Evaluator eval(emulator_pool, emu);

  Periodically status_per(2.0);
  Periodically depths_per(60.0, false);
  Timer timer;


  // Unnecessary visualization!
  // At every depth, a memory value count histogram. We assume
  // that the values are low entropy, so we store them sparsely.
  struct Depth {
    // Size 2048. The histogram of values seen in that location.
    std::vector<std::unordered_map<int, int>> memhisto;
    // Number of times we were at this depth.
    int occurrences = 0;
  };
  std::vector<Depth> depths;
  static constexpr int MAX_DEPTH = 200;
  depths.resize(MAX_DEPTH);

  // We are trying to create the entire state graph, where edges
  // correspond to a single frame with a specific input. Rather than
  // represent the graph explicitly, though, we put a state in this
  // set only when we know that every descendent of it (possibly
  // including itself) results in death. This will quickly get out
  // of hand unless inputs usually have no effect on the state.
  //
  // The save states themselves are kinda big, and we don't need
  // the whole thing. Instead we use the processor state and RAM,
  // assuming that the game outcomes don't depend on anything but
  // this (it would not be true in general, but I think it is true
  // for mario). We could improve recall (and performance) by
  // ablating useless state here, although it would reduce
  // confidence in the result.
  // using State = std::array<uint8_t, 2048 + 8>;
  using State = std::vector<uint8_t>;
  auto GetState = [](const Emulator *emu) {
      State state;
      state.resize(2048 + 8);
      std::span<uint8_t> out(state);
      uint64_t reg = emu->Registers();
      for (int i = 0; i < 8; i++) {
        state[i] = reg & 0xFF;
        reg >>= 8;
      }
      emu->CopyMemory(out.last(2048));
      return state;
    };

  std::unordered_set<State, Hashing<State>> states;

  // Select does nothing so we don't bother. We don't allow pausing.
  static constexpr uint8_t CONTROLLER_MASK =
    INPUT_U | INPUT_D | INPUT_L | INPUT_R |
    INPUT_B | INPUT_A;

  // Do the search starting at the state that emu is currently in.
  int64_t frames = 0, deaths = 0;

  // Returns true if every descendant results in death. Returns false
  // if we succeeded (!) or exhausted the state budget (typical).
  //
  // This modifies a single emulator (emu), reading it on
  // input and leaving it in an unspecified state. Caller should
  // save the state if they need it.
  std::function<bool(int)> InsertRec =
    [emu, max_states, &depths, &depths_per,
     &level, &status_per, &timer, status_index, &status, &GetState,
     &InsertRec, &eval, &states, &frames, &deaths](int depth) {
      // if ((int)states.size() > max_states) return false;
      // Since the success case involves inserting all the states
      // back to the root, we can exit once the depth exceeds
      // the remaining space. This is also important for cases
      // where the game is frozen and the death timer is not
      // actually running.
      if ((int)states.size() + depth > max_states) return false;
      if (eval.Succeeded(emu)) return false;
      if (eval.IsDead(emu)) {
        deaths++;
        return true;
      }

      if (status_per.ShouldRun()) {
        if (status_index == 0) {
          std::string s = MarioUtil::ScreenshotANSI(emu);
          status->Print("Depth {}:\n", depth);
          status->Emit(s);
        }
        std::string msg =
          std::format(
              "{}: " AYELLOW("↓") "{}, {} st, {} fr, "
              ARED("☠") "{}",
              ColorLevel(level),
              depth,
              (int64_t)states.size(),
              MarioUtil::FormatNum(frames),
              MarioUtil::FormatNum(deaths));

        std::string prog =
          ANSI::ProgressBar(depth + states.size(), max_states,
                            msg,
                            timer.Seconds(),
                            ANSI::ProgressBarOptions{
                              .full_width = 72,
                              .bar_filled = 0x420f6eFF,
                              .bar_empty = 0x210936FF,
                              .include_frac = false,
                            });

        status->EmitLine(status_index, prog);
      }

      const State current_state = GetState(emu);
      // It's essential that we memoize, or else this is
      // intractable (unless e.g. we are always dead on the first frame)!
      if (states.contains(current_state)) return true;

      if (depth < MAX_DEPTH) {
        Depth &d = depths[depth];
        d.occurrences++;
        // Be lazy in case we don't reach the depth, but once we do we'll
        // have at least one value for every memory address.
        if (d.memhisto.empty()) {
          d.memhisto.resize(2048);
        }
        for (int addr = 0; addr < 2048; addr++) {
          uint8_t v = emu->ReadRAM(addr);
          d.memhisto[addr][v]++;
        }

        if (depths_per.ShouldRun()) {
          // Save depths image.
          ImageRGBA img(MAX_DEPTH, 2048);
          img.Clear32(0x000000FF);

          // Also, a text version since we definitely care about the
          // specific memory locations.
          std::string content;

          for (int x = 0; x < MAX_DEPTH; x++) {
            if (x >= depths.size() || depths[x].memhisto.empty())
              continue;

            AppendFormat(&content, "Depth {} ({}):", x,
                         depths[x].occurrences);

            int variable_locations = 0;
            for (int y = 0; y < 2048; y++) {
              int distinct_values = (int)depths[x].memhisto[y].size();
              if (distinct_values > 1) {
                variable_locations++;
              }
            }

            if (variable_locations == 0) content.append(" const.\n");
            else content.append("\n");

            bool many = variable_locations > 64;
            if (many) content.append("  Many variable:");

            for (int y = 0; y < 2048; y++) {
              int distinct_values = (int)depths[x].memhisto[y].size();
              CHECK(distinct_values != 0) << "This should be impossible, "
                "since there always has to be a value in each memory "
                "location.";
              if (distinct_values == 1) {
                // This is the boring case; don't draw anything.
              } else {
                // Otherwise, color according to how many different
                // values are taken on. The maximum would be 256.
                float f = sqrtf(distinct_values / 256.0f);
                uint32_t c = ColorUtil::LinearGradient32(
                    ColorUtil::HEATED_TEXT, f);
                img.SetPixel32(x, y, c);

                static constexpr bool SYMBOLIC = true;
                if (many) {
                  if (SYMBOLIC) {
                    AppendFormat(&content, " {}",
                                 MarioUtil::DescribeAddress(y));
                  } else {
                    AppendFormat(&content, " {:04x}", y);
                  }
                } else {
                  if (SYMBOLIC) {
                    AppendFormat(&content, " {}:",
                                 MarioUtil::DescribeAddress(y));
                  } else {
                    AppendFormat(&content, "  {:04x}:", y);
                  }
                  std::vector<std::pair<int, int>> val_count;
                  for (const auto &[val, count] : depths[x].memhisto[y]) {
                    CHECK(count != 0);
                    val_count.emplace_back(val, count);
                  }
                  // Sort descending by count, then ascending by value.
                  std::sort(val_count.begin(), val_count.end(),
                            [](const auto &a, const auto &b) {
                              if (a.second == b.second)
                                return a.first < b.first;
                              return a.second > b.second;
                            });
                  for (int i = 0; i < val_count.size() && i < 6; i++) {
                    const auto &[val, count] = val_count[i];
                    if (count == 1) {
                      AppendFormat(&content, " {:02x}", val);
                    } else {
                      AppendFormat(&content, " {:02x}[{}]", val, count);
                    }
                  }
                  int remain = (int)val_count.size() - 6;
                  if (remain > 0) {
                    AppendFormat(&content, " ({} more)\n", remain);
                  } else {
                    AppendFormat(&content, "\n");
                  }
                }
              }
            }

            if (many) AppendFormat(&content, "\n");
          }


          const auto [major, minor] = UnpackLevel(level);
          {
            std::string filename =
              std::format("always-depths{:02x}-{:02x}.png", major, minor);
            img.Save(filename);
            status->Print("Wrote " AGREEN("{}") "\n", filename);
          }
          {
            std::string filename =
              std::format("always-depths-{:02x}-{:02x}.txt", major, minor);
            Util::WriteFile(filename, content);
            status->Print("Wrote " AGREEN("{}") "\n", filename);
          }


        }
      }

      std::vector<uint8_t> current_save = emu->SaveUncompressed();

      for (int b = 0; b < 256; b++) {
        // Only canonical button states.
        if (b == (b & CONTROLLER_MASK)) {
          // Try it.
          emu->LoadUncompressed(current_save);
          emu->Step(b, 0);
          frames++;
          bool r = InsertRec(depth + 1);
          if (!r) return false;
        }
      }

      // If we get here than death is inevitable, so add it to the states
      // vector.
      states.insert(current_state);
      return true;
    };

  if (InsertRec(0)) {
    status->Print(AGREEN("Always dead") " on {} ({} reachable states).\n",
                  ColorLevel(level), states.size());
    return true;
  } else {
    status->Print(AYELLOW("No joy") " on {} ({} reachable states).\n",
                  ColorLevel(level), states.size());
    return false;
  }
}

// Another common cause of impossible levels: The game is in a "cutscene"
// mode (like used in 00-01) but mario is not moving. In this case the
// game never exits the cutscene and is unplayable. "Always dead" doesn't
// generally catch this because the timer doesn't initialize, and thus
// mario will never actually die. Evaluator doesn't consider this
// "never started" because we do have a level id and we ARE in mode:task
// 1:3. (Subroutine is 7 (vert pipe/interstitial) rather than 8 (playing),
// but we don't want to reject a level like 00-01 as it is solvable!).
// See PlayerEntrance: in game code.
//
// An example level would be 13-4b. The player never gains control because
// when mario's y position is less than 0x30, it autocontrols the player
// with no input (I guess assuming that he will fall).
static bool IsCutscene(int max_states,
                       LevelId level,
                       Emulator *emu,
                       int status_index,
                       StatusBar *status) {
  ArcFour rc(std::format("cutscene.{}", level));
  Evaluator eval(emulator_pool, emu);

  Periodically status_per(2.0);
  Timer timer;

  // Wait until the game gets into the interstitial mode.
  status->LineStatus(status_index, "Find intro subroutine");
  for (int i = 0; i < 1000; i++) {
    const uint8_t mode = emu->ReadRAM(OPER_MODE);
    const uint8_t task = emu->ReadRAM(OPER_MODE_TASK);
    const uint8_t sub = emu->ReadRAM(GAME_ENGINE_SUBROUTINE);
    if (mode == 1 && task == 3) {
      if (sub == 7) break;
      if (sub == 8) {
        // If the player ever gains control, this is not a cutscene.
        break;
      }
      // But otherwise we allow various initialization subroutines to run.
    }
    emu->StepFull(0, 0);
  }

  {
    const uint8_t mode = emu->ReadRAM(OPER_MODE);
    const uint8_t task = emu->ReadRAM(OPER_MODE_TASK);
    const uint8_t sub = emu->ReadRAM(GAME_ENGINE_SUBROUTINE);
    // Also if we are using alt entrance control (pipe/vine) then this is
    // not the kind of cutscene we know how to handle. This may never happen
    // at level start, anyway.
    const uint8_t aec = emu->ReadRAM(ALT_ENTRANCE_CONTROL);
    if (mode != 1 || task != 3 || sub != 7 || aec == 2) {
      status->Print(AYELLOW("Not cutscene") " on {} ({:02x}:{:02x}:{:02x}).\n",
                    ColorLevel(level),
                    mode, task, sub);
      return false;
    }
  }

  status->LineStatus(status_index, "Running cutscene");

  // If mario has y pos < 0x30, he is automatically controlled (no inputs).
  //
  // Otherwise, when PLAYER_ENTRANCE_CONTROL is 6 or 7, mario is automatically
  // controlled (hold right). This condition ends if mario's sprite is set
  // to background mode, which is a byproduct of entering a pipe.
  //
  // Otherwise, we enter the play state (1:3:8).
  //
  // Since mario is automatically controlled, and gaining a y position
  // >= 30 or going in a pipe both require mario to move, we are stuck
  // forever if mario is not moving. In principle there could be something
  // temporary blocking him (moving platforms) or he could be killed by
  // an enemy, but we assume that if this does not happen for a very long
  // time, we are truly stuck.
  // This is about 30 minutes of game time.
  static constexpr int TARGET_STUCK_FRAMES = 100000;
  static constexpr int MAX_FRAMES = TARGET_STUCK_FRAMES * 10;

  // Select does nothing so we don't bother. We don't allow pausing.
  static constexpr uint8_t CONTROLLER_MASK =
    INPUT_U | INPUT_D | INPUT_L | INPUT_R |
    INPUT_B | INPUT_A;

  MarioUtil::Pos last_pos = MarioUtil::GetPos(emu);

  int stuck_frames = 0;
  for (int frames = 0; frames < MAX_FRAMES; frames++) {

    if (status_per.ShouldRun()) {
      std::string msg =
        std::format(
            "{}: {} stuck {} total (" ABLUE("{}") "," ABLUE("{}") ")",
            ColorLevel(level),
            stuck_frames, frames,
            last_pos.x, last_pos.y);

      std::string prog =
        ANSI::ProgressBar(frames, MAX_FRAMES,
                          msg,
                          timer.Seconds(),
                          ANSI::ProgressBarOptions{
                            .full_width = 72,
                            .bar_filled = 0x420f6eFF,
                            .bar_empty = 0x210936FF,
                            .include_frac = false,
                          });

      status->EmitLine(status_index, prog);
    }

    const uint8_t mode = emu->ReadRAM(OPER_MODE);
    const uint8_t task = emu->ReadRAM(OPER_MODE_TASK);
    const uint8_t sub = emu->ReadRAM(GAME_ENGINE_SUBROUTINE);

    bool success = eval.Succeeded(emu);
    bool dead = eval.IsDead(emu);
    bool playing = mode == 1 && task == 3 && sub == 8;
    if (success || dead || playing) {
      // If we can win, die, or play, then we are not stuck.
      status->Print(AYELLOW("No joy") " on {}: {}{}{}\n",
                    ColorLevel(level),
                    success ? "success " : "",
                    dead ? "dead " : "",
                    playing ? "playing " : "");
      return false;
    }

    const uint8_t joy = emu->ReadRAM(LAST_JOYPAD);
    if (!(joy == 0x00 || joy == 0x01)) {
      status->Print(AORANGE("Not cutscene") " on {} because we seem to have "
                    "control still??\n",
                    ColorLevel(level));
      return false;
    }

    // We press random buttons to make sure we are actually being
    // autocontrolled. This is more like a sanity check than part of
    // the unsolvability argument.
    const uint8_t b = rc.Byte() & CONTROLLER_MASK;
    emu->StepFull(b, 0);

    MarioUtil::Pos pos = MarioUtil::GetPos(emu);
    if (pos != last_pos) {
      // We moved. So reset the counter.
      stuck_frames = 0;
      last_pos = pos;
    } else {
      stuck_frames++;
    }

    if (stuck_frames == TARGET_STUCK_FRAMES) {
      status->Print(
          AGREEN("Stuck cutscene") " on {} after {} frames ({} stuck)\n",
          ColorLevel(level),
          frames, stuck_frames);
      return true;
    }
  }

  // This is strange. We are moving for a long time but not reaching
  // either condition.
  status->Print(AORANGE("Too many frames") " on {}. Stuck {}.\n",
                ColorLevel(level),
                stuck_frames);
  return false;
}

// TODO: Another situation, seen on eg. 1f-fa, is that we are in a cutscene
// and falling, but instantly die (TIME UP) upon exiting the cutscene. This is
// not found by always dead since the player never actually gets control,
// and it is not found by cutscene because the player is continuously moving.

template<class F>
static void TryToReject(std::optional<LevelId> args,
                        const std::string &method_name, int rejected_method,
                        int64_t limit, const F &IsRejected) {
  MinusDB db;
  const std::unordered_set<LevelId> solved = db.GetSolved();
  const std::unordered_set<LevelId> rejected = db.GetRejected();

  const std::unordered_set<LevelId> already =
    db.GetAttemptedByMethod(rejected_method);

  Print(
      "Trying to reject with " AWHITE("{}") ".\n"
      "{} already done.\n"
      "{} already attempted with this method.\n"
      "{} already rejected by any method.\n",
      method_name,
      solved.size(),
      already.size(),
      rejected.size());

  Timer timer;
  Periodically status_per(5.0);

  static constexpr int PARALLEL_THREADS = 12;
  const int NUM_THREADS = args.has_value() ? 1 : PARALLEL_THREADS;

  // One status line at the bottom for the overall summary.
  const int all_status_idx = NUM_THREADS;
  StatusBar status(1 + NUM_THREADS);

  std::mutex m;
  std::vector<LevelId> todo;
  if (args.has_value()) {
    Print("Running only {}.\n", ColorLevel(args.value()));
    todo.push_back(args.value());
  } else {
    for (int i = 0; i < 65536; i++) {
      // for (int i = 65535; i >= 0; i--) {
      // const auto &[major, minor] = UnpackLevel(i);

      // In the future, we could try again with a higher depth budget.
      if (already.contains(i)) continue;

      // No point in doing it if we already have a definitive answer.
      if (solved.contains(i)) continue;
      if (rejected.contains(i)) continue;

      todo.push_back(i);
    }
  }

  const int denominator = todo.size();

  ParallelFan(
      NUM_THREADS,
      [&IsRejected, rejected_method, limit,
       &db, &timer, &status_per, &status, all_status_idx,
       &m, &todo, denominator](int thread_idx) {

        for (;;) {
          LevelId level = 0;
          bool done = false;
          {
            MutexLock ml(&m);
            if (todo.empty()) {
              done = true;
            } else {
              level = todo.back();
              todo.pop_back();
            }
          }

          // Not holding lock.
          if (done) {
            status.EmitLine(thread_idx, "no more work");
            return;
          } else {
            status.LineStatus(
                thread_idx,
                "Start {}.", ColorLevel(level));
          }

          const auto &[major, minor] = UnpackLevel(level);
          levels_attempted++;

          auto emu = emulator_pool->AcquireClean();
          CHECK(emu.get() != nullptr);
          MarioUtil::WarpTo(emu.get(), major, minor, 0);

          if (IsRejected(limit, level, emu.get(), thread_idx, &status)) {
            levels_solved++;
            db.AddRejected(level, rejected_method);
          } else {
            db.AddAttempted(level, rejected_method, limit);
          }

          status_per.RunIf([&](){
              const int numer = levels_attempted.Read();
              std::string bar =
                ANSI::ProgressBar(
                    numer, denominator,
                    std::format(
                        "[{}] Rejected " ARED("{}") " skipped "
                        AWHITE("{}"),
                        ColorLevel(level),
                        levels_solved.Read(),
                        levels_skipped.Read()),
                    timer.Seconds());

              status.EmitLine(all_status_idx, bar);
            });
        }
      });

  Print("\n"
        "Finished. " ARED("{}") " were rejected as unsolvable.\n"
        AYELLOW("{}") " were skipped (already done).\n"
        "Took: {}\n",
        levels_solved.Read(),
        levels_skipped.Read(),
        ANSI::Time(timer.Seconds()));
}


static void AlwaysDead(std::optional<LevelId> args) {
  TryToReject(args, "alwaysdead",
              MinusDB::REJECT_ALWAYS_DEAD, 500000, IsAlwaysDead);
}

static void Cutscene(std::optional<LevelId> args) {
  TryToReject(args, "cutscene",
              MinusDB::REJECT_CUTSCENE, 100000, IsCutscene);
}

int main(int argc, char **argv) {
  ANSI::Init();

  emulator_pool = new EmulatorPool(ROMFILE);
  CHECK(emulator_pool != nullptr);

  // Parse args after the strategy. Either nothing (try all levels) or a
  // single level id as major minor.
  auto Args = [argc, argv]() -> std::optional<LevelId> {
      if (argc == 2) return std::nullopt;
      CHECK(argc == 4) << "Either give major minor, or nothing.";
      const uint8_t major = strtol(argv[2], nullptr, 16);
      const uint8_t minor = strtol(argv[3], nullptr, 16);
      const LevelId level = PackLevel(major, minor);
      return {level};
    };

  if (argc > 1) {
    if (argv[1] == (std::string)"cross") {
      // e.g. to check all solutions that happened after the last
      // time we succeeded with "cross" strategy:
      // ./minus-query.exe "select * from solutions where method = 2 order by createdate desc limit 2"
      int64_t start_time = 0;
      if (argc > 2) {
        start_time = strtoll(argv[2], nullptr, 10);
      }

      Cross(start_time);
    } else if (argv[1] == (std::string)"solve") {
      Solve();
    } else if (argv[1] == (std::string)"maze") {
      Maze();
    } else if (argv[1] == (std::string)"manual") {
      CHECK(argc == 5) << "./solve.exe manual major minor movie.fm7";
      const uint8_t major = strtol(argv[2], nullptr, 16);
      const uint8_t minor = strtol(argv[3], nullptr, 16);
      const LevelId level = PackLevel(major, minor);
      std::string file = argv[4];
      Manual(level, argv[4]);
    } else if (argv[1] == (std::string)"never") {
      Never();
    } else if (argv[1] == (std::string)"alwaysdead") {
      AlwaysDead(Args());
    } else if (argv[1] == (std::string)"cutscene") {
      Cutscene(Args());

    } else {
      LOG(FATAL) << "Usage:\n"
        "./solve.exe [method [args]]\n"
        "Methods:\n"
        "  cross\n"
        "  solve\n"
        "  manual maj min file\n"
        "  never\n"
        "  alwaysdead\n"
        "  cutscene\n"
        "The default is solve.\n";
    }
  } else {
    Solve();
  }

  return 0;
}
