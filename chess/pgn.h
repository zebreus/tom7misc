
#ifndef _CHESS_PGN_H
#define _CHESS_PGN_H

#include <unordered_map>
#include <vector>
#include <string>
#include <optional>

#include "re2.h"

struct PGN {

  enum class Result {
    WHITE_WINS,
    BLACK_WINS,
    DRAW,
    OTHER,
  };

  enum class Termination {
    NORMAL,
    TIME_FORFEIT,
    ABANDONED,
    OTHER,
  };

  enum class TimeClass {
    ULTRA_BULLET,
    BULLET,
    BLITZ,
    RAPID,
    CLASSICAL,
    // TimeControl "-"
    CORRESPONDENCE,
    UNKNOWN,
  };

  enum class EvalType {
    PAWNS,
    MATE,
  };

  struct Eval {
    EvalType type = EvalType::MATE;
    union {
      // Score in (nominal) pawns. +2.1 means that white has a 2.1 pawn
      // advantage.
      float pawns;
      // Mate in a certain number of moves. This is exact. -3 means that
      // black has a mate in 3 (full) moves.
      int mate = 0;
    } e;
  };

  struct Move {
    Move(std::string m,
         std::optional<int> clock,
         std::optional<Eval> eval) : move(std::move(m)),
                                     clock(clock),
                                     eval(eval) {}
    // The actual move, like "Nxh4".
    std::string move;
    // TODO: If present, annotations like clock, eval.
    // If present, the clock annotation (number of seconds remaining for
    // the side that made the move).
    std::optional<int> clock;
    std::optional<Eval> eval;
  };

  // If you are parsing a large number of PGNs, it is slightly
  // faster to make a PGNParser instance and reuse it.
  static bool Parse(const std::string &s, PGN *pgn);

  // Parse just a series of moves, like "1. d4 d5 2. Nf3" into
  // ["d4", "d5", "Nf4"]. Termination like "1-0" is ignored.
  // Returns true and appends to 'moves' if successful.
  static bool ParseMoves(const std::string &s, std::vector<Move> *moves);

  std::unordered_map<std::string, std::string> meta;
  int MetaInt(const std::string &key, int default_value = 0) const;
  Termination GetTermination() const;

  // Gives {number of starting seconds per side, increment per side}.
  // Returns {0, 0} if not specified; also used for correspondence
  // games ([TimeControl "-"]).
  std::pair<int, int> GetTimeControl() const;
  TimeClass GetTimeClass() const;

  // The moves of the game. White moves are at even indices.
  // Does not include the terminating event like 1-0.
  std::vector<Move> moves;
  Result result;
};

struct PGNParser {
  // Parses a subset of the PGN language. Returns false upon failure.
  bool Parse(const std::string &s, PGN *pgn) const;
  PGNParser();

private:
  const RE2 meta_line_re, move_re, comment_re, clock_re, eval_re, end_re;
};

#endif
