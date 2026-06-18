
#include <cmath>
#include <ctime>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "box2d.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "initialization.h"
#include "level.h"
#include "opt/opt-seq.h"
#include "pcg.h"
#include "periodically.h"
#include "randutil.h"
#include "scene.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "toward-util.h"
#include "util.h"

DECLARE_COUNTERS(ctr_total_evals);

// Sizes for the input/output regions.
static constexpr int IN_SIZE = 5;
static constexpr int CUP_WIDTH = 5;
static constexpr int CUP_HEIGHT = 7;


// These come from the Level now, but these are the default locations.
static constexpr int IN_X = 20;
static constexpr int IN_Y = 1;

// Out cup for 0.
static constexpr int OUT0_X = 10;
static constexpr int OUT0_Y = Levels::BLOCKS_DOWN - CUP_HEIGHT - 1;

// Out cup for 1.
static constexpr int OUT1_X = 30;
static constexpr int OUT1_Y = Levels::BLOCKS_DOWN - CUP_HEIGHT - 1;


// The centers of the inputs and the two outputs.
struct Problem {
  vec2f input;
  vec2f output0, output1;
};

// Add the walls for the input and output regions defined in the
// problem. For the input region, we put a 1 block-wide wall on its
// the left and right. For the output region, we put the same side
// walls, and also a bottom wall, making a "cup." (For this program we
// want to catch the objects so we know that they made it into the
// output, but in the future these will be linked to later inputs.)
static std::vector<LevelBody> InitialBodies(const Problem &problem) {
  std::vector<LevelBody> bodies;

  auto AddBlockRect = [&bodies](float px, float py, float w, float h,
                                uint32_t color) {
      LevelBody body;
      body.color = color;
      body.pos = vec2f(px, py);
      body.dynamic = false;
      body.mesh.vertices = {
        {0.0f, 0.0f},
        {0.0f, h * Levels::BLOCK_SIZE},
        {w * Levels::BLOCK_SIZE, h * Levels::BLOCK_SIZE},
        {w * Levels::BLOCK_SIZE, 0.0f},
      };
      body.mesh.polygons = {
        {0, 1, 2, 3},
      };

      bodies.push_back(std::move(body));
    };

  auto AddCup = [&](vec2f center, uint32_t color) {
      float ex = center.x - (CUP_WIDTH * Levels::BLOCK_SIZE) / 2.0f;
      float ey = center.y - (CUP_HEIGHT * Levels::BLOCK_SIZE) / 2.0f;

      AddBlockRect(ex - Levels::BLOCK_SIZE, ey, 1, CUP_HEIGHT, color);
      AddBlockRect(ex + CUP_WIDTH * Levels::BLOCK_SIZE,
                   ey, 1, CUP_HEIGHT, color);
      AddBlockRect(ex - Levels::BLOCK_SIZE,
                   ey + CUP_HEIGHT * Levels::BLOCK_SIZE,
                   CUP_WIDTH + 2, 1, color);
    };

  AddCup(problem.output0, 0xFF0000FF);
  AddCup(problem.output1, 0x00FF00FF);

  float in_ex = problem.input.x - (IN_SIZE * Levels::BLOCK_SIZE) / 2.0f;
  float in_ey = problem.input.y - (IN_SIZE * Levels::BLOCK_SIZE) / 2.0f;

  // In region must have vertical walls.
  AddBlockRect(in_ex - Levels::BLOCK_SIZE,
               in_ey - Levels::BLOCK_SIZE, 1, IN_SIZE + 1, 0x00FFFFFF);
  AddBlockRect(in_ex + IN_SIZE * Levels::BLOCK_SIZE,
               in_ey - Levels::BLOCK_SIZE, 1, IN_SIZE + 1, 0x00FFFFFF);

  return bodies;
}


