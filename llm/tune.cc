
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
static constexpr int MAX_LAYERS_GPU = 32;

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

  int MinNumTrials() const {
    int min_trials = cells[0].trials;
    for (const Cell &cell : cells)
      min_trials = std::min(min_trials, cell.trials);
    return min_trials;
  }

  int NumTrials(int num_threads,
                int num_layers_gpu) {
    if (num_threads >= 0 && num_threads < MAX_THREADS &&
        num_layers_gpu >= 0 && num_layers_gpu < MAX_LAYERS_GPU) {
      return 0;
    } else {
      return cells[num_layers_gpu * MAX_THREADS + num_threads].trials;
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

  void Save(const std::string &tunefile) const {
    std::string contents;
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
    GradRGB(0.0f, 0x00FF00),
    GradRGB(1.0f, 0xFF0000),
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
        ImageRGBA img(MAX_THREADS, MAX_LAYERS_GPU);
        img.Clear32(0x000000FF);

        // Get min/max
        std::optional<double> minv, maxv;
        for (const Cell &cell : cells) {
          if (cell.trials > 0) {
            double f = cell.results[field] / cell.trials;
            minv = std::min(f, minv.value_or(f));
            maxv = std::max(f, maxv.value_or(f));
          }
        }

        // No data?
        if (!minv.has_value()) return;

        const double range = maxv.value() - minv.value();

        for (int y = 0; y < MAX_LAYERS_GPU; y++) {
          for (int x = 0; x < MAX_THREADS; x++) {
            const Cell &cell = cells[y * MAX_THREADS + x];
            if (cell.trials > 0) {
              double v = cell.results[field] / cell.trials;
              double f = (v - minv.value()) / range;

              uint32_t color = ColorUtil::LinearGradient32(
                  BEST_WORST, f);
              img.SetPixel32(x, y, color);
            }
          }
        }

        int SCALE = 8;
        int TOP = 36;
        ImageRGBA full(img.Width() * SCALE + TOP,
                       img.Height() * SCALE);
        full.Clear32(0x000033FF);
        full.CopyImage(0, TOP, img.ScaleBy(SCALE));

        full.BlendText32(1, 1, 0xFFFFFFFF,
                         StringPrintf("%s. x: num_threads. y: num_gpu_layers",
                                      name));

        int xx = 1;
        for (float f : {0.0, 0.25, 0.55, 0.75, 1.0}) {
          std::string value = StringPrintf("%.4f", f * range + minv.value());
          uint32_t color = ColorUtil::LinearGradient32(BEST_WORST, f);
          full.BlendText32(xx, ImageRGBA::TEXT_HEIGHT + 2, color, value);
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
                           " == Run %d gpu, %d threads == ")) "\n\n",
         num_gpu_layers, num_threads);
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


  Database db;
  db.Load(tunefile);

  ArcFour rc("tune");

  // Run one for warm start.
  (void)RunOne(model, 16, 3);

  Periodically save_per(120.0);

  Timer run_timer;
  while (db.MinNumTrials() < 2) {
    // PERF: Should not do random samples once the grid is
    // pretty full.
    int gpu = RandTo(&rc, MAX_LAYERS_GPU);
    int th = RandTo(&rc, MAX_THREADS);

    if (db.NumTrials(th, gpu) < 2) {
      Database::Cell cell = RunOne(model, th, gpu);
      db.MergeCell(th, gpu, cell);
    }

    if (save_per.ShouldRun()) {
      printf("Total trials: " AYELLOW("%d") "/" AWHITE("%d") " in %s\n",
             db.TotalTrials(),
             2 * MAX_LAYERS_GPU * MAX_THREADS,
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
