
#include "llm.h"

#include <string>
#include <vector>

#include "util.h"
#include "periodically.h"
#include "re2/re2.h"
#include "timer.h"
#include "arcfour.h"
#include "randutil.h"
#include "ansi.h"
#include "image.h"
#include "color-util.h"

static constexpr int MAX_THREADS = 64;
// If the number of layers is too high to fit, performance drops off
// dramatically (or it will just fail). It's fine to increase this
// after tuning has started. You want to include a little past the
// performance drop off, or up to the total number of layers (which
// is printed at startup).
static constexpr int MAX_LAYERS_GPU = 34;

// Outside this region, sample less often.
static constexpr int HINT_MIN_THREADS = 4;
static constexpr int HINT_MAX_THREADS = 30;
static constexpr int HINT_MAX_GPU = 32;

struct Database {
  static constexpr int MODEL_LOAD = 0;
  static constexpr int SINGLE_TOKEN = 1;
  static constexpr int BATCH = 2;
  static constexpr int SAVE = 3;
  static constexpr int RESTORE = 4;
  static constexpr int NUM_FIELDS = 5;

  struct Cell {
    int trials = 0;
    // Sum across all trials.
    // Indexed by cosntants above.
    double results[NUM_FIELDS] = {};
  };

  // Major axis is num_gpu_layers.
  // Minor axis is num_threads.
  std::vector<Cell> cells;
  // The model name is just presentational; it gets written
  // as a comment in the database.
  std::string model;

  int MinNumTrials() const {
    int min_trials = cells[0].trials;
    for (const Cell &cell : cells)
      min_trials = std::min(min_trials, cell.trials);
    return min_trials;
  }

  int NumTrials(int num_threads,
                int num_layers_gpu) const {
    if (num_threads >= 0 && num_threads < MAX_THREADS &&
        num_layers_gpu >= 0 && num_layers_gpu < MAX_LAYERS_GPU) {
      return cells[num_layers_gpu * MAX_THREADS + num_threads].trials;
    } else {
      return 0;
    }
  }

  int TotalTrials() const {
    int total = 0;
    for (const Cell &cell : cells) total += cell.trials;
    return total;
  }

  void MergeCell(int num_threads,
                 int num_layers_gpu,
                 const Cell &cell) {
    if (num_threads >= 0 && num_threads < MAX_THREADS &&
        num_layers_gpu >= 0 && num_layers_gpu < MAX_LAYERS_GPU) {
      Cell &ocell = cells[num_layers_gpu * MAX_THREADS + num_threads];
      ocell.trials += cell.trials;
      for (int i = 0; i < NUM_FIELDS; i++) {
        ocell.results[i] += cell.results[i];
      }
    }
  }

  void SetModelName(std::string model_in) {
    model = std::move(model_in);
  }

  void Save(const std::string &tunefile) const {
    std::string contents =
      StringPrintf("# %s\n", model.c_str());

    for (int y = 0; y < MAX_LAYERS_GPU; y++) {
      for (int x = 0; x < MAX_THREADS; x++) {
        const Cell &cell = cells[y * MAX_THREADS + x];
        if (cell.trials > 0) {
          StringAppendF(&contents,
                        "%d %d %d %.17g %.17g %.17g %.17g %.17g\n",
                        x, y,
                        cell.trials,
                        cell.results[MODEL_LOAD],
                        cell.results[SINGLE_TOKEN],
                        cell.results[BATCH],
                        cell.results[SAVE],
                        cell.results[RESTORE]);
        }
      }
    }
    Util::WriteFile(tunefile, contents);
  }

  // Lower times are better.
  static constexpr ColorUtil::Gradient BEST_WORST{
    GradRGB(0.00f, 0xFFFFFF),
    GradRGB(0.01f, 0xFFFF00),
    GradRGB(0.05f, 0x00FF00),
    GradRGB(0.50f, 0x2222FF),
    GradRGB(1.00f, 0xFF0000),
  };