// Construct level geometry from the arguments.
static constexpr int NUM_TRIANGLES = 5;
static std::vector<LevelBody> ApplyArgs(std::span<const double> args) {
  std::vector<LevelBody> bodies;
  CHECK(args.size() == 6 * NUM_TRIANGLES);
  for (int i = 0; i < NUM_TRIANGLES; i++) {
    vec2 a{args[i * 6 + 0], args[i * 6 + 1]};
    vec2 b{args[i * 6 + 2], args[i * 6 + 3]};
    vec2 c{args[i * 6 + 4], args[i * 6 + 5]};

    double det = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (det == 0.0) {
      // Skip if degenerate.
      continue;
    }

    LevelBody body;
    body.color = 0x0000AAFF;
    body.mesh.vertices = {a, b, c};
    if (det < 0.0) {
      body.mesh.polygons = {{0, 1, 2}};
    } else {
      body.mesh.polygons = {{0, 2, 1}};
    }

    bodies.push_back(std::move(body));
  }

  return bodies;
}

ArcFour *rc = nullptr;
static constexpr int NUM_EVAL_THREADS = 8;
static constexpr int MAX_SIMULATE_STEPS = 1000;
static constexpr int NUM_TRIALS = 200;
struct Score {
  std::vector<vec2f> one_pos, zero_pos;
  int correct = 0;
  double distance_penalty = 0.0;
};

static double ScorePenalty(const Score &score) {
  return -2000.0 * score.correct + score.distance_penalty;
};

static Score ComputeScore(
    const Problem &problem, std::span<const double> args) {
  std::vector<LevelBody> bodies = InitialBodies(problem);
  for (LevelBody &body : ApplyArgs(args)) {
    bodies.emplace_back(std::move(body));
  }

  uint64_t seed = rc->Word64();
  std::mutex m;
  Score score;

  ParallelComp(
      NUM_TRIALS,
      [&](int sample) {
        // We don't bother copying inputs/outputs here; they aren't
        // needed to simulate.
        Level level{
          .bodies = bodies,
          .scene_walls = false,
        };

        PCG32 pcg(seed + sample);

        // Half of each.
        bool bit = sample & 1;

        LevelBody body = bit ? Levels::One() : Levels::Zero();
        body.color = bit ? 0x00FF00FF : 0xFF0000FF;
        body.dynamic = true;
        body.vel = vec2f(pcg.Double() * 2 - 1, pcg.Double() * 2 - 1);
        // between -1 and +1 radians per second.
        body.avel = pcg.Double() * 2 - 1;
        // XXX randomize angle

        float bitw = bit ? Levels::BLOCK_SIZE : 4.0 * Levels::BLOCK_SIZE;
        constexpr float bith = 4.0 * Levels::BLOCK_SIZE;

        float sample_width = ((IN_SIZE * Levels::BLOCK_SIZE) - bitw) * 0.98;
        float xoff =
          0.02f + bitw * 0.5f +
          pcg.Double() * sample_width;

        float sample_height = ((IN_SIZE * Levels::BLOCK_SIZE) - bith) * 0.098;
        float yoff =
          0.02f + bith * 0.5f +
          pcg.Double() * sample_height;

        float in_left = problem.input.x - (IN_SIZE * Levels::BLOCK_SIZE) / 2.0f;
        float in_top = problem.input.y - (IN_SIZE * Levels::BLOCK_SIZE) / 2.0f;

        body.pos = vec2f(in_left + xoff, in_top + yoff);

        level.bodies.emplace_back(std::move(body));

        std::unique_ptr<Scene> scene = Levels::CreateScene(level);

        b2BodyId body_id = scene->objects.back().body_id;

        for (int i = 0; i < MAX_SIMULATE_STEPS; i++) {
          scene->Update();
          if (!b2Body_IsAwake(body_id)) break;
        }

        b2Vec2 final_pos = b2Body_GetPosition(body_id);

        float target_x = bit ? problem.output1.x : problem.output0.x;
        float target_y = bit ? problem.output1.y : problem.output0.y;

        double dx = final_pos.x - target_x;
        double dy = final_pos.y - target_y;
        const double dist = std::sqrt(dx * dx + dy * dy);

        MutexLock ml(&m);
        if (bit) {
          score.one_pos.push_back(vec2f{final_pos.x, final_pos.y});
          float out1_left =
            problem.output1.x - (CUP_WIDTH * Levels::BLOCK_SIZE) / 2.0f;
          float out1_top =
            problem.output1.y - (CUP_HEIGHT * Levels::BLOCK_SIZE) / 2.0f;
          if (final_pos.x >= out1_left &&
              final_pos.x <= out1_left + CUP_WIDTH * Levels::BLOCK_SIZE &&
              final_pos.y >= out1_top &&
              final_pos.y <= out1_top + CUP_HEIGHT * Levels::BLOCK_SIZE) {
            score.correct++;
          } else {
            score.distance_penalty += dist;
          }
        } else {
          score.zero_pos.push_back(vec2f{final_pos.x, final_pos.y});
          float out0_left =
            problem.output0.x - (CUP_WIDTH * Levels::BLOCK_SIZE) / 2.0f;
          float out0_top =
            problem.output0.y - (CUP_HEIGHT * Levels::BLOCK_SIZE) / 2.0f;
          if (final_pos.x >= out0_left &&
              final_pos.x <= out0_left + CUP_WIDTH * Levels::BLOCK_SIZE &&
              final_pos.y >= out0_top &&
              final_pos.y <= out0_top + CUP_HEIGHT * Levels::BLOCK_SIZE) {
            score.correct++;
          } else {
            score.distance_penalty += dist;
          }
        }

      }, NUM_EVAL_THREADS);

  // TODO: Occasionally draw the result.

  // Lower is better.
  return score;
}

