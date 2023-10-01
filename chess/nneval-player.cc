
#include <string>
#include <memory>
#include <cstdint>
#include <mutex>
#include <map>

#include "../cc-lib/base/logging.h"
#include "../cc-lib/base/stringprintf.h"

#include "player.h"
#include "chess.h"
#include "player-util.h"
#include "threadutil.h"
#include "image.h"

#include "../pluginvert/network.h"

using int64 = int64_t;

using Move = Position::Move;
using namespace std;

static constexpr bool VERBOSE = false;

namespace {

// TODO: If this gets beyond an experiment, we should snapshot the Network
// code in here. Note that the code changed a lot since blind/lowercase
// (e.g. chunks and support for convolutions) so it's actually different
// than those, and some evidence that this snapshotting approach is not
// paranoia!

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

static constexpr int INPUT_SIZE = BOARD_SIZE;


// PERF: Don't let this grow without bound.
// PERF: Is it even helpful to cache positions?
struct ScoreCache {
  std::optional<std::pair<float, float>> Get(const Position &pos) {
    ReadMutexLock ml(&m);
    auto it = cache.find(pos);
    if (it == cache.end()) return {};
    else return {it->second};
  }

  void Insert(const Position &pos, std::pair<float, float> eval) {
    WriteMutexLock ml(&m);
    cache[pos] = eval;
  }

private:
  std::shared_mutex m;
  std::unordered_map<Position, std::pair<float, float>,
                     PositionHash, PositionEq> cache;
};

// One model/cache pair for each index. Never freed.
static std::mutex m;
static std::map<int, std::pair<Network *, ScoreCache *>> singletons;

struct NNEvalPlayer : public StatelessPlayer {
  Network *network = nullptr;
  ScoreCache *score_cache = nullptr;

  explicit NNEvalPlayer(int modelnum) : modelnum(modelnum) {
    MutexLock ml(&m);
    auto it = singletons.find(modelnum);
    if (it == singletons.end()) {
      singletons[modelnum].first = Network::ReadFromFile(
          StringPrintf("eval%d.val", modelnum));
      singletons[modelnum].second = new ScoreCache;
    }
    std::tie(network, score_cache) = singletons[modelnum];
    CHECK(network != nullptr);
    CHECK(score_cache != nullptr);
  }

  std::pair<float, float> GetScore(const Position &pos) {
    if (std::optional<std::pair<float, float>> fo = score_cache->Get(pos)) {
      return fo.value();
    }

    Stimulation stim{*network};
    BoardVecTo(pos, &stim.values[0], 0);
    network->RunForward(&stim);
    const float eval = stim.values.back()[0];
    const float over = stim.values.back()[1];
    const std::pair<float, float> res = make_pair(eval, over);
    score_cache->Insert(pos, res);
    return res;
  }


  bool IsDeterministic() const override { return true; }

  Move MakeMove(const Position &orig_pos, Explainer *explainer) override {
    Position pos = orig_pos;
    std::vector<Position::Move> legal = pos.GetLegalMoves();

    // Compute score for each move.
    std::vector<std::pair<Move, std::pair<float, float>>> results;
    results.reserve(legal.size());
    for (Move m : legal) {
      results.emplace_back(
          m,
          pos.MoveExcursion(m,
                            [this, &pos]() {
                              return GetScore(pos);
                            }));
    }

    // We only use the score to sort, but it might make sense to weigh
    // the 'how much is this game over' prediction (second element) as well?
    auto PreferBlack = [](const std::pair<Move, std::pair<float, float>> &a,
                          const std::pair<Move, std::pair<float, float>> &b) {
        // Want lower score, to favor black.
        return a.second.first < b.second.first;
      };

    auto PreferWhite = [](const std::pair<Move, std::pair<float, float>> &a,
                          const std::pair<Move, std::pair<float, float>> &b) {
        // Want higher score, to favor white.
        return a.second.first > b.second.first;
      };

    const Position::Move best_move =
      (pos.BlackMove() ?
       PlayerUtil::GetBest(results, PreferBlack) :
       PlayerUtil::GetBest(results, PreferWhite)).first;


    if (explainer != nullptr) {
      std::sort(results.begin(), results.end(),
                pos.BlackMove() ? PreferBlack : PreferWhite);

      std::vector<std::tuple<Position::Move, int64_t, std::string>> explained;
      for (const auto &[move, res] : results) {
        const auto &[score, over] = res;
        double fs = score * 100000000.0 * over;
        int64_t penalty = (int64_t)(pos.BlackMove() ? fs : -fs);
        explained.emplace_back(move, penalty, StringPrintf("%+.4f %.3f", score, over));
      }

      explainer->SetScoredMoves(explained);
    }

    return best_move;
  }

  string Name() const override {
    return StringPrintf("nneval%d", modelnum);
  }

  string Desc() const override {
    return StringPrintf(
        "Take the next move with the most favorable "
        "evaluation (i.e. negative if playing as black) "
        "as predicted by a neural network (#%d).", modelnum);
  }
private:
  const int modelnum;
};

}  // namespace

Player *NNEval(int modelnum) {
  return new MakeStateless<NNEvalPlayer, int>(modelnum);
}