  void SaveImages(const std::string &load,
                  const std::string &single,
                  const std::string &batch,
                  const std::string &save,
                  const std::string &restore) {
    auto MakeOneImage = [this](const char *name,
                               const std::string &file,
                               int field) {
        if (file.empty()) return;
        int RC = 2;
        ImageRGBA img(MAX_THREADS + 1 + RC, MAX_LAYERS_GPU + 1 + RC);
        img.Clear32(0x000000FF);

        // Get all values so we can compute rank.
        std::vector<double> values;

        for (const Cell &cell : cells) {
          if (cell.trials > 0) {
            double f = cell.results[field] / cell.trials;
            values.push_back(f);
          }
        }

        // No data...
        if (values.empty()) return;

        std::sort(values.begin(), values.end());

        // Get a value's rank as a number in [0, 1].
        auto Rank = [&values](double v) {
            const int idx = std::lower_bound(values.begin(),
                                             values.end(),
                                             v) - values.begin();
            return std::clamp(idx / (double)values.size(), 0.0, 1.0);
          };

        auto MapValue = [&Rank](double v) {
            // (v - minv.value()) / range;
            return Rank(v);
          };

        for (int y = 0; y < MAX_LAYERS_GPU; y++) {
          for (int x = 0; x < MAX_THREADS; x++) {
            const Cell &cell = cells[y * MAX_THREADS + x];
            if (cell.trials > 0) {
              double v = cell.results[field] / cell.trials;
              double f = MapValue(v);

              uint32_t color = ColorUtil::LinearGradient32(
                  BEST_WORST, f);
              img.SetPixel32(x, y, color);
            }
          }
        }

        // Now averages of rows
        for (int y = 0; y < MAX_LAYERS_GPU; y++) {
          double num = 0.0;
          int den = 0;
          for (int x = 0; x < MAX_THREADS; x++) {
            const Cell &cell = cells[y * MAX_THREADS + x];
            if (cell.trials > 0) {
              num += cell.results[field] / cell.trials;
              den++;
            }
          }

          if (den > 0) {
            double v = num / den;
            double f = MapValue(v);

            uint32_t color = ColorUtil::LinearGradient32(
                BEST_WORST, f);
            for (int i = 0; i < RC; i++) {
              img.SetPixel32(MAX_THREADS + 1 + i, y, color);
            }
          }
        }

        // And columns
        for (int x = 0; x < MAX_THREADS; x++) {
          double num = 0.0;
          int den = 0;
          for (int y = 0; y < MAX_LAYERS_GPU; y++) {
            const Cell &cell = cells[y * MAX_THREADS + x];
            if (cell.trials > 0) {
              num += cell.results[field] / cell.trials;
              den++;
            }
          }

          if (den > 0) {
            double v = num / den;
            double f = MapValue(v);

            uint32_t color = ColorUtil::LinearGradient32(
                BEST_WORST, f);
            for (int i = 0; i < RC; i++) {
              img.SetPixel32(x, MAX_LAYERS_GPU + 1 + i, color);
            }
          }
        }

        int SCALE = 11;
        ImageRGBA scaled = img.ScaleBy(SCALE);
        // label
        for (int y = 0; y < MAX_LAYERS_GPU; y++) {
          scaled.BlendText32(
              (MAX_THREADS + 1) * SCALE + 1,
              y * SCALE + 1, 0x000000AA,
              StringPrintf("%02d", y));
        }

        for (int x = 0; x < MAX_THREADS; x++) {
          scaled.BlendTextVert32(
              x * SCALE - 1,
              (MAX_LAYERS_GPU + 1) * SCALE + 1,
              false, 0x000000AA,
              StringPrintf("%02d", x));
        }

        for (int y = 0; y < MAX_LAYERS_GPU; y++) {
          for (int x = 0; x < MAX_THREADS; x++) {
            const Cell &cell = cells[y * MAX_THREADS + x];
            if (cell.trials > 0) {
              scaled.BlendText32(
                  x * SCALE + 1, y * SCALE + 2,
                  0x00000033,
                  StringPrintf("%d",
                               std::clamp(cell.trials, 0, 9)));
            }
          }
        }

        constexpr int HEADER_LINES = 3;
        int TOP = HEADER_LINES * (ImageRGBA::TEXT_HEIGHT + 2) + 4;
        ImageRGBA full(scaled.Width(),
                       scaled.Height() + TOP);
        full.Clear32(0x000033FF);
        full.CopyImage(0, TOP, scaled);

        int yy = 1;
        full.BlendText32(
            1, yy, 0xCCCCCCFF,
            StringPrintf("Model: %s.", model.c_str()));

        const int total = TotalTrials();
        yy += ImageRGBA::TEXT_HEIGHT + 2;
        full.BlendText32(
            1, yy, 0xFFFFFFFF,
            StringPrintf("%s. x: threads. y: gpu layers. "
                         "%d total trials",
                         name, total));

        auto GetQuantile = [&values](float f) -> double {
            int idx = std::clamp((int)std::round(f * values.size()),
                                 0,
                                 (int)values.size() - 1);
            return values[idx];
          };

        int xx = 1;
        yy += ImageRGBA::TEXT_HEIGHT + 2;
        for (float f : {0.0, 0.125, 0.25, 0.375, 0.50,
                        0.625, 0.75, 0.875, 1.0}) {
          std::string value = StringPrintf("%.4f", GetQuantile(f));
          uint32_t color = ColorUtil::LinearGradient32(BEST_WORST, f);
          full.BlendText32(xx, yy, color, value);
          xx += (value.size() + 2) * ImageRGBA::TEXT_WIDTH;
        }

        full.Save(file);
        printf("Wrote " AGREEN("%s") "\n", file.c_str());
      };

    MakeOneImage("load", load, Database::MODEL_LOAD);
    MakeOneImage("single", single, Database::SINGLE_TOKEN);
    MakeOneImage("batch", batch, Database::BATCH);
    MakeOneImage("save", save, Database::SAVE);
    MakeOneImage("restore", restore, Database::RESTORE);
  }

