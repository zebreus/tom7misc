// Network benchmark: Learning a chess evaluation function
// from pre-labeled positions (e.g. by Stockfish). This is
// forked from ../pluginvert/train-eval.cc.

#include "network-gpu.h"

#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <ctype.h>
#include <chrono>
#include <thread>
#include <deque>
#include <numbers>
#include <tuple>
#include <unordered_map>

#include "chess.h"
#include "pgn.h"
#include "bigchess.h"
#include "network.h"
#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "image.h"
#include "util.h"
#include "train-util.h"
#include "error-history.h"
#include "timer.h"
#include "periodically.h"
#include "ansi.h"

#include "nnchess.h"

using namespace std;

static CL *cl = nullptr;

using int64 = int64_t;
using uint64 = uint64_t;
using uint8 = uint8_t;

static constexpr WeightUpdate WEIGHT_UPDATE = ADAM;

static constexpr const char *GAME_PGN = "d:\\chess\\lichess_db_standard_rated_2020-06.pgn";

#define MODEL_BASE "chess"
#define MODEL_NAME MODEL_BASE ".val"
static constexpr int EXAMPLES_PER_ROUND = 2048;

struct TrainParams {
  UpdateConfig update_config = {};

  bool do_decay = false;
  // XXX this should probably depend on the learning rate; if the
  // learning rate is too small, it won't even be able to overcome
  // the decay
  float decay_rate = 0.999999f;

  string ToString() const {
    return StringPrintf("{.update_config = %s, "
                        ".do_decay = %s, "
                        ".decay_rate = %.11g}",
                        update_config.ToString().c_str(),
                        do_decay ? "true" : "false",
                        decay_rate);
  }
};

static TrainParams DefaultTrainParams() {
  UpdateConfig update_config = {};

  // These were values I used for the initial (not hyper-tuned)
  // experiments.
  update_config.base_learning_rate = 0.01f;
  update_config.clipping = true;
  update_config.clip_error = true;
  update_config.error_max = 0.1f;
  update_config.adam_epsilon = 1.0e-4;

  TrainParams tparams = {};
  tparams.update_config = update_config;
  return tparams;
}

