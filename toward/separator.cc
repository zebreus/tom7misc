
#include <cmath>
#include <ctime>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
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
#include "yocto-math.h"
#include "atomic-util.h"

DECLARE_COUNTERS(ctr_total_evals);

// The input region is an empty 5x5 block spot.
static constexpr int IN_X = 20;
static constexpr int IN_Y = 1;
static constexpr int IN_SIZE = 5;

static constexpr int CUP_WIDTH = 5;
static constexpr int CUP_HEIGHT = 7;

// Out cup for 0.
static constexpr int OUT0_X = 10;
static constexpr int OUT0_Y = Levels::BLOCKS_DOWN - CUP_HEIGHT - 1;

// Out cup for 1.
static constexpr int OUT1_X = 30;
static constexpr int OUT1_Y = Levels::BLOCKS_DOWN - CUP_HEIGHT - 1;

// Bodies that are always in the scene.
static std::vector<LevelBody> InitialBodies() {
  std::vector<LevelBody> bodies;

  auto AddBlockRect = [&bodies](int x, int y, int w, int h,
                                uint32_t color) {
      LevelBody body;
      body.color = color;
      body.pos = vec2f(x * Levels::BLOCK_SIZE, y * Levels::BLOCK_SIZE);
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

  auto AddCup = [&](int x, int y, uint32_t color) {
      AddBlockRect(x - 1, y, 1, CUP_HEIGHT, color);
      AddBlockRect(x + CUP_WIDTH, y, 1, CUP_HEIGHT, color);
      AddBlockRect(x - 1, y + CUP_HEIGHT,
                   CUP_WIDTH + 2, 1, color);
    };

  AddCup(OUT0_X, OUT0_Y, 0xFF0000FF);
  AddCup(OUT1_X, OUT1_Y, 0x00FF00FF);

  // In region must have vertical walls.
  AddBlockRect(IN_X - 1, IN_Y - 1, 1, IN_SIZE + 1, 0x00FFFFFF);
  AddBlockRect(IN_X + IN_SIZE, IN_Y - 1, 1, IN_SIZE + 1, 0x00FFFFFF);

  return bodies;
}

static LevelBody BitBody(bool b) {
  LevelBody body = b ? Levels::One() : Levels::Zero();
  body.pos = vec2f((IN_X + IN_SIZE / 2.0f) * Levels::BLOCK_SIZE,
                   (IN_Y + IN_SIZE / 2.0f) * Levels::BLOCK_SIZE);
  body.color = b ? 0x00FF00FF : 0xFF0000FF;
  body.dynamic = true;
  return body;
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
static double Score(std::span<const double> args) {
  std::vector<LevelBody> bodies = InitialBodies();
  for (LevelBody &body : ApplyArgs(args)) {
    bodies.emplace_back(std::move(body));
  }

  uint64_t seed = rc->Word64();
  std::mutex m;
  std::vector<vec2f> one_pos, zero_pos;
  int correct = 0;
  double distance_penalty = 0.0;

  ParallelComp(
      100,
      [&](int sample) {
        Level level{
          .bodies = bodies,
        };

        PCG32 pcg(seed + sample);

        // Half of each.
        bool bit = sample & 1;

        LevelBody body = bit ? Levels::One() : Levels::Zero();
        body.color = bit ? 0x00FF00FF : 0xFF0000FF;
        body.dynamic = true;
        // XXX randomize velocity and angular velocity
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

        body.pos = vec2f(IN_X * Levels::BLOCK_SIZE + xoff,
                         // randomize y offset too
                         IN_Y * Levels::BLOCK_SIZE + yoff);

        level.bodies.emplace_back(std::move(body));

        std::unique_ptr<Scene> scene = Levels::CreateScene(level);

        b2BodyId body_id = scene->objects.back().body_id;

        for (int i = 0; i < MAX_SIMULATE_STEPS; i++) {
          scene->Update();
          if (!b2Body_IsAwake(body_id)) break;
        }

        b2Vec2 final_pos = b2Body_GetPosition(body_id);

        float target_x = (bit ? OUT1_X : OUT0_X) + CUP_WIDTH * 0.5f;
        target_x *= Levels::BLOCK_SIZE;
        float target_y = (bit ? OUT1_Y : OUT0_Y) + CUP_HEIGHT * 0.5f;
        target_y *= Levels::BLOCK_SIZE;

        double dx = final_pos.x - target_x;
        double dy = final_pos.y - target_y;
        double dist = std::sqrt(dx * dx + dy * dy);

        MutexLock ml(&m);
        distance_penalty += dist;
        if (bit) {
          one_pos.push_back(vec2f{final_pos.x, final_pos.y});
          if (final_pos.x >= OUT1_X * Levels::BLOCK_SIZE &&
              final_pos.x <= (OUT1_X + CUP_WIDTH) * Levels::BLOCK_SIZE &&
              final_pos.y >= OUT1_Y * Levels::BLOCK_SIZE &&
              final_pos.y <= (OUT1_Y + CUP_HEIGHT) * Levels::BLOCK_SIZE) {
            correct++;
          }
        } else {
          zero_pos.push_back(vec2f{final_pos.x, final_pos.y});
          if (final_pos.x >= OUT0_X * Levels::BLOCK_SIZE &&
              final_pos.x <= (OUT0_X + CUP_WIDTH) * Levels::BLOCK_SIZE &&
              final_pos.y >= OUT0_Y * Levels::BLOCK_SIZE &&
              final_pos.y <= (OUT0_Y + CUP_HEIGHT) * Levels::BLOCK_SIZE) {
            correct++;
          }
        }

      }, NUM_EVAL_THREADS);

  // TODO: Occasionally draw the result.

  // Lower is better.
  return -2000.0 * correct + distance_penalty;
}

StatusBar *status = nullptr;

static std::vector<double> StartRoot() {
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
  return ret;
}

static constexpr int MAX_ITERS_PER_ROUND = 1000;
static void Optimize() {
  Periodically status_per(1);
  Periodically flush_per(60);
  Timer timer;

  std::vector<double> root = StartRoot();
  double best_score = Score(root);
  std::vector<double> best_values = root;
  bool best_dirty = false;
  status->Print("Initial root score: {}\n", best_score);

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
      double score = Score(arg);
      seq.Result(score);
      ctr_total_evals++;

      if (score < best_score) {
        best_score = score;
        best_values = arg;
        best_dirty = true;
        status->Print("Iteration {}: New best score: {}\n", iter, best_score);
      }

      flush_per.RunIf([&]{
          if (best_dirty) {
            Level level;
            level.bodies = InitialBodies();
            for (LevelBody &body : ApplyArgs(best_values)) {
              level.bodies.emplace_back(std::move(body));
            }

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
                         "{} iters ({} total), {} best score, {} ea.{}",
                         iter, total, best_score,
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

  Optimize();

  return 0;
}
