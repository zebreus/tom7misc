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
#include "timer.h"
#include "periodically.h"

using namespace std;

static CL *cl = nullptr;

using int64 = int64_t;
using uint64 = uint64_t;
using uint8 = uint8_t;

static constexpr WeightUpdate WEIGHT_UPDATE = ADAM;

static constexpr const char *GAME_PGN = "d:\\chess\\lichess_db_standard_rated_2020-06.pgn";

#define MODEL_BASE "chess"
#define MODEL_NAME MODEL_BASE ".val"

// 8x8x13 one-hot, then 4x castling bits,
// 8x en passant state
static constexpr int SQUARE_SIZE = 13;
static constexpr int BOARD_SIZE = 8 * 8 * SQUARE_SIZE + 4 + 8;
static constexpr int OTHER_STATE_IDX = 8 * 8 * SQUARE_SIZE;
static constexpr int OTHER_STATE_SIZE = 4 + 8;

// Maps eval scores to [-1, +1]. The range [-0.75, +0.75] is used
// for standard pawn score (no mate found); the rest for mate.
static float MapEval(const PGN::Eval &eval) {
  switch (eval.type) {
  case PGN::EvalType::PAWNS: {
    // The nominal range is [0, 64];
    float f = std::clamp(eval.e.pawns, -64.0f, 64.0f);
    return f * (0.75f / 64.0f);
  }
  case PGN::EvalType::MATE: {
    // Not really any limit to the depth of a mate we could
    // discover, so this asymptotically approaches 0. A mate
    // in 1 has the highest bonus, 0.25f.

    const int depth = abs(eval.e.mate) - 1;

    // This scale is abitrary; we just want something that
    // considers mate in 5 better than mate in 6, and so on.
    static constexpr float SCALE = 0.1f;
    float depth_bonus =
      (1.0f - (1.0f / (1.0f + expf(-SCALE * depth)))) * 0.5f;
    return eval.e.mate > 0 ?
      0.75f + depth_bonus : -0.75f - depth_bonus;
  }

  default:
    CHECK(false) << "Bad/unimplemented eval type";
    return 0.0f;
  }
}