StatusBar *status = nullptr;

static std::pair<Problem, std::vector<double>>
FromSVG(std::string_view filename) {
  std::unique_ptr<Level> level = Levels::LoadSVG(filename);
  std::vector<double> args;
  for (const LevelBody &body : level->bodies) {
    if (body.color == 0x0000AAFF) {
      for (const std::vector<int> &poly : body.mesh.polygons) {
        CHECK(poly.size() == 3) << "Expected blue shapes to be triangles";
        for (int v : poly) {
          args.push_back(body.pos.x + body.mesh.vertices[v].x);
          args.push_back(body.pos.y + body.mesh.vertices[v].y);
        }
      }
    }
  }

  CHECK(args.size() == NUM_TRIANGLES * 6)
      << "Found " << (args.size() / 6) << " blue triangles, but expected "
      << NUM_TRIANGLES;

  CHECK(level->inputs.size() == 1) << "Expected exactly 1 input in SVG";
  CHECK(level->outputs.size() == 2) << "Expected exactly 2 outputs in SVG";

  Problem problem;
  problem.input = level->inputs[0];
  problem.output0 = level->outputs[0];
  problem.output1 = level->outputs[1];

  return {problem, args};
}

static std::pair<Problem, std::vector<double>> StartRoot() {
  std::vector<std::string> lines =
    Util::ReadFileToLines("best-separator.txt");
  std::vector<double> ret;
  if (lines.empty()) {
    // If we don't have a best one so far, just make up something
    // randomly.
    for (int i = 0; i < NUM_TRIANGLES; i++) {
      for (int v = 0; v < 3; v++) {
        ret.push_back(Levels::BLOCK_SIZE +
                      RandDouble(rc) * Levels::BLOCKS_ACROSS *
                      (Levels::BLOCK_SIZE - 2));
        ret.push_back(Levels::BLOCK_SIZE +
                      RandDouble(rc) * Levels::BLOCKS_DOWN *
                      (Levels::BLOCK_SIZE - 2));
      }
    }

  } else {
    for (const std::string &line : lines) {
      std::optional<double> od = Util::ParseDoubleOpt(line);
      CHECK(od.has_value()) << line;
      ret.push_back(od.value());
    }
  }

  CHECK(ret.size() == NUM_TRIANGLES * 6);

  Problem problem;
  problem.input = vec2f((IN_X + IN_SIZE / 2.0f) * Levels::BLOCK_SIZE,
                        (IN_Y + IN_SIZE / 2.0f) * Levels::BLOCK_SIZE);
  problem.output0 = vec2f((OUT0_X + CUP_WIDTH / 2.0f) * Levels::BLOCK_SIZE,
                          (OUT0_Y + CUP_HEIGHT / 2.0f) * Levels::BLOCK_SIZE);
  problem.output1 = vec2f((OUT1_X + CUP_WIDTH / 2.0f) * Levels::BLOCK_SIZE,
                          (OUT1_Y + CUP_HEIGHT / 2.0f) * Levels::BLOCK_SIZE);

  return {problem, ret};
}