  void Load(const std::string &tunefile) {
    Reset();

    std::vector<std::string> lines = Util::ReadFileToLines(tunefile);
    for (std::string &line : lines) {
      line = Util::NormalizeWhitespace(line);
      if (line.empty()) continue;
      if (line[0] == '#') continue;

      Cell cell;
      int x, y;
      #define INT "([0-9]+)"
      #define WS "[ \t]+"
      #define FLOAT "([0-9.]+)"

      if (line.empty()) continue;
      CHECK(RE2::FullMatch(line,
                           INT WS INT WS
                           INT WS
                           FLOAT WS FLOAT WS FLOAT WS FLOAT WS FLOAT,
                           &x, &y,
                           &cell.trials,
                           &cell.results[MODEL_LOAD],
                           &cell.results[SINGLE_TOKEN],
                           &cell.results[BATCH],
                           &cell.results[SAVE],
                           &cell.results[RESTORE])) << line;
      MergeCell(x, y, cell);
    }

  }

  // Doesn't reset the model name.
  void Reset() {
    cells.clear();
    cells.resize(MAX_LAYERS_GPU * MAX_THREADS);
  }

};

static Database::Cell RunOne(const std::string &model,
                             int num_threads,
                             int num_gpu_layers) {

  printf("\n\n"
         ABGCOLOR(80, 10, 80,
                  AFGCOLOR(255, 255, 120,
                           " == Run %d threads, %d gpu == ")) "\n\n",
         num_threads, num_gpu_layers);
  Timer expt_timer;

  Database::Cell cell;
  cell.trials = 1;

  ContextParams cparams;
  cparams.model = model;
  // We don't use zero threads.
  cparams.num_threads = num_threads + 1;
  cparams.num_gpu_layers = num_gpu_layers;

  SamplerParams sparams;
  sparams.type = SampleType::MIROSTAT_2;
  sparams.regex = ".*";
  sparams.seed = 0xCAFE;

  Timer load_timer;
  LLM llm(cparams, sparams);

  // We consider this part of model loading, because some loading
  // may be lazy.
  llm.DoPrompt("Go.", false);
  cell.results[Database::MODEL_LOAD] = load_timer.Seconds();

  Timer save_timer;
  LLM::State state = llm.SaveState();
  cell.results[Database::SAVE] = save_timer.Seconds();

  // Now some batch inference.
  Timer batch_timer;
  llm.InsertString(
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
      "sed do eiusmod tempor incididunt ut labore et dolore magna "
      "aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
      "ullamco laboris nisi ut aliquip ex ea commodo consequat. "
      "Duis aute irure dolor in reprehenderit in voluptate velit "
      "esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
      "occaecat cupidatat non proident, sunt in culpa qui officia "
      "deserunt mollit anim id est laborum.",
      true);
  cell.results[Database::BATCH] = batch_timer.Seconds();

  Timer restore_timer;
  llm.LoadState(state);
  cell.results[Database::RESTORE] = restore_timer.Seconds();

  // Serial inference. We predict a few to get a better average for
  // steady-state inference.
  Timer infer_timer;
  constexpr int NUM_SERIAL = 12;
  for (int i = 0; i < NUM_SERIAL; i++) {
    std::string tok = llm.SampleAndTake();
    printf("[%s]", tok.c_str());
  }
  cell.results[Database::SINGLE_TOKEN] = infer_timer.Seconds() / NUM_SERIAL;

  printf(" == Finished in %s == \n",
         ANSI::Time(expt_timer.Seconds()).c_str());

  return cell;
}

