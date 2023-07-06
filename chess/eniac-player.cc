
#include "eniac-player.h"

#include <optional>

#include "player.h"
#include "chess.h"

#include "arcfour.h"
#include "player-util.h"
#include "eniac.h"

using Move = Position::Move;
using namespace std;

namespace {

// Parse a move like a2a3, or a7a8q for promotion.
// Does not check that the move is valid/legal.
static std::optional<Move> ParseLongMove(string m) {
  if (m.size() < 4 || m.size() > 5) return {};

  // lowercase letters
  m[0] |= 32;
  m[2] |= 32;
  if (m.size() == 5) m[4] |= 32;

  if (m[0] < 'a' || m[0] > 'h') return {};
  if (m[1] < '1' || m[1] > '8') return {};
  if (m[2] < 'a' || m[2] > 'h') return {};
  if (m[3] < '1' || m[3] > '8') return {};
  Move move;
  if (m.size() == 5) {
    switch (m[4]) {
    case 'q': move.promote_to = Position::QUEEN; break;
    case 'n': move.promote_to = Position::KNIGHT; break;
    case 'r': move.promote_to = Position::ROOK; break;
    case 'b': move.promote_to = Position::BISHOP; break;
    default:
      return {};
    }
  }

  move.src_col = m[0] - 'a';
  move.src_row = 7 - (m[1] - '1');
  move.dst_col = m[2] - 'a';
  move.dst_row = 7 - (m[3] - '1');
  return {move};
}

struct EniacPlayer : public StatelessPlayer {
  EniacPlayer() : rc(PlayerUtil::GetSeed()) {
    rc.Discard(800);
  }

  // Plays using the standalone ENIAC engine, with a fallback to
  // random moves if it fails (I think it doesn't implement all rules,
  // etc.)
  Move MakeMove(const Position &orig_pos, Explainer *explainer) override {
    Position pos = orig_pos;

    auto RandomMove = [this, explainer, &pos](const string &why) {
        if (explainer) explainer->SetMessage(why);
        std::vector<Move> legal = pos.GetLegalMoves();
        CHECK(!legal.empty());
        return legal[RandTo32(&rc, legal.size())];
      };

    // Assuming ENIAC engine doesn't care about move counts.
    const string fen = pos.ToFEN(1, 1);
    const string eniac_move = eniac_chess_move(fen);
    optional<Move> moveo = ParseLongMove(eniac_move);

    if (!moveo.has_value()) return RandomMove("no move");
    const Move move = moveo.value();

    if (!pos.IsLegal(move))
      return RandomMove(
          StringPrintf("illegal: eniac [%s] parsed to [%s]",
                       eniac_move.c_str(),
                       Position::DebugMoveString(move).c_str()));
    return move;
  }

  string Name() const override { return "eniac"; }
  string Desc() const override {
    return "Run a contributed ENIAC chess engine in simulation, or "
      "play a random move if it fails.";
  }

  ArcFour rc;
};

}  // namespace

Player *Eniac() {
  return new MakeStateless<EniacPlayer>;
}