std::string ScoreString(const Score &score) {
  return std::format("{}/{} correct + {:.4f}", score.correct, NUM_TRIALS,
                     score.distance_penalty);
}

static constexpr int MAX_ITERS_PER_ROUND = 1000;
static void Optimize(Problem start_problem,
                     std::vector<double> start_root) {
  Periodically status_per(1);
  Periodically flush_per(60);
  Timer timer;

  Problem problem = start_problem;
  std::vector<double> root = std::move(start_root);
  Score initial_score = ComputeScore(problem, root);
  double best_penalty = ScorePenalty(initial_score);
  std::vector<double> best_values = root;
  bool best_dirty = false;
  status->Print("Initial root score: {} penalty: {:.2f}\n",
                ScoreString(initial_score),
                best_penalty);

  for (;;) {
    std::vector<std::pair<double, double>> bounds;
    int idx = 0;
    for (int i = 0; i < NUM_TRIANGLES; i++) {
      for (int v = 0; v < 3; v++) {
        // x bounds
        bounds.push_back(
            {-root[idx],
             Levels::BLOCKS_ACROSS * Levels::BLOCK_SIZE - root[idx]});
        idx++;
        // y bounds
        bounds.push_back(
            {-root[idx],
             Levels::BLOCKS_DOWN * Levels::BLOCK_SIZE - root[idx]});
        idx++;
      }
    }

    status->Print("Starting optimization round...\n");
    OptSeq seq(bounds);

    for (int iter = 0; iter < MAX_ITERS_PER_ROUND; iter++) {
      std::vector<double> arg = seq.Next();
      for (size_t i = 0; i < arg.size(); i++) {
        arg[i] += root[i];
      }
      const Score score = ComputeScore(problem, arg);
      double penalty = ScorePenalty(score);
      seq.Result(penalty);
      ctr_total_evals++;

      if (penalty < best_penalty) {
        best_penalty = penalty;
        best_values = arg;
        best_dirty = true;
        status->Print("Iteration {}: New best {}; penalty: {}\n",
                      iter,
                      ScoreString(score),
                      best_penalty);
      }

      flush_per.RunIf([&]{
          if (best_dirty) {
            Level level;
            level.bodies = InitialBodies(problem);
            level.scene_walls = false;
            for (LevelBody &body : ApplyArgs(best_values)) {
              level.bodies.emplace_back(std::move(body));
            }

            level.inputs.push_back(problem.input);
            level.outputs.push_back(problem.output0);
            level.outputs.push_back(problem.output1);

            std::string file = std::format("best-separator-{}.svg",
                                           time(nullptr));
            Levels::SaveSVG(level, file);

            std::string bestfile;
            for (double d : best_values) {
              AppendFormat(&bestfile, "{:.17g}\n", d);
            }
            Util::WriteFile("best-separator.txt", bestfile);

            status->Print("Wrote " AGREEN("{}") " and "
                          ACYAN("best-separator.txt") "\n", file);
            best_dirty = false;
          }
        });

      status_per.RunIf([&]{
          std::string save;
          if (best_dirty) {
            save = std::format(" save in {}",
                               ANSI::Time(flush_per.SecondsLeft()));
          }
          int64_t total = ctr_total_evals.Read();
          double each = timer.Seconds() / total;
          status->Status(AGREY("----------------------------") "\n"
                         "{} iters ({} total), {:.2f} best penalty, {} ea.{}",
                         iter, total, best_penalty,
                         ANSI::Time(each), save);
        });
    }

    if (!best_values.empty()) {
      root = best_values;
    }
  }
}


int main(int argc, char* argv[]) {
  ANSI::Init();

  status = new StatusBar(2);
  rc = new ArcFour(std::format("separator.{}", time(nullptr)));

  Problem problem;
  std::vector<double> root;
  if (argc == 2) {
    std::tie(problem, root) = FromSVG(argv[1]);
  } else {
    std::tie(problem, root) = StartRoot();
  }
  Optimize(problem, root);

  return 0;
}