static double Train(const string &dir, Network *net, int64 max_rounds,
                    bool no_save,
                    TrainParams params,
                    ExamplePool *example_pool) {

  ArcFour rc(StringPrintf("%lld.train.%s", time(nullptr), MODEL_BASE));

  const string error_history_file = Util::dirplus(dir, "error-history.tsv");
  const string model_file = Util::dirplus(dir, MODEL_NAME);

  ErrorHistory error_history(error_history_file);

  static constexpr int max_parallelism = 4;
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  const bool SAVE_INTERMEDIATE = !no_save && true;

  // On a verbose round we compute training error and print out
  // examples.
  const int VERBOSE_EVERY_SEC = 60.0;
  Periodically verbose_per(VERBOSE_EVERY_SEC);

  // How often to also save error history to disk (if we run a
  // verbose round).
  const double HISTORY_EVERY_SEC = no_save ? 999999.0 : 120.0;
  Periodically history_per(HISTORY_EVERY_SEC);

  const double SAVE_ERROR_IMAGES_EVERY = no_save ? 999999.0 : 130.0;
  Periodically error_images_per(SAVE_ERROR_IMAGES_EVERY);

  const double TIMING_EVERY_SEC = 20.0;
  Periodically timing_per(TIMING_EVERY_SEC);

  static constexpr int64 CHECKPOINT_EVERY_ROUNDS = 100000;

  const int IMAGE_EVERY = 100;
  TrainingImages images(*net, Util::dirplus(dir, "train"),
                        model_file, IMAGE_EVERY);

  constexpr int NUM_COLUMNS = 1;
  std::array<const char *, NUM_COLUMNS> COLUMN_NAMES = {"score"};
  std::vector<std::unique_ptr<ErrorImage>> error_images;
  for (int i = 0; i < NUM_COLUMNS; i++) {
    string filename = Util::dirplus(dir, StringPrintf("error-%d.png", i));
    error_images.emplace_back(
        std::make_unique<ErrorImage>(2000, EXAMPLES_PER_ROUND, filename));
  }

  printf("Training!\n");

  auto net_gpu = make_unique<NetworkGPU>(cl, net);

  if (no_save) net_gpu->SetVerbose(false);

  std::unique_ptr<ForwardLayerCL> forward_cl =
    std::make_unique<ForwardLayerCL>(cl, net_gpu.get());
  std::unique_ptr<SetOutputErrorCL> error_cl =
    std::make_unique<SetOutputErrorCL>(cl, net_gpu.get(),
                                       params.update_config);
  std::unique_ptr<BackwardLayerCL> backward_cl =
    std::make_unique<BackwardLayerCL>(cl, net_gpu.get(),
                                      params.update_config);
  std::unique_ptr<DecayWeightsCL> decay_cl =
    std::make_unique<DecayWeightsCL>(cl, net_gpu.get(),
                                     params.decay_rate);
  std::unique_ptr<UpdateWeightsCL> update_cl =
    std::make_unique<UpdateWeightsCL>(cl, net_gpu.get(),
                                      EXAMPLES_PER_ROUND,
                                      params.update_config);

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(EXAMPLES_PER_ROUND, cl, *net));

  CHECK(!net->layers.empty());
  CHECK(net->layers[0].num_nodes == NNChess::INPUT_SIZE);
  CHECK(net->layers.back().num_nodes == NNChess::OUTPUT_SIZE);

  static constexpr double SAVE_EVERY_SEC = 180.0;
  Periodically save_per(SAVE_EVERY_SEC);

  double round_ms = 0.0;
  double example_ms = 0.0;
  double forward_ms = 0.0;
  double error_ms = 0.0;
  double decay_ms = 0.0;
  double backward_ms = 0.0;
  double update_ms = 0.0;
  double loss_ms = 0.0;
  double image_ms = 0.0;

  Timer train_timer;
  int64 total_examples = 0LL;

  double last_error = 0.0;
  for (int iter = 0; net->rounds < max_rounds; iter++) {
    Timer round_timer;

    const bool last_round = net->rounds == max_rounds - 1;
    const bool verbose_round = last_round || verbose_per.ShouldRun();

    // Initialize training examples.

    std::vector<float> inputs;
    inputs.resize(EXAMPLES_PER_ROUND * NNChess::INPUT_SIZE);
    std::vector<float> expecteds;
    expecteds.resize(EXAMPLES_PER_ROUND * NNChess::OUTPUT_SIZE);

    {
      // Examples from database.
      Timer example_timer;

      static constexpr int MIN_EXAMPLES = 1000000;
      const int64 num_available = [example_pool](){
          for (;;) {
            {
              MutexLock ml(&example_pool->pool_mutex);
              if (example_pool->pool.size() >= MIN_EXAMPLES) {
                return example_pool->pool.size();
              }
            }
            printf("Waiting until we have enough examples..\n");
            std::this_thread::sleep_for(2s);
          }
        }();

      {
        MutexLock ml(&example_pool->pool_mutex);
        for (int i = 0; i < EXAMPLES_PER_ROUND; i++) {
          const int64 idx = RandTo(&rc, num_available);
          const auto &[pos, score, over] = example_pool->pool[idx];
          if (i == 0 && verbose_round) {
            printf("Eval %.5f Over %.3f:\n"
                   "%s"
                   "%s\n\n",
                   score, over, pos.UnicodeAnsiBoardString().c_str(),
                   pos.ToFEN(1, 1).c_str());
          }

          // We train only on white-to-move, so flip the
          // board (and the eval) if it's black's move.
          if (pos.BlackMove()) {
            Position rev = Position::FlipSides(pos);
            NNChess::BoardVecTo(rev, &inputs, NNChess::INPUT_SIZE * i);
            expecteds[i * NNChess::OUTPUT_SIZE + 0] = -score;
          } else {
            NNChess::BoardVecTo(pos, &inputs, NNChess::INPUT_SIZE * i);
            expecteds[i * NNChess::OUTPUT_SIZE + 0] = score;
          }
        }
      }


      CHECK(inputs.size() == NNChess::INPUT_SIZE * EXAMPLES_PER_ROUND);
      CHECK(expecteds.size() == NNChess::OUTPUT_SIZE * EXAMPLES_PER_ROUND);
      training->LoadInputs(inputs);
      training->LoadExpecteds(expecteds);
      example_ms += example_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Prepped examples.\n");

    {
      Timer forward_timer;
      for (int src_layer = 0;
           src_layer < net->layers.size() - 1;
           src_layer++) {
        forward_cl->RunForward(training.get(), src_layer);
      }
      forward_ms += forward_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Forward done.\n");

    {
      Timer error_timer;
      error_cl->SetOutputError(training.get());
      error_ms += error_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Set error.\n");

    {
      Timer backward_timer;
      for (int dst_layer = net->layers.size() - 1;
           // Don't propagate to input.
           dst_layer > 1;
           dst_layer--) {
        backward_cl->BackwardLayer(training.get(), dst_layer);
      }
      backward_ms += backward_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Backward pass.\n");

    if (params.do_decay) {
      Timer decay_timer;
      for (int layer_idx = 0; layer_idx < net->layers.size(); layer_idx++) {
        decay_cl->Decay(layer_idx);
      }
      decay_ms += decay_timer.MS();
    }

    {
      Timer update_timer;
      // Can't run training examples in parallel because these all write
      // to the same network. But each layer is independent.
      UnParallelComp(net->layers.size() - 1,
                     [&](int layer_minus_1) {
                       const int layer_idx = layer_minus_1 + 1;
                       update_cl->Update(training.get(),
                                         layer_idx);
                     },
                     max_parallelism);
      update_ms += update_timer.MS();
    }

    if (VERBOSE > 1)
      printf("Updated weights.\n");

    total_examples += EXAMPLES_PER_ROUND;

    net->examples += EXAMPLES_PER_ROUND;
    net->rounds++;

    if (verbose_round) {
      Timer loss_timer;
      if (VERBOSE > 1)
        printf("Verbose round...\n");

      const double total_sec = train_timer.Seconds();
      const double eps = total_examples / total_sec;
      const double rpm = iter / (total_sec / 60.0);

      printf(AYELLOW("%lld") " rounds "
             AWHITE("%d") " iter: (" ABLUE("%.2f") " rounds/min;  "
             APURPLE("%.2f") " eps)\n",
             net->rounds, iter, rpm, eps);

      // Get loss as abs distance, plus number of incorrect (as booleans).
      std::vector<float> actuals(EXAMPLES_PER_ROUND * NNChess::OUTPUT_SIZE);
      training->ExportOutputs(&actuals);

      const bool save_history = !no_save && history_per.ShouldRun();
      const bool save_error_images = !no_save && error_images_per.ShouldRun();
      // compute loss for the two outputs on each example, actuals vs expecteds
      for (int col = 0; col < NUM_COLUMNS; col++) {

        std::vector<std::pair<float, float>> ex(EXAMPLES_PER_ROUND);
        float average_loss = 0, min_loss = 1.0f / 0.0f, max_loss = 0.0f;
        for (int idx = 0; idx < EXAMPLES_PER_ROUND; idx++) {
          float expected = expecteds[idx * NUM_COLUMNS + col];
          float actual = actuals[idx * NUM_COLUMNS + col];
          ex[idx] = std::make_pair(expected, actual);
          float loss = fabsf(expected - actual);
          min_loss = std::min(min_loss, loss);
          max_loss = std::max(max_loss, loss);
          average_loss += loss;

          if (idx == 0) {
            printf("Example 0: %s expected %.5f got %.5f\n",
                   COLUMN_NAMES[col], expected, actual);
          }
        }
        average_loss /= EXAMPLES_PER_ROUND;

        if (save_history) {
          error_history.Add(net->rounds, average_loss, col);
        }

        error_images[col]->Add(std::move(ex));

        if (save_error_images) {
          error_images[col]->Save();
          printf("Wrote %s\n", error_images[col]->Filename().c_str());
        }

        printf("   %s: %.3f<%.3f<%.3f\n",
               COLUMN_NAMES[col], min_loss, average_loss, max_loss);

        if (col == 0) last_error = average_loss;
      }

      loss_ms += loss_timer.MS();
    }

    // TODO: Should probably synchronize saving images with saving
    // the model. Otherwise when we continue, we lose pixels that
    // were written to the image but not saved to disk.

    // Note: Early on, this saves every round.
    if (!no_save &&
        (net->rounds < IMAGE_EVERY || (iter % IMAGE_EVERY) == 0)) {
      Timer image_timer;
      net_gpu->ReadFromGPU();

      const vector<vector<float>> stims = training->ExportStimulationsFlat();

      images.Sample(*net, EXAMPLES_PER_ROUND, stims);
      image_ms += image_timer.MS();

      #if 0
      // XXX HAX
      Errors err(*net);
      training->ExportErrors(0, &err);
      for (int layer = 1; layer < err.error.size(); layer++) {
        const std::vector<float> &e = err.error[layer];
        const int num = e.size();
        string filename =
          StringPrintf("%s.r%lld.%d.png",
                       Util::dirplus(dir, "err").c_str(),
                       net->rounds, layer);
        if (num == 1) {
          string text = StringPrintf("%.11g", e[0]);
          ImageRGBA img(text.size() * 9, 9);
          img.Clear32(0x000000FF);
          img.BlendText32(0, 0, 0xFFFFFFFF, text);
          img.Save(filename);
        } else {
          const int w = sqrt(num) + 1;
          const int h = (num / w) + 1;
          CHECK(w * h >= num);
          ImageRGBA img(w, h);
          img.Clear32(0x000000FF);
          for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
              int i = y * w + x;
              if (i >= num) continue;

              auto WeightColor = [](float f) -> uint32 {
                  auto MapV = [](float f) -> uint8 {
                      float ff = 0.0f;
                      ff = sqrtf(f);
                      return (uint8)std::clamp(
                          (int)roundf(255.0f * ff),
                          0,
                          255);
                    };

                  if (!std::isfinite(f)) {
                    return 0xFF00FFFF;
                  } else if (f == 0.0f) {
                    return 0xFFFF00FF;
                  } else if (f > 0.0f) {
                    uint32 v = MapV(f);
                    return (v << 16) | 0xFF;
                  } else {
                    uint32 v = MapV(-f);
                    return (v << 24) | 0xFF;
                  }
                };

              img.SetPixel32(x, y, WeightColor(e[i]));
            }
          }
          img.Save(filename);
        }
      }
      #endif
    }

    const bool save_timeout = !no_save && save_per.ShouldRun();

    // Checkpoint (no overwrite) every X rounds.
    bool checkpoint_timeout = (net->rounds % CHECKPOINT_EVERY_ROUNDS) == 0;

    if (SAVE_INTERMEDIATE && (save_timeout || checkpoint_timeout ||
                              iter % 5000 == 0)) {
      net_gpu->ReadFromGPU();
      // Note that if we write a checkpoint, we don't also overwrite
      // the main model, which is less redundant but might be surprising?
      const string filename = checkpoint_timeout ?
        StringPrintf("%s.%lld.val",
                     Util::dirplus(dir, MODEL_BASE).c_str(),
                     net->rounds) :
        model_file;
      net->SaveToFile(filename);
      if (VERBOSE)
        printf("Wrote %s\n", filename.c_str());
      error_history.Save();
      ImageRGBA error_image =
        error_history.MakeImage(1920, 1080,
                                {{0, 0xFFFF00AA},
                                 {1, 0x00FFFFAA}}, 0);
      string eimg_file = Util::dirplus(dir, MODEL_BASE "-error.png");
      error_image.Save(eimg_file);
      printf("Wrote %s\n", eimg_file.c_str());
    }

    round_ms += round_timer.MS();

    if (timing_per.ShouldRun()) {
      double accounted_ms = example_ms + forward_ms +
        error_ms + decay_ms + backward_ms + update_ms + loss_ms +
        image_ms;
      double other_ms = round_ms - accounted_ms;
      double pct = 100.0 / round_ms;
      printf("%.1f%% ex  "
             "%.1f%% fwd  "
             "%.1f%% err  "
             "%.1f%% dec  "
             "%.1f%% bwd  "
             "%.1f%% up  "
             "%.1f%% loss "
             "%.1f%% img "
             "%.1f%% other\n",
             example_ms * pct,
             forward_ms * pct,
             error_ms * pct,
             decay_ms * pct,
             backward_ms * pct,
             update_ms * pct,
             loss_ms * pct,
             image_ms * pct,
             other_ms * pct);
      double msr = 1.0 / (iter + 1);
      printf("%.1fms ex  "
             "%.1fms fwd  "
             "%.1fms err  "
             "%.1fms dec  "
             "%.1fms bwd  "
             "%.1fms up  "
             "%.2fms loss  "
             "%.2fms img  "
             "%.1fms other\n",
             example_ms * msr,
             forward_ms * msr,
             error_ms * msr,
             decay_ms * msr,
             backward_ms * msr,
             update_ms * msr,
             loss_ms * msr,
             image_ms * msr,
             other_ms * msr);
    }
  }

  // Done.
  net_gpu->ReadFromGPU();

  /*
  printf("Training took %.2f hours\n",
         train_timer.Seconds() / (60.0 * 60.0));
  printf("Reached max_rounds of %lld.\n", max_rounds);
  */
  if (!no_save) {
    net->SaveToFile(model_file);
    printf("Saved to %s.\n", model_file.c_str());
  }


  // For hyperparameter optimization.
  std::vector<int64> nonzero_error(net->layers.size(), 0);
  {
    // Get the number of nonzero errors on each layer, across
    // all examples. Note the first and last layers won't be
    // interesting, but we just compute them all for uniformity.
    Errors err(*net);
    for (int ex = 0; ex < EXAMPLES_PER_ROUND; ex++) {
      training->ExportErrors(ex, &err);
      for (int layer = 0; layer < err.error.size(); layer++) {
        const std::vector<float> &e = err.error[layer];
        for (float f : e) {
          if (std::isfinite(f) && f != 0.0f) nonzero_error[layer]++;
        }
      }
    }
  }
  printf("Ran " ABLUE("%lld") " rounds in "
         AYELLOW("%.2f") " sec. " AWHITE("Non-zeroes per layer") ":\n",
         max_rounds, train_timer.Seconds());
  for (int i = 0; i < nonzero_error.size(); i++) {
    printf("  %02d. %s%d" ANSI_RESET "\n",
           i, nonzero_error[i] == 0 ? ANSI_RED : ANSI_GREEN,
           nonzero_error[i]);
  }

  for (int i = nonzero_error.size() - 2; i > 0; i--) {
    if (nonzero_error[i] == 0) {
      // If the layer is all zero, we fail. But the later the
      // layer where this happens, the worse.
      const double badness = 100000000.0 * pow(10.0, i);
      // The less zero the next layer is, the better.
      return std::max(100000.0, badness - nonzero_error[i + 1]);
    }
  }

  // If we actually have a nontrivial network, then lower loss is
  // preferable.
  return last_error;
}

static unique_ptr<Network> NewChessNetwork(
    TransferFunction tf,
    RandomizationParams rand_params = {}) {
  printf("New network with transfer function %s\n",
         TransferFunctionName(tf));

  // Deterministic!
  ArcFour rc("learn-chess-network");

  std::vector<Layer> layers;

  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = NNChess::INPUT_SIZE;
  input_chunk.width = NNChess::INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  layers.push_back(Network::LayerFromChunks(input_chunk));

  static constexpr int FEATURES_3x3 = 256;
  static constexpr int FEATURES_1x1 = 32;
  static constexpr int FEATURES_8x1 = 128;
  static constexpr int FEATURES_1x8 = 128;
  static constexpr int NUM_INTERNAL_LAYERS = 3;

  // The board is spatial, so do some convolution. 3x3.
  Chunk first_conv_chunk =
    Network::Make2DConvolutionChunk(
        // span start, width, height
        0, 8 * NNChess::SQUARE_SIZE, 8,
        // features, pattern size
        FEATURES_3x3, 3 * NNChess::SQUARE_SIZE, 3,
        // stride is one square, but align with the one-hot pieces
        NNChess::SQUARE_SIZE, 1,
        tf, WEIGHT_UPDATE);

  // Also a 1x1.
  Chunk square_conv_chunk =
    Network::Make2DConvolutionChunk(
        0, 8 * NNChess::SQUARE_SIZE, 8,
        FEATURES_1x1, NNChess::SQUARE_SIZE, 1,
        NNChess::SQUARE_SIZE, 1,
        tf, WEIGHT_UPDATE);

  // And 8x1, 1x8.
  Chunk row_conv_chunk =
    Network::Make2DConvolutionChunk(
        0, 8 * NNChess::SQUARE_SIZE, 8,
        FEATURES_8x1, 8 * NNChess::SQUARE_SIZE, 1,
        8 * NNChess::SQUARE_SIZE, 1,
        tf, WEIGHT_UPDATE);
  Chunk col_conv_chunk =
    Network::Make2DConvolutionChunk(
        0, 8 * NNChess::SQUARE_SIZE, 8,
        FEATURES_1x8, NNChess::SQUARE_SIZE, 8,
        NNChess::SQUARE_SIZE, 8,
        tf, WEIGHT_UPDATE);

  vector<Network::SparseSpan> sparse_spans = {
    // Sample from the board.
    Network::SparseSpan(0, 8 * 8 * NNChess::SQUARE_SIZE, 63),
    // And one from the less-interesting state.
    Network::SparseSpan(NNChess::OTHER_STATE_IDX,
                        NNChess::OTHER_STATE_SIZE, 1),
  };
  Chunk first_all_chunk =
    Network::MakeRandomSparseChunk(&rc, 2048, sparse_spans, tf, ADAM);

  layers.push_back(Network::LayerFromChunks(first_conv_chunk,
                                            square_conv_chunk,
                                            row_conv_chunk,
                                            col_conv_chunk,
                                            first_all_chunk));

  // Then, more convolution and mixing layers.
  for (int layer = 0; layer < NUM_INTERNAL_LAYERS; layer++) {
    const int conv_width =
      first_conv_chunk.num_occurrences_across * FEATURES_3x3 *
      first_conv_chunk.num_occurrences_down;
    Chunk conv_chunk =
      Network::Make1DConvolutionChunk(
          // span start, width
          0,
          conv_width,
          // features, pattern width, stride.
          // each block processed independently, same size output
          FEATURES_3x3, FEATURES_3x3, FEATURES_3x3,
          tf, WEIGHT_UPDATE);

    // and the sparse channel (which doesn't depend on the 3x3 convolutions)
    int prev_size = layers.back().num_nodes;
    int skip_prefix = (layer == NUM_INTERNAL_LAYERS - 1) ? 0 : conv_width;
    Chunk sparse_chunk =
      Network::MakeRandomSparseChunk(
          &rc, 4096,
          {Network::SparseSpan(skip_prefix, prev_size - skip_prefix, 24)},
          tf, ADAM);

    layers.push_back(Network::LayerFromChunks(conv_chunk, sparse_chunk));
  }

  constexpr int LAST_INTERNAL_LAYER_SIZE = 32;

  // Scale down.
  while (layers.back().num_nodes > LAST_INTERNAL_LAYER_SIZE) {
    int prev_size = layers.back().num_nodes;
    int target_size = std::min(LAST_INTERNAL_LAYER_SIZE, prev_size / 8);
    Chunk sparse_chunk =
      Network::MakeRandomSparseChunk(
          &rc, target_size,
          {Network::SparseSpan(0, prev_size, prev_size / 16)},
          tf, ADAM);

    layers.push_back(Network::LayerFromChunks(sparse_chunk));
  }

  // Finally, a dense chunk for the output.
  CHECK(layers.back().num_nodes == LAST_INTERNAL_LAYER_SIZE);

  Chunk sink1;
  sink1.type = CHUNK_DENSE;
  // Since the eval output is [-1, +1], ideally we want to use a
  // symmetric transfer function like TANH here.
  //
  // For the grad experiments, we want the transfer function to be
  // "linear," so to be fair to both the test and reference functions,
  // we use IDENTITY for the last layer.
  sink1.transfer_function = IDENTITY;
  sink1.num_nodes = 1;
  sink1.span_start = 0;
  sink1.span_size = layers.back().num_nodes;
  sink1.indices_per_node = sink1.span_size;
  sink1.weights.resize(sink1.indices_per_node * sink1.num_nodes);
  sink1.biases.resize(sink1.num_nodes);
  sink1.weight_update = WEIGHT_UPDATE;
  sink1.weights_aux.resize(sink1.weights.size() * 2, 0.0f);
  sink1.biases_aux.resize(sink1.biases.size() * 2, 0.0f);
  sink1.fixed = false;
  sink1.width = sink1.num_nodes;
  sink1.height = 1;
  sink1.channels = 1;

  layers.push_back(
      Network::LayerFromChunks(std::move(sink1)));
  CHECK(layers.back().num_nodes == NNChess::OUTPUT_SIZE);

  auto net = std::make_unique<Network>(layers);

  printf("Randomize..\n");
  RandomizeNetwork(&rc, net.get(), rand_params, 2);
  printf("New network with %lld parameters\n", net->TotalParameters());
  return net;
}

// Note that destructor does not stop the thread. If we want to
// have the program exit cleanly, we'd need to add this.
static ExamplePool *example_pool = nullptr;


static constexpr TransferFunction OPT_TF = TANH;
static constexpr int OPT_ROUNDS = 2000;
static constexpr int NUM_INTS = 2;
static constexpr int NUM_DOUBLES = 9;
using TrainOptimizer = Optimizer<NUM_INTS, NUM_DOUBLES, char>;

static std::pair<TrainParams, RandomizationParams>
GetOptimizedParams(TransferFunction tf) {
  switch (tf) {
  case SIGMOID: {
    // This was actually a sigmoid with a hacked in bonus derivative of 0.01.
    #if 0
    // sigmoid results, which were just the first sample!
      GetParams(
          std::make_pair(
              std::array<int32_t, NUM_INTS>{0, 1},
              std::array<double,  NUM_DOUBLES>{18.0, 5.8300267747, 18.383389302, 14.109313061,
                  3.0086677831, 8760.2582169, 1013.9750224, 1.1163693398}));
    #endif

    RandomizationParams rparams =
      RandomizationParams{.sigmoid_uniform = true, .sigmoid_mag = 1.1163693666};
    TrainParams tparams =
      TrainParams{.update_config = UpdateConfig{.base_learning_rate = 0.0029379983151, .learning_rate_dampening = 18.383389302, .max_num_scratch = 2147483648, .adam_epsilon = 7.4542367656e-07, .adam_b1 = 0.89999997616, .adam_b2 = 0.99900001287, .clipping = false, .constrain = true, .weight_constrain_max = 3.0086677074, .bias_constrain_max = 8760.2578125, .clip_error = true, .error_max = 1013.9750366 }, .do_decay = false, .decay_rate = 0.99900001287};
    return make_pair(tparams, rparams);
  }
  case TANH: {
    RandomizationParams rparams =
      RandomizationParams{
      .sigmoid_uniform = false, .sigmoid_mag = 0.10000000149,
      .zeromean_uniform = true, .zeromean_numer = 0.84733331203};
    TrainParams tparams =
      TrainParams{
      .update_config = {.base_learning_rate = 0.022586992263, .learning_rate_dampening = 51.75795321, .max_num_scratch = 2147483648, .adam_epsilon = 4.8231521532e-07, .adam_b1 = 0.89999997616, .adam_b2 = 0.99900001287, .clipping = false, .constrain = true, .weight_constrain_max = 5.5063300133, .bias_constrain_max = 13863.599609, .clip_error = true, .error_max = 816.96124268, .conv_update_exponent = 0.0065212696791 }, .do_decay = false, .decay_rate = 0.99900001287};
    return make_pair(tparams, rparams);
  }
  default: {
    printf("Note: No optimization results for %s\n",
           TransferFunctionName(tf));
    RandomizationParams rparams;
    TrainParams tparams = DefaultTrainParams();
    return make_pair(tparams, rparams);
  }
  }
}





static std::pair<TrainParams, RandomizationParams> GetParams(
    const TrainOptimizer::arg_type &args) {
  const auto &[clip_int, zmunif_int] = args.first;
  const auto &[decay,
               base_rate, dampening, adam_e,
               weight_max, bias_max,
               clip_error, zeromean_numer,
               conv_update] = args.second;

  TrainParams tparams;

  tparams.do_decay = decay < 0.0;
  // Make sure the parameter is valid even if disabled.
  tparams.decay_rate = decay < 0.0 ? (1.0 - exp(-decay)) : 0.999f;

  tparams.update_config.base_learning_rate = exp(-base_rate);
  tparams.update_config.learning_rate_dampening = dampening;

  tparams.update_config.adam_epsilon = exp(-adam_e);

  tparams.update_config.clipping = clip_int == 1;

  tparams.update_config.constrain = weight_max > 1.0 || bias_max > 1.0;
  tparams.update_config.weight_constrain_max = weight_max;
  tparams.update_config.bias_constrain_max = bias_max;

  tparams.update_config.clip_error = clip_error >= 1.0;
  tparams.update_config.error_max = clip_error;

  tparams.update_config.conv_update_exponent = conv_update;

  RandomizationParams rparams;
  rparams.zeromean_uniform = zmunif_int == 1;
  rparams.zeromean_numer = zeromean_numer;

  return make_pair(tparams, rparams);
}

static TrainOptimizer::return_type
OptimizeMe(const TrainOptimizer::arg_type &args) {
  printf("\n" ABGCOLOR(50, 0, 50,
                       AFGCOLOR(240, 240, 240,
                                "== Optimize with params ==")) "\n");
  for (const double d : args.second) {
    printf("  %.11g\n", d);
  }

  const auto &[tparams, rparams] = GetParams(args);

  unique_ptr<Network> net = NewChessNetwork(OPT_TF, rparams);

  double score = Train("opt-tmp", net.get(), OPT_ROUNDS,
                       // no save
                       true,
                       tparams, example_pool);

  // All args are "feasible"
  return make_pair(score, make_optional('@'));
}




int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;

#if 0
  const auto [tparams, rparams] =
      GetParams(
          std::make_pair(
              std::array<int32_t, NUM_INTS>{0, 1},
              std::array<double,  NUM_DOUBLES>{
                10.31953607, 3.790381102, 51.75795321, 14.54466792, 5.506329934, 13863.59975, 816.9612668, 0.8473333159, 0.006521269745}));

  printf("Randomization params:\n%s\n",
         rparams.ToString().c_str());
  printf("Training params:\n%s\n",
         tparams.ToString().c_str());
#endif

  if (false) {
    example_pool = new ExamplePool;
    example_pool->PopulateExamplesInBackground(GAME_PGN, -1);

    const std::array<std::pair<int32_t, int32_t>, NUM_INTS>
      int_bounds = {
      // clipping, bool
      make_pair(0, 2),
      // uniform sigmoid initialization, bool
      make_pair(0, 2),
    };

    const std::array<std::pair<double, double>, NUM_DOUBLES>
      double_bounds = {
      // decay, 1 - (e^-x) if x < 0, or else disabled
      make_pair(-18.0, 18.0),

      // learning rate, e^-x
      make_pair(0.0, 16.0),
      // dampening
      make_pair(0.01, 100.0),

      // adam_epsilon, e^-x
      make_pair(0.0, 16.0),

      // constrain disabled if both are <1.
      // weight constrain max
      make_pair(0.1f, 16.0f),
      // bias constrain max
      make_pair(0.1f, 16384.0f),

      // clip error, disabled if <1
      make_pair(0.1f, 1024.0),

      // mag of random initialization
      make_pair(0.00001f, sqrtf(6.0f)),

      // conv update
      make_pair(0.0f, 1.1f),
    };

    constexpr double hour = 3600.0;

    Timer opt_timer;

    TrainOptimizer opt(OptimizeMe, (int64_t)time(nullptr));
    opt.SetSaveAll(true);
    opt.Run(int_bounds, double_bounds,
            nullopt, nullopt, {4 * hour});

    printf("Ran " ABLUE("%lld") " evaluations in "
           AYELLOW("%.3f") " sec.\n",
           opt.NumEvaluations(),
           opt_timer.Seconds());

    string tsv;
    StringAppendF(&tsv,
                  "SCORE\t"
                  "clip\tzmunif\t"
                  "decay\tbase_rate\tdampening\tadam_e\t"
                  "weight_max\tbias_max\t"
                  "clip_error\tzeromean_numer\t"
                  "conv_update\n");
    for (const auto &[args, score, outopt_] : opt.GetAll()) {
      const auto &[clip_int, zmunif_int] = args.first;
      const auto &[decay,
                   base_rate, dampening, adam_e,
                   weight_max, bias_max,
                   clip_error, zeromean_numer, conv_update] = args.second;
      StringAppendF(&tsv,
                    "%.11g\t"
                    "%d\t%d\t"
                    "%.11g\t%.11g\t%.11g\t%.11g\t"
                    "%.11g\t%.11g\t"
                    "%.11g\t%.11g\t"
                    "%.11g\n",
                    score,
                    clip_int, zmunif_int,
                    decay,
                    base_rate, dampening, adam_e,
                    weight_max, bias_max,
                    clip_error, zeromean_numer,
                    conv_update);
    }

    Util::WriteFile("records.tsv", tsv);
    printf("Results:\n%s\n", tsv.c_str());

  } else {

    CHECK(argc == 4) <<
      "./train-chess.exe dir transfer_function rounds\n"
      "Notes:\n"
      "  dir must exist. Resumes training if the dir\n"
      "    contains a model file.\n"
      "  transfer_function should be one of\n"
      "    SIGMOID, RELU, LEAKY_RELU, IDENTITY\n"
      "    TANH, GRAD1,\n";

    const string dir = argv[1];
    const TransferFunction tf = ParseTransferFunction(argv[2]);
    const int64 max_rounds = (int64)atoll(argv[3]);
    CHECK(max_rounds > 0) << argv[3];

    auto [tparams, rparams] = GetOptimizedParams(tf);

    printf("Randomization params:\n%s\n",
           rparams.ToString().c_str());
    printf("Training params:\n%s\n",
           tparams.ToString().c_str());

    const string model_file = Util::dirplus(dir, MODEL_NAME);

    example_pool = new ExamplePool;
    example_pool->PopulateExamplesInBackground(GAME_PGN, -1);

    std::unique_ptr<Network> net(
        Network::ReadFromFile(model_file));

    if (net.get() == nullptr) {
      net = NewChessNetwork(tf, rparams);
      CHECK(net.get() != nullptr);
      net->SaveToFile(model_file);
      printf("Wrote to %s\n", model_file.c_str());
    }

    net->StructuralCheck();
    net->NaNCheck(model_file);

    if (net->rounds >= max_rounds) {
      printf("%s: Already trained %lld rounds.\n",
             model_file.c_str(), net->rounds);
    } else {
      Train(dir, net.get(), max_rounds, false, tparams, example_pool);
    }

    printf("OK\n");
  }

  return 0;
}