// Write into the output vector starting at the given index; the space must
// already be reserved.
//
// For this experiment, the board must have white to move. Caller should
// flip the position if it's black's move.
static void BoardVecTo(const Position &pos, std::vector<float> *out, int idx) {
  // Flip the board first.
  CHECK(!pos.BlackMove());

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

// Loads example positions from a database in the background.
static std::mutex pool_mutex;
// position, eval, over. We don't use "over" for this experiment.
static std::vector<std::tuple<Position, float, float>> example_pool;
static void PopulateExamples() {
  PGNParser parser;
  printf("Loading examples from %s...\n", GAME_PGN);
  int64 num_games = 0, num_positions = 0, parse_errors = 0;
  PGNTextStream ts(GAME_PGN);
  string pgn_text;
  while (ts.NextPGN(&pgn_text)) {
    PGN pgn;
    if (parser.Parse(pgn_text, &pgn)) {
      if (!pgn.moves.empty() && pgn.moves[0].eval.has_value()) {
        Position pos;
        for (int i = 0; i < pgn.moves.size(); i++) {
          const PGN::Move &m = pgn.moves[i];

          Position::Move move;
          if (!pos.ParseMove(m.move.c_str(), &move)) {
            parse_errors++;
            goto next_game;
          }

          pos.ApplyMove(move);

          // Eval applies to position after move.
          if (m.eval.has_value()) {
            const float score = MapEval(m.eval.value());
            const float over = i / (float)pgn.moves.size();

            {
              MutexLock ml(&pool_mutex);
              example_pool.emplace_back(pos, score, over);
            }
            num_positions++;
          }
        }
        num_games++;
        if (num_games % 10000 == 0) {
          printf("Read %lld games, %lld pos from %s\n",
                 num_games, num_positions, GAME_PGN);
        }
      }
    } else {
      parse_errors++;
    }
  next_game:;
  }

  printf("Done loading examples from %s.\n"
         "%lld games, %lld positions, %lld errors\n",
         GAME_PGN, num_games, num_positions, parse_errors);
}


static constexpr int INPUT_SIZE = BOARD_SIZE;
// Just the evaluation.
static constexpr int OUTPUT_SIZE = 1;

static constexpr int EXAMPLES_PER_ROUND = 2048;

// In-progress game. We keep a few of these around and use them to
// generate training data. It is somewhat convoluted because we want
// to evaluate a bunch of board states (across multiple games) in
// a batch for efficiency.
struct Game {
  enum class Result {
    WHITE_WINS,
    BLACK_WINS,
    DRAW_BY_REPETITION,
    DRAW_50_MOVES,
    DRAW_STALEMATE,
    // ...
  };

  static const char *ResultString(Result r) {
    switch (r) {
    case Result::WHITE_WINS: return "WHITE";
    case Result::BLACK_WINS: return "BLACK";
    case Result::DRAW_BY_REPETITION: return "REPETITION";
    case Result::DRAW_50_MOVES: return "50 MOVES";
    case Result::DRAW_STALEMATE: return "STALEMATE";
    default: return "??";
    }
  };

  Game() {
    legal = pos.GetLegalMoves();
    // Not that we are trying to be precise about the
    // stalemate rules here, but it is technically not
    // FIDE rules to consider the initial position for
    // repetition!
    // position_counts[pos]++;
    reached.push_back(pos);
  }

  // Returns the result if the game has ended.
  std::optional<Result> GetResult() {
    // We use threefold repetition (our player "claims" the draw).
    const bool draw_by_repetition = position_counts[pos] >= 3;
    const bool draw_50_moves = stale_moves > 100;
    // also need to check draw by repetition, counter, and (dead position?)
    if (draw_by_repetition || draw_50_moves || legal.empty()) {
      // Game has ended. If it's black's move and we're mated, this is a
      // win for white (+1), etc.
      if (draw_by_repetition) {
        return {Result::DRAW_BY_REPETITION};
      } else if (draw_50_moves) {
        return {Result::DRAW_50_MOVES};
      } else if (pos.IsMated()) {
        if (pos.BlackMove()) {
          return {Result::WHITE_WINS};
        } else {
          return {Result::BLACK_WINS};
        }
      } else {
        return {Result::DRAW_STALEMATE};
      }
    }

    return std::nullopt;
  }

  void MakeMove(int n) {
    CHECK(n >= 0 && n < legal.size());

    stale_moves++;
    Position::Move m = legal[n];
    if (pos.IsCapturing(m) ||
        pos.IsPawnMove(m)) {
      stale_moves = 0;
      position_counts.clear();
    }
    pos.ApplyMove(m);
    legal = pos.GetLegalMoves();
    position_counts[pos]++;
    reached.push_back(pos);
  }

  Position pos;

  // For detecting draws by repetition.
  std::unordered_map<Position, int,
                     PositionHash, PositionEq> position_counts;
  // Count of moves without pawn move or capture. We probably aren't
  // handling the end conditions correctly here, but it doesn't
  // matter that much.
  int stale_moves = 0;

  // This is the history of the game, used for generating the training
  // examples at the end.
  std::vector<Position> reached;

  // Number of legal moves in the current position.
  std::vector<Position::Move> legal;

};

static void Train(const string &dir, Network *net, int64 max_rounds) {
  std::thread example_thread(&PopulateExamples);

  ArcFour rc(StringPrintf("%lld.train.%s", time(nullptr), MODEL_BASE));

  const string error_history_file = Util::dirplus(dir, "error-history.tsv");
  const string model_file = Util::dirplus(dir, MODEL_NAME);

  ErrorHistory error_history(error_history_file);

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
  update_config.adam_epsilon = 1.0e-2;

  // XXX this should probably depend on the learning rate; if the
  // learning rate is too small, it won't even be able to overcome
  // the decay
  static constexpr float DECAY_RATE = 0.999999f;
  static constexpr bool DO_DECAY = false;

  // On a verbose round we compute training error and print out
  // examples.
  static constexpr int VERBOSE_EVERY_SEC = 60.0;
  Periodically verbose_per(VERBOSE_EVERY_SEC);

  // How often to also save error history to disk (if we run a
  // verbose round).
  static constexpr double HISTORY_EVERY_SEC = 120.0;
  Periodically history_per(HISTORY_EVERY_SEC);

  static constexpr double SAVE_ERROR_IMAGES_EVERY = 130.0;
  Periodically error_images_per(SAVE_ERROR_IMAGES_EVERY);

  static constexpr double TIMING_EVERY_SEC = 20.0;
  Periodically timing_per(TIMING_EVERY_SEC);

  static constexpr int64 CHECKPOINT_EVERY_ROUNDS = 100000;

  constexpr int IMAGE_EVERY = 100;
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

  static constexpr double SAVE_EVERY_SEC = 180.0;
  Periodically save_per(SAVE_EVERY_SEC);

  // Stats from self-play.
  int white_wins = 0, black_wins = 0,
    repetition_draws = 0, fifty_draws = 0, stalemate_draws = 0;

  double round_ms = 0.0;
  double example_ms = 0.0;
  double expand_ms = 0.0;
  double makemove_ms = 0.0;
  double eval_ms = 0.0;
  double forward_ms = 0.0;
  double error_ms = 0.0;
  double decay_ms = 0.0;
  double backward_ms = 0.0;
  double update_ms = 0.0;
  double loss_ms = 0.0;
  double image_ms = 0.0;

  Timer train_timer;
  int64 total_examples = 0LL;

  for (int iter = 0; net->rounds < max_rounds; iter++) {
    Timer round_timer;

    const bool verbose_round = verbose_per.ShouldRun();

    // Initialize training examples.

    std::vector<float> inputs;
    inputs.resize(EXAMPLES_PER_ROUND * INPUT_SIZE);
    std::vector<float> expecteds;
    expecteds.resize(EXAMPLES_PER_ROUND * OUTPUT_SIZE);

    {
      // Examples from database.
      Timer example_timer;

      static constexpr int MIN_EXAMPLES = 100000;
      const int num_available = [](){
          for (;;) {
            {
              MutexLock ml(&pool_mutex);
              if (example_pool.size() >= MIN_EXAMPLES) {
                return example_pool.size();
              }
            }
            printf("Waiting until we have enough examples..\n");
            std::this_thread::sleep_for(2s);
          }
        }();

      {
        MutexLock ml(&pool_mutex);
        for (int i = 0; i < EXAMPLES_PER_ROUND; i++) {
          const int idx = RandTo(&rc, num_available);
          const auto &[pos, score, over] = example_pool[idx];
          if (i == 0 && verbose_round) {
            printf("Eval %.5f Over %.3f:\n"
                   "%s"
                   "%s\n\n",
                   score, over, pos.BoardString().c_str(),
                   pos.ToFEN(1, 1).c_str());
          }

          // We train only on white-to-move, so flip the
          // board (and the eval) if it's black's move.
          if (pos.BlackMove()) {
            Position rev = Position::FlipSides(pos);
            BoardVecTo(rev, &inputs, INPUT_SIZE * i);
            expecteds[i * OUTPUT_SIZE + 0] = -score;
          } else {
            BoardVecTo(pos, &inputs, INPUT_SIZE * i);
            expecteds[i * OUTPUT_SIZE + 0] = score;
          }
        }
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

      const double total_sec = train_timer.MS() / 1000.0;
      const double eps = total_examples / total_sec;
      printf("%lld|%d: %d wh %d bl %d rep %d fifty %d stale (%.2f eps)\n",
             net->rounds, iter,
             white_wins, black_wins, repetition_draws,
             fifty_draws, stalemate_draws, eps);

      // Get loss as abs distance, plus number of incorrect (as booleans).
      std::vector<float> actuals(EXAMPLES_PER_ROUND * OUTPUT_SIZE);
      training->ExportOutputs(&actuals);

      const bool save_history = history_per.ShouldRun();
      const bool save_error_images = error_images_per.ShouldRun();
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
      }

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

    const bool save_timeout = save_per.ShouldRun();

    // Checkpoint (no overwrite) every X rounds.
    bool checkpoint_timeout = (net->rounds % CHECKPOINT_EVERY_ROUNDS) == 0;

    if (SAVE_INTERMEDIATE && (save_timeout || checkpoint_timeout || finished ||
                              iter % 5000 == 0)) {
      net_gpu->ReadFromGPU();
      // Note that if we write a checkpoint, we don't also overwrite
      // the main model, which is less redundant but might be surprising?
      const string filename = checkpoint_timeout ?
        StringPrintf("%s.%lld.val",
                     Util::dirplus(dir, MODEL_BASE).c_str(),
                     net->rounds) :
        (string)MODEL_NAME;
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

    // Parameter for average_loss termination?
    if (finished) {
      printf("Successfully trained!\n");
      return;
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
      printf("%.1fms [%.1f+%.1f+%.1f] ex  "
             "%.1fms fwd  "
             "%.1fms err  "
             "%.1fms dec  "
             "%.1fms bwd  "
             "%.1fms up  "
             "%.2fms loss  "
             "%.2fms img  "
             "%.1fms other\n",
             example_ms * msr,
             /* */
             expand_ms * msr,
             eval_ms * msr,
             makemove_ms * msr,
             /* */
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

  printf("Reached max_rounds of %lld.\n", max_rounds);
  net->SaveToFile(model_file);
  printf("Saved to %s.\n", model_file.c_str());
}

static unique_ptr<Network> NewChessNetwork(TransferFunction tf) {
  printf("New network with transfer function %s\n",
         TransferFunctionName(tf));

  // Deterministic!
  ArcFour rc("learn-chess-network");

  std::vector<Layer> layers;

  Chunk input_chunk;
  input_chunk.type = CHUNK_INPUT;
  input_chunk.num_nodes = INPUT_SIZE;
  input_chunk.width = INPUT_SIZE;
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
        0, 8 * SQUARE_SIZE, 8,
        // features, pattern size
        FEATURES_3x3, 3 * SQUARE_SIZE, 3,
        // stride is one square, but align with the one-hot pieces
        SQUARE_SIZE, 1,
        tf, WEIGHT_UPDATE);

  // Also a 1x1.
  Chunk square_conv_chunk =
    Network::Make2DConvolutionChunk(
        0, 8 * SQUARE_SIZE, 8,
        FEATURES_1x1, SQUARE_SIZE, 1,
        SQUARE_SIZE, 1,
        tf, WEIGHT_UPDATE);

  // And 8x1, 1x8.
  Chunk row_conv_chunk =
    Network::Make2DConvolutionChunk(
        0, 8 * SQUARE_SIZE, 8,
        FEATURES_8x1, 8 * SQUARE_SIZE, 1,
        8 * SQUARE_SIZE, 1,
        tf, WEIGHT_UPDATE);
  Chunk col_conv_chunk =
    Network::Make2DConvolutionChunk(
        0, 8 * SQUARE_SIZE, 8,
        FEATURES_1x8, SQUARE_SIZE, 8,
        SQUARE_SIZE, 8,
        tf, WEIGHT_UPDATE);

  vector<Network::SparseSpan> sparse_spans = {
    // Sample from the board.
    Network::SparseSpan(0, 8 * 8 * SQUARE_SIZE, 63),
    // And one from the less-interesting state.
    Network::SparseSpan(OTHER_STATE_IDX, OTHER_STATE_SIZE, 1),
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
  CHECK(layers.back().num_nodes == OUTPUT_SIZE);

  auto net = std::make_unique<Network>(layers);

  printf("Randomize..\n");
  RandomizeNetwork(&rc, net.get(), 2);
  printf("New network with %lld parameters\n", net->TotalParameters());
  return net;
}


int main(int argc, char **argv) {

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

  cl = new CL;

  const string model_file = Util::dirplus(dir, MODEL_NAME);

  std::unique_ptr<Network> net(
      Network::ReadFromFile(model_file));

  if (net.get() == nullptr) {
    net = NewChessNetwork(tf);
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
    Train(dir, net.get(), max_rounds);
  }

  printf("OK\n");
  return 0;
}