static int GetModelLayers(const std::string &model) {
  ContextParams cparams;
  cparams.model = model;
  // We're just going to throw it away, so use the fastest method.
  cparams.num_threads = 6;
  cparams.num_gpu_layers = 0;

  SamplerParams sparams;

  Timer load_timer;
  LLM llm(cparams, sparams);
  auto md = llm.GetModelMetadata();
  for (const auto &[k, v] : md) {
    printf(AWHITE("%s") ": %s\n", k.c_str(), v.c_str());
  }
  CHECK(md.contains("llama.block_count"));
  int layers = atoi(md["llama.block_count"].c_str());
  CHECK(layers > 0);
  return layers;
}

static void Tune(const std::string &model, const std::string &tunefile) {

  // The main parameters we can tune are:
  // num_gpu_layers
  // num_threads
  // num_threads_batch

  // And the things we want to measure are:
  // - Loading the model
  // - Single token inference
  // - Batch decoding (BATCH_SIZE tokens)
  // - Context save time
  // - Context restore time

  // We assume that the thread parameters only affect the corresponding
  // inference type, so we set them both together during experiments,
  // but pick the optimal values separately.

  // We only need to try num_gpu_layers from 0 to the number of layers
  // in the model (this is 81 for llama2 70b), and num_threads for
  // like [0, 64]. So we can actually run every cell of the experiment,
  // which will also help us visualize.

  const int actual_num_layers = GetModelLayers(model);
  printf("Model " ABLUE("%s") " appears to have " AYELLOW("%d")
         " actual layers.\n", model.c_str(), actual_num_layers);

  Database db;
  db.SetModelName(model);
  db.Load(tunefile);
  printf("Loaded " AGREEN("%d") " previous trials.\n",
         db.TotalTrials());

  ArcFour rc(StringPrintf("tune.%lld", time(nullptr)));

  // Run one for warm start.
  (void)RunOne(model, 16, 3);

  Periodically save_per(60.0);

  std::vector<std::pair<int, int>> todo;
#if 1
  todo.emplace_back(14, 32);
  // Redo the best region, to reduce noise
  for (int th = 10; th <= 18; th++) {
    for (int gpu = 31; gpu <= 32; gpu++) {
      if (true || db.NumTrials(th, gpu) == 0)
        todo.emplace_back(th, gpu);
    }
  }
#endif
  Shuffle(&rc, &todo);

  const int max_gpu_sample =
    std::min(MAX_LAYERS_GPU, actual_num_layers);

  Timer run_timer;
  for (;;) {
    int min_trials = db.MinNumTrials();
    if (min_trials == 3) break;

    bool force = false;
    int th, gpu;
    if (todo.empty()) {
      // PERF: Should not do random samples once the grid is
      // pretty full.
      th = RandTo(&rc, MAX_THREADS);
      gpu = RandTo(&rc, max_gpu_sample + 1);

      if (gpu > HINT_MAX_GPU && RandTo(&rc, 5) > 0) continue;
      if (gpu > HINT_MAX_GPU + 2 && RandTo(&rc, 20) > 0) continue;
      if (th < HINT_MIN_THREADS && RandTo(&rc, 5) > 0) continue;
      if (th > HINT_MAX_THREADS && RandTo(&rc, 5) > 0) continue;
    } else {
      std::tie(th, gpu) = todo.back();
      todo.pop_back();
      force = true;
    }

    printf("Run %d,%d which has %d already.\n",
           th, gpu, db.NumTrials(th, gpu));

    if (force || db.NumTrials(th, gpu) == min_trials) {
      Database::Cell cell = RunOne(model, th, gpu);
      db.MergeCell(th, gpu, cell);
    }


    if (save_per.ShouldRun()) {
      printf("Total trials: " AYELLOW("%d") "/" AWHITE("%d") " in %s\n",
             db.TotalTrials(),
             2 * (max_gpu_sample + 1) * MAX_THREADS,
             ANSI::Time(run_timer.Seconds()).c_str());
      db.Save(tunefile);
      db.SaveImages("tune-load.png",
                    "tune-single.png",
                    "tune-batch.png",
                    "tune-save.png",
                    "tune-restore.png");
    }
  }

  db.Save(tunefile);
}

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc >= 2) << "./tune.exe modelfile tune-data.txt\n"
    "Tune inference parameters for a model.";

  std::string model = argv[1];
  std::string tunefile = "tunefile.txt";
  if (argc >= 3) tunefile = argv[2];
  Tune(model, tunefile);

  return 0;
}
