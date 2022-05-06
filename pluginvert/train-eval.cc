// Toy to train a chess evaluation function with self-play.

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

#include "chess.h"
#include "network.h"
#include "network-test-util.h"
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

using namespace std;

static CL *cl = nullptr;

using int64 = int64_t;
using uint64 = uint64_t;
using uint8 = uint8_t;

static constexpr WeightUpdate WEIGHT_UPDATE = ADAM;

#define MODEL_BASE "eval"
#define MODEL_NAME MODEL_BASE ".val"

// 8x8x13 one-hot, then 1x side to move, 4x castling bits,
// 8x en passant state
static constexpr int SQUARE_SIZE = 13;
static constexpr int BOARD_SIZE = 8 * 8 * SQUARE_SIZE + 1 + 4 + 8;
static constexpr int WHOSE_MOVE_IDX = 8 * 8 * SQUARE_SIZE;
static constexpr int OTHER_STATE_IDX = WHOSE_MOVE_IDX + 1;
static constexpr int OTHER_STATE_SIZE = 4 + 8;

// Write into the output vector starting at the given index; the space must
// already be reserved.
static void BoardVecTo(const Position &pos, std::vector<float> *out, int idx) {
  for (int i = 0; i < BOARD_SIZE; i++) (*out)[idx + i] = 0.0f;

  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      const uint8 p = pos.SimplePieceAt(y, x);
      if (p == Position::EMPTY) {
        (*out)[idx] = 1.0f;
      } else if ((p & Position::COLOR_MASK) == Position::WHITE) {
        const uint8 t = p & Position::TYPE_MASK;
        (*out)[idx + 1 + t] = 1.0f;
      } else {
        // p & COLOR_MASK == BLACK
        const uint8 t = p & Position::TYPE_MASK;
        (*out)[idx + 1 + 6 + t] = 1.0f;
      }
      idx += 13;
    }
  }

  // Side to move. 0 = white, 1 black.
  (*out)[idx++] = pos.BlackMove() ? 1.0f : 0.0f;
  // Castling.
  (*out)[idx++] = pos.CanStillCastle(false, false) ? 1.0f : 0.0f;
  (*out)[idx++] = pos.CanStillCastle(false, true) ? 1.0f : 0.0f;
  (*out)[idx++] = pos.CanStillCastle(true, false) ? 1.0f : 0.0f;
  (*out)[idx++] = pos.CanStillCastle(true, true) ? 1.0f : 0.0f;
  // En passant state.
  std::optional<uint8> ep = pos.EnPassantColumn();
  if (ep.has_value()) {
    uint8 c = ep.value() & 0x7;
    (*out)[idx + c] = 1.0f;
  }
}

static vector<float> BoardVec(const Position &pos) {
  std::vector<float> ret;
  ret.resize(BOARD_SIZE);
  BoardVecTo(pos, &ret, 0);
  return ret;
}

static constexpr int INPUT_SIZE = BOARD_SIZE;
static constexpr int OUTPUT_SIZE = 2;

// Examples are about 2k.
static constexpr int EXAMPLES_PER_ROUND = 1000;

