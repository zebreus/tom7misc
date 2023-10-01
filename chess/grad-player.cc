
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

#include "../grad/network.h"

#include "../grad/nnchess.h"

using int64 = int64_t;

using Move = Position::Move;
using namespace std;

static constexpr bool VERBOSE = false;

namespace {

// TODO: Again: If this gets beyond an experiment, we should snapshot
// the Network code in here. Note that the code changed a lot since
// blind/lowercase (e.g. chunks and support for convolutions) so it's
// actually different than those, and some evidence that this
// snapshotting approach is not paranoia!

// PERF: Don't let this grow without bound.
// PERF: Is it even helpful to cache positions?
struct ScoreCache {
  std::optional<float> Get(const Position &pos) {
    ReadMutexLock ml(&m);
    auto it = cache.find(pos);
    if (it == cache.end()) return {};
    else return {it->second};
  }

  void Insert(const Position &pos, float eval) {
    WriteMutexLock ml(&m);
    cache[pos] = eval;
  }

private:
  std::shared_mutex m;
  std::unordered_map<Position, float, PositionHash, PositionEq> cache;
};

// One model/cache pair for each model filename. Never freed.
static std::mutex m;
static std::map<std::string, Network *> models;
// indexed by DETECT_END bool too, as they have different scores
static std::map<std::string, ScoreCache *> scorecaches[2];

template<bool DETECT_END>
struct GradPlayer : public StatelessPlayer {
  Network *network = nullptr;
  // We only store scores with white to move, like the model predicts.
  ScoreCache *score_cache = nullptr;

  GradPlayer(const std::string &name,
             const std::string &modelfile) :
    name(DETECT_END ? name + "_fix" : name),
    modelfile(modelfile) {
    MutexLock ml(&m);
    auto nit = models.find(modelfile);
    if (nit == models.end()) {
      models[modelfile] = Network::ReadFromFile(modelfile);
    }

    const int scidx = DETECT_END ? 1 : 0;
    auto sit = scorecaches[scidx].find(modelfile);
    if (sit == scorecaches[scidx].end()) {
      scorecaches[scidx][modelfile] = new ScoreCache;
    }

    network = models[modelfile];
    score_cache = scorecaches[scidx][modelfile];
    CHECK(network != nullptr);
    CHECK(score_cache != nullptr);
  }

  float GetWhiteToMoveScore(const Position &pos) {
    CHECK(!pos.BlackMove());
    if (std::optional<float> fo = score_cache->Get(pos)) {
      return fo.value();
    }

    // Due to a bug in training data generation, the models are never
    // trained on checkmate or stalemate positions. But these are
    // mechanical (like "is a move legal"), so in this mode, we just
    // detect those and give them the proper scores.
    if constexpr (DETECT_END) {
      Position copy = pos;
      if (!copy.HasLegalMoves()) {
        // Game has ended.
        if (copy.IsMated()) {
          // I'm mated.
          return -9999.0f;
        } else {
          // Must be stalemate then.
          return 0.0f;
        }
      }
    }

    Stimulation stim{*network};
    NNChess::BoardVecTo(pos, &stim.values[0], 0);
    network->RunForward(&stim);
    const float eval = stim.values.back()[0];
    score_cache->Insert(pos, eval);
    return eval;
  }

  float GetScore(const Position &pos) {
    // We train only on white-to-move, so flip the
    // board (and the eval) if it's black's move.
    if (pos.BlackMove()) {
      Position rev = Position::FlipSides(pos);
      return -GetWhiteToMoveScore(rev);
    } else {
      return GetWhiteToMoveScore(pos);
    }
  }

  bool IsDeterministic() const override { return true; }

  Move MakeMove(const Position &orig_pos, Explainer *explainer) override {
    Position pos = orig_pos;
    std::vector<Position::Move> legal = pos.GetLegalMoves();

    // Compute score for each move.
    std::vector<std::pair<Move, float>> results;
    results.reserve(legal.size());
    for (Move m : legal) {
      results.emplace_back(
          m,
          pos.MoveExcursion(m,
                            [this, &pos]() {
                              return GetScore(pos);
                            }));
    }

    auto PreferBlack = [](const std::pair<Move, float> &a,
                          const std::pair<Move, float> &b) {
        // Want lower score, to favor black.
        return a.second < b.second;
      };

    auto PreferWhite = [](const std::pair<Move, float> &a,
                          const std::pair<Move, float> &b) {
        // Want higher score, to favor white.
        return a.second > b.second;
      };

    const Position::Move best_move =
      (pos.BlackMove() ?
       PlayerUtil::GetBest(results, PreferBlack) :
       PlayerUtil::GetBest(results, PreferWhite)).first;


    if (explainer != nullptr) {
      std::sort(results.begin(), results.end(),
                pos.BlackMove() ? PreferBlack : PreferWhite);

      std::vector<std::tuple<Position::Move, int64_t, std::string>> explained;
      for (const auto &[move, eval] : results) {
        double fs = eval * 1000.0;
        int64_t penalty = (int64_t)(pos.BlackMove() ? fs : -fs);
        explained.emplace_back(move, penalty,
                               StringPrintf("%+.4f", eval));
      }

      explainer->SetScoredMoves(explained);
    }

    return best_move;
  }

  string Name() const override {
    return name;
  }

  string Desc() const override {
    return StringPrintf(
        "Take the next move with the most favorable "
        "evaluation (i.e. negative if playing as black) "
        "as predicted by the 'grad' neural network %s.", name.c_str());
  }
private:
  const std::string name, modelfile;
};

}  // namespace

Player *GradEval(const std::string &name, const std::string &modelfile) {
  return new MakeStateless<GradPlayer<false>, std::string, std::string>(
      name, modelfile);
}

Player *GradEvalFix(const std::string &name, const std::string &modelfile) {
  return new MakeStateless<GradPlayer<true>, std::string, std::string>(
      name, modelfile);
}