static void Train(Network *net) {
  ArcFour rc(StringPrintf("%lld.train.%s", time(nullptr), MODEL_BASE));

  ErrorHistory error_history(StringPrintf("%s-error-history.tsv", MODEL_BASE));

  static constexpr int max_parallelism = 4;
  // 0, 1, 2
  static constexpr int VERBOSE = 1;
  static constexpr bool SAVE_INTERMEDIATE = true;

  UpdateWeightsCL::UpdateConfig update_config;
  update_config.base_learning_rate = 0.1f;
  // This is conservative, but with larger exponents I would
  // get divergence after hundreds of thousands of rounds.
  // This happened again with the plugin parameter predictor
  // with a value of 1e-6!
  update_config.adam_epsilon = 1.0e-4;

  // XXX this should probably depend on the learning rate; if the
  // learning rate is too small, it won't even be able to overcome
  // the decay
  static constexpr float DECAY_RATE = 0.999999f;
  static constexpr bool DO_DECAY = false;

  // On a verbose round we compute training error and print out
  // examples.
  constexpr int VERBOSE_EVERY = 10;
  // We save this to the error history file every this many
  // verbose rounds.
  constexpr int HISTORY_EVERY_VERBOSE = 5;
  int64 total_verbose = 0;
  constexpr int TIMING_EVERY = 20;

  static constexpr int64 CHECKPOINT_EVERY_ROUNDS = 100000;

  constexpr int IMAGE_EVERY = 10;
  TrainingImages images(*net, "train", MODEL_NAME, IMAGE_EVERY);

  printf("Training!\n");

  auto net_gpu = make_unique<NetworkGPU>(cl, net);

  std::unique_ptr<ForwardLayerCL> forward_cl =
    std::make_unique<ForwardLayerCL>(cl, net_gpu.get());
  std::unique_ptr<SetOutputErrorCL> error_cl =
    std::make_unique<SetOutputErrorCL>(cl, net_gpu.get());
  std::unique_ptr<BackwardLayerCL> backward_cl =
    std::make_unique<BackwardLayerCL>(cl, net_gpu.get());
  [[maybe_unused]]
  std::unique_ptr<DecayWeightsCL> decay_cl =
    std::make_unique<DecayWeightsCL>(cl, net_gpu.get(), DECAY_RATE);
  std::unique_ptr<UpdateWeightsCL> update_cl =
    std::make_unique<UpdateWeightsCL>(cl, net_gpu.get(),
                                      EXAMPLES_PER_ROUND,
                                      update_config);

  // Uninitialized training examples on GPU.
  std::unique_ptr<TrainingRoundGPU> training(
      new TrainingRoundGPU(EXAMPLES_PER_ROUND, cl, *net));

  CHECK(!net->layers.empty());
  CHECK(net->layers[0].num_nodes == INPUT_SIZE);
  CHECK(net->layers.back().num_nodes == OUTPUT_SIZE);

  // Stats from self-play.
  int white_wins = 0, black_wins = 0,
    repetition_draws = 0, reversible_draws = 0, stalemate_draws = 0;

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
  // seconds since timer started
  double last_save = 0.0;
  for (int iter = 0; true; iter++) {
    Timer round_timer;

    const bool verbose_round = (iter % VERBOSE_EVERY) == 0;

    // Initialize training examples.

    // We do this by self-playing until we have filled the budget.

    std::vector<float> inputs;
    inputs.resize(EXAMPLES_PER_ROUND * INPUT_SIZE);
    std::vector<float> expecteds;
    expecteds.resize(EXAMPLES_PER_ROUND * OUTPUT_SIZE);

    {
      Timer example_timer;

      enum class Result {
        WHITE_WINS,
        BLACK_WINS,
        DRAW_BY_REPETITION,
        DRAW_50_MOVES,
        DRAW_STALEMATE,
        // ...
      };

      auto ResultString = [](Result r) -> const char * {
          switch (r) {
          case Result::WHITE_WINS: return "WHITE";
          case Result::BLACK_WINS: return "BLACK";
          case Result::DRAW_BY_REPETITION: return "REPETITION";
          case Result::DRAW_50_MOVES: return "50 MOVES";
          case Result::DRAW_STALEMATE: return "STALEMATE";
          default: return "??";
          }
        };

      std::vector<std::tuple<Position, Result, float>> ex;
      ex.reserve(EXAMPLES_PER_ROUND);

      // scratch space to load boards to evaluate
      std::vector<float> inputs;
      inputs.resize(INPUT_SIZE * EXAMPLES_PER_ROUND);

      // scratch space to read out the scores of moves at each position
      std::vector<float> outputs;
      outputs.resize(OUTPUT_SIZE * EXAMPLES_PER_ROUND);

      // PERF: We can have multiple games being played simultaneously.
      for (;;) {
        Position pos;

        // For detecting draws by repetition.
        std::unordered_map<Position, int, PositionHash, PositionEq>
          position_counts;
        // Count of moves without pawn move or capture. We probably aren't
        // handling the end conditions correctly here.
        int stale_moves = 0;

        // This is the history of the game, used for generating the training
        // examples at the end.
        std::vector<Position> reached;
        // Move with the eval/over of the resulting position.
        // Note: We don't actually need to save this.
        // XXX save and print it
        std::vector<std::tuple<Position::Move, float, float>> moves;

        for (;;) {
          reached.push_back(pos);
          position_counts[pos]++;
          std::vector<Position::Move> legal = pos.GetLegalMoves();

          // We use threefold repetition (our player "claims" the draw).
          const bool draw_by_repetition = position_counts[pos] >= 3;
          const bool draw_50_moves = stale_moves > 100;
          // also need to check draw by repetition, counter, and (dead position?)
          if (draw_by_repetition || draw_50_moves || legal.empty()) {
            // Game has ended. If it's black's move and we're mated, this is a
            // win for white (+1), etc.
            Result result = Result::DRAW_STALEMATE;
            if (draw_by_repetition) {
              repetition_draws++;
              result = Result::DRAW_BY_REPETITION;
            } else if (draw_50_moves) {
              reversible_draws++;
              result = Result::DRAW_50_MOVES;
            } else if (pos.IsMated()) {
              if (pos.BlackMove()) {
                white_wins++;
                result = Result::WHITE_WINS;
              } else {
                black_wins++;
                result = Result::BLACK_WINS;
              }
            } else {
              stalemate_draws++;
              result = Result::DRAW_STALEMATE;
            }

            // PERF
            if (false) {
              Position replay;
              printf("%s: ", ResultString(result));
              for (int i = 0; i < moves.size(); i++) {
                auto [m, e, o] = moves[i];
                CHECK(replay.IsLegal(m)) << replay.ToFEN(0, 0) << " "
                                         << Position::DebugMoveString(m);
                printf("%d.%s %s { [%%eval %.3f] [%%over %.3f] } ",
                       1 + (i >> 1), (i & 1) ? ".." : "",
                       replay.ShortMoveString(m).c_str(),
                       e, o);
                replay.ApplyMove(m);
              }
              printf("\n");
            }

            float num_moves = (float)reached.size();
            for (int i = reached.size(); i >= 0; i--) {
              ex.emplace_back(std::move(reached[i]), result, i / num_moves);
              if (ex.size() == EXAMPLES_PER_ROUND) goto enough_examples;
            }
            goto next_game;
          }

          // Use the existing training round to predict the score for each move.
          for (int move_idx = 0; move_idx < legal.size(); move_idx++) {
            // Could just drop moves in this case, but we don't expect to
            // even come close.
            CHECK(move_idx < EXAMPLES_PER_ROUND);

            pos.MoveExcursion(legal[move_idx], [&]() {
                BoardVecTo(pos, &inputs, move_idx * INPUT_SIZE);
                return 0;
              });
          }
          // PERF: Only copy the prefix...
          training->LoadInputs(inputs);

          // Score them.
          for (int src_layer = 0;
               src_layer < net->layers.size() - 1;
               src_layer++) {
            // We only need to run legal.size() examples here; the
            // rest are just leftover from the last round and we won't
            // look at the outputs.
            forward_cl->RunForwardPrefix(training.get(), src_layer, (int)legal.size());
          }

          // PERF: Only copy the prefix that has meaningful data...
          training->ExportOutputs(&outputs);
          // Negate the scores (in place) if black has the move, so we'll
          // always prefer positive scores below.
          if (pos.BlackMove()) {
            for (int i = 0; i < legal.size(); i++) {
              outputs[i * 2] = -outputs[i * 2];
            }
          }

          const int take_move = [&]() -> int {
              if (legal.size() == 1)
                return 0;

              std::vector<int> best_idx;
              best_idx.reserve(legal.size());
              for (int i = 0; i < legal.size(); i++) best_idx.push_back(i);
              // Avoid biasing towards earlier moves when the distribution
              // is very flat.
              Shuffle(&rc, &best_idx);
              // Sort by score, descending.
              std::sort(best_idx.begin(), best_idx.end(),
                        [&](int a, int b) {
                          return outputs[a * 2] > outputs[b * 2];
                        });

              // Sample the move.
              // Is there a more principled way (i.e. fewer parameters) to do this?
              CHECK(legal.size() > 1);
              const int n_best = std::clamp((int)legal.size() / 4, 1, (int)legal.size() - 1);

              // the next element; we base the scores for weighted sampling off this
              int bias_idx = std::clamp(n_best + 1, 0, (int)legal.size() - 1);
              CHECK(bias_idx >= 0 && bias_idx < legal.size());
              int bias_elt = best_idx[bias_idx];
              CHECK(bias_elt >= 0 && bias_elt < legal.size());
              float bias = outputs[bias_elt * 2];
              int best_elt = best_idx[0];
              CHECK(best_elt >= 0 && best_elt < legal.size());
              if (outputs[best_elt * 2] >= bias) {
                // Flat distribution. Choose uniformly.
                return RandTo(&rc, n_best);
              }

              // Otherwise, weighted.
              double total_weight = 0.0;
              for (int i = 0; i < n_best; i++) {
                int elt = best_idx[i];
                CHECK(elt >= 0 && elt <= legal.size());
                total_weight += outputs[elt * 2];
              }

              double point = RandDouble(&rc) * total_weight;
              for (int i = 0; i < n_best; i++) {
                int elt = best_idx[i];
                CHECK(elt >= 0 && elt <= legal.size());
                if (point < outputs[elt * 2])
                  return elt;
                point -= outputs[elt * 2];
              }

              // Only can get here due to floating point
              // approximations failing us.
              return best_idx[0];
            }();
          CHECK(take_move >= 0 && take_move < legal.size());

          {
            stale_moves++;
            Position::Move m = legal[take_move];
            moves.emplace_back(m,
                               outputs[take_move * 2 + 0],
                               outputs[take_move * 2 + 1]);
            if (pos.IsCapturing(m) ||
                pos.IsPawnMove(m)) {
              stale_moves = 0;
              position_counts.clear();
            }
            pos.ApplyMove(m);
          }
        }
      next_game:;
      }

      enough_examples:;

      CHECK(ex.size() == EXAMPLES_PER_ROUND);

      for (int i = 0; i < ex.size(); i++) {
        const auto &[pos, result, over] = ex[i];
        BoardVecTo(pos, &inputs, INPUT_SIZE * i);
        auto ResultFloat = [](Result r) -> float {
            switch (r) {
            case Result::WHITE_WINS: return 1.0f;
            case Result::BLACK_WINS: return -1.0f;
            default: return 0.0f;
            }
          };
        expecteds[i * 2 + 0] = ResultFloat(result);
        expecteds[i * 2 + 1] = over;
      }

      CHECK(inputs.size() == INPUT_SIZE * EXAMPLES_PER_ROUND);
      CHECK(expecteds.size() == OUTPUT_SIZE * EXAMPLES_PER_ROUND);
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

    if (DO_DECAY) {
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

    // (currently no way to actually finish, but we could set a
    // training error threshold below.)
    const bool finished = false;

    if (verbose_round) {
      Timer loss_timer;
      if (VERBOSE > 1)
        printf("Verbose round...\n");

      // Get loss as abs distance, plus number of incorrect (as booleans).

      // PERF could do this on the flat vector, but we only need to
      // run it for verbose rounds
      std::vector<std::vector<float>> expected;
      expected.reserve(EXAMPLES_PER_ROUND);
      for (int i = 0; i < EXAMPLES_PER_ROUND; i++) {
        std::vector<float> one;
        one.resize(OUTPUT_SIZE, 0.0f);
        for (int j = 0; j < OUTPUT_SIZE; j++) {
          one[j] = expecteds[i * OUTPUT_SIZE + j];
        }
        expected.emplace_back(std::move(one));
      }

      if (VERBOSE > 1)
        printf("Got expected\n");

      std::vector<float> losses =
        UnParallelMapi(expected,
                       [&](int idx, const std::vector<float> &exp) {
                         std::vector<float> got;
                         got.resize(exp.size());
                         CHECK(got.size() == OUTPUT_SIZE);
                         CHECK(idx >= 0 && idx < EXAMPLES_PER_ROUND);
                         training->ExportOutput(idx, &got);

                         // XXX if this is all we're doing, we should
                         // instead use the already-computed
                         // values from GetOutputError, perhaps
                         // summing on GPU
                         float loss = 0.0f;
                         for (int i = 0; i < exp.size(); i++) {
                           loss += fabsf(exp[i] - got[i]);
                         }

                         return loss;
                       }, max_parallelism);

      if (VERBOSE > 1)
        printf("Got losses.\n");

      float min_loss = 1.0f / 0.0f, average_loss = 0.0f, max_loss = 0.0f;
      for (auto loss_dist : losses) {
        min_loss = std::min(loss_dist, min_loss);
        max_loss = std::max(loss_dist, max_loss);
        average_loss += loss_dist;
      }
      average_loss /= losses.size();

      const double total_sec = train_timer.MS() / 1000.0;
      const double eps = total_examples / total_sec;

      printf("%lld|%d: %.3f<%.3f<%.3f  %d wh %d bl %d rep %d fifty %d stale",
             net->rounds, iter,
             min_loss, average_loss, max_loss,
             white_wins, black_wins, repetition_draws,
             reversible_draws, stalemate_draws);
      printf(" (%.2f eps)\n", eps);

      if ((total_verbose % HISTORY_EVERY_VERBOSE) == 0) {
        error_history.Add(net->rounds, average_loss, false);
      }
      total_verbose++;
      loss_ms += loss_timer.MS();
    }

    // TODO: Should probably synchronize saving images with saving
    // the model. Otherwise when we continue, we lose pixels that
    // were written to the image but not saved to disk.
    if ((iter % IMAGE_EVERY) == 0) {
      Timer image_timer;
      net_gpu->ReadFromGPU();
      images.Sample(*net);
      image_ms += image_timer.MS();
    }

    static constexpr double SAVE_EVERY_SEC = 180.0;
    bool save_timeout = false;
    if ((train_timer.MS() / 1000.0) > last_save + SAVE_EVERY_SEC) {
      save_timeout = true;
      last_save = train_timer.MS() / 1000.0;
    }

    // Checkpoint (no overwrite) every X rounds.
    bool checkpoint_timeout = (net->rounds % CHECKPOINT_EVERY_ROUNDS) == 0;

    if (SAVE_INTERMEDIATE && (save_timeout || checkpoint_timeout || finished ||
                              iter % 5000 == 0)) {
      net_gpu->ReadFromGPU();
      // Note that if we write a checkpoint, we don't also overwrite
      // the main model, which is less redundant but might be surprising?
      const string filename = checkpoint_timeout ?
        StringPrintf(MODEL_BASE ".%lld.val", net->rounds) :
        (string)MODEL_NAME;
      net->SaveToFile(filename);
      if (VERBOSE)
        printf("Wrote %s\n", filename.c_str());
      error_history.Save();
    }

    // Parameter for average_loss termination?
    if (finished) {
      printf("Successfully trained!\n");
      return;
    }

    round_ms += round_timer.MS();

    if (iter % TIMING_EVERY == 0) {
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
}

static unique_ptr<Network> NewEvalNetwork() {
  // Deterministic!
  ArcFour rc("learn-eval-network");

  std::vector<Layer> layers;

  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
  input_chunk.height = 1;
  input_chunk.channels = 1;

  layers.push_back(Network::LayerFromChunks(input_chunk));

  static constexpr int FEATURES_1 = 128;
  static constexpr int NUM_INTERNAL_LAYERS = 1;

  // The board is spatial, so do some convolution. 3x3.
  Chunk first_conv_chunk =
    Network::Make2DConvolutionChunk(
        // span start, width, height
        0, 8 * SQUARE_SIZE, 8,
        // features, pattern size
        FEATURES_1, 3 *, 3,
        // stride is one square, but align with the one-hot pieces
        SQUARE_SIZE, 1,
        LEAKY_RELU, WEIGHT_UPDATE);

  vector<Network::SparseSpan> sparse_spans = {
    // Always sample the side to move on this first layer.
    Network::SparseSpan(WHOSE_MOVE_IDX, 1, 1),
    // Sample from the board.
    Network::SparseSpan(0, 8 * 8 * SQUARE_SIZE, 62),
    // And one from the less-interesting state.
    Network::SparseSpan(OTHER_STATE_IDX, OTHER_STATE_SIZE, 1),
  };
  Chunk first_all_chunk =
    Network::MakeRandomSparseChunk(&rc, 4096, sparse_spans, LEAKY_RELU, ADAM);

  layers.push_back(Network::LayerFromChunks(first_conv_chunk, first_all_chunk));

  // Then, more convolution and mixing layers.
  for (int layer = 0; layer < NUM_INTERNAL_LAYERS; layer++) {
    Chunk conv_chunk =
      Network::Make1DConvolutionChunk(
          // span start, width
          0,
          first_conv_chunk.num_occurrences_across * FEATURES_1 *
          first_conv_chunk.num_occurrences_down,
          // features, pattern width, stride.
          // each block processed indepenently, same size output
          FEATURES_1, FEATURES_1, FEATURES_1,
          LEAKY_RELU, WEIGHT_UPDATE);

    // and more sparse mixing.
    int prev_size = layers.back().num_nodes;
    Chunk sparse_chunk =
      Network::MakeRandomSparseChunk(
          &rc, 4096, {Network::SparseSpan(0, prev_size, 64)}, LEAKY_RELU, ADAM);

    layers.push_back(Network::LayerFromChunks(conv_chunk, sparse_chunk));
  }

  // Scale down.
  while (layers.back().num_nodes > 16) {
    int prev_size = layers.back().num_nodes;
    Chunk sparse_chunk =
      Network::MakeRandomSparseChunk(
          &rc, prev_size / 4,
          {Network::SparseSpan(0, prev_size, prev_size / 8)}, LEAKY_RELU, ADAM);

    layers.push_back(Network::LayerFromChunks(sparse_chunk));
  }

  // Finally, a dense chunk for each of the two outputs. Maybe we could
  // segregate these somewhat (depending on different, partially-overlapping
  // regions of the previous)?
  Chunk sink1;
  sink1.type = CHUNK_DENSE;
  // Since the output is [-1, +1], we want to use a symmetric evaluation
  // function like TANH (IDENTITY would also be ok) here.
  sink1.transfer_function = TANH;
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

  Chunk sink2;
  sink2.type = CHUNK_DENSE;
  // This one is in [0, 1] so sigmoid makes sense.
  sink2.transfer_function = SIGMOID;
  sink2.num_nodes = 1;
  sink2.span_start = 0;
  sink2.span_size = layers.back().num_nodes;
  sink2.indices_per_node = sink2.span_size;
  sink2.weights.resize(sink2.indices_per_node * sink2.num_nodes);
  sink2.biases.resize(sink2.num_nodes);
  sink2.weight_update = WEIGHT_UPDATE;
  sink2.weights_aux.resize(sink2.weights.size() * 2, 0.0f);
  sink2.biases_aux.resize(sink2.biases.size() * 2, 0.0f);
  sink2.fixed = false;
  sink2.width = sink2.num_nodes;
  sink2.height = 1;
  sink2.channels = 1;

  layers.push_back(Network::LayerFromChunks(std::move(sink1), std::move(sink2)));
  CHECK(layers.back().num_nodes == OUTPUT_SIZE);

  auto net = std::make_unique<Network>(layers);

  printf("Randomize..\n");
  RandomizeNetwork(&rc, net.get(), 2);
  printf("New network with %lld parameters\n", net->TotalParameters());
  return net;
}


int main(int argc, char **argv) {
  cl = new CL;

  std::unique_ptr<Network> net(
      Network::ReadFromFile(MODEL_NAME));

  if (net.get() == nullptr) {
    net = NewEvalNetwork();
    CHECK(net.get() != nullptr);
    net->SaveToFile(MODEL_NAME);
    printf("Wrote to %s\n", MODEL_NAME);
  }

  net->StructuralCheck();
  net->NaNCheck(MODEL_NAME);

  Train(net.get());

  printf("OK\n");
  return 0;
}
