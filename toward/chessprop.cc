
#include "chessprop.h"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "chess.h"
#include "prop.h"
#include "randutil.h"
#include "small-int-set.h"

using Board = ChessProp::Board;
using enum ChessProp::Type;
using Details = ChessProp::Details;

using SquareSet = SmallIntSet<64>;

static constexpr bool VERBOSE = false;

static constexpr uint8_t PAWN = 0;
static constexpr uint8_t KNIGHT = 1;
static constexpr uint8_t BISHOP = 2;
static constexpr uint8_t ROOK = 3;
static constexpr uint8_t QUEEN = 4;
static constexpr uint8_t KING = 5;

// such that BLACK_START + PIECE = BLACK_PIECE.
static constexpr uint8_t BLACK_START = 0;
static constexpr uint8_t WHITE_START = 6;

std::string_view ChessProp::ShortType(uint8_t t) {
  switch (t) {
  case BLACK_PAWN: return "p";
  case BLACK_KNIGHT: return "n";
  case BLACK_BISHOP: return "b";
  case BLACK_ROOK: return "r";
  case BLACK_QUEEN: return "q";
  case BLACK_KING: return "k";
  case WHITE_PAWN: return "P";
  case WHITE_KNIGHT: return "N";
  case WHITE_BISHOP: return "B";
  case WHITE_ROOK: return "R";
  case WHITE_QUEEN: return "Q";
  case WHITE_KING: return "K";
  case EMPTY: return "o";
  default:
    LOG(FATAL) << "Bad type";
    return "??";
  }
}

Board ChessProp::NewBoard(World *world) {
  size_t start = world->symbol_names.size();
  world->symbol_names.resize(start + NUM_BOARD_PROPS);
  Board board;
  board.props.resize(NUM_BOARD_PROPS);
  auto SetSym = [start, world, &board](int idx, std::string_view s) {
      CHECK(world->symbol_names[start + idx].empty()) <<
        start << " + " << idx << "... already has " <<
        world->symbol_names[start + idx];
      world->symbol_names[start + idx] = std::move(s);
      board.props[idx] = {Var{.id = (int)start + idx}};
    };

  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      // For each square, we use a one-hot
      // representation for its contents.
      for (int t = 0; t < NUM_TYPES; t++) {
        SetSym(HasContentsIdx(row, col, t),
               std::format("{:c}{:c}_{}",
                           'a' + col,
                           '1' + (7 - row),
                           ShortType(t)));
      }
    }
  }

  // en passant bits. one per column.
  for (int c = 0; c < 8; c++) {
    SetSym(EnPassantColIdx(c),
           std::format("ep_{:c}", 'a' + c));
  }

  for (bool white : { false, true }) {
    for (bool kingside : { false, true}) {
      SetSym(CastlingIdx(white, kingside),
             std::format("cast_{:c}",
                         white ?
                         ("QK"[kingside]) :
                         ("qk"[kingside])));
    }
  }

  SetSym(CheckIdx(), "in_check");

  return board;
}

static void SetPiece(Board *board, int r, int c, int t) {
  CHECK(r >= 0 && r < 8 && c >= 0 && c < 8);
  for (int type = 0; type < ChessProp::NUM_TYPES; type++) {
    if (type == t) {
      board->props[ChessProp::HasContentsIdx(r, c, type)] = True();
    } else {
      board->props[ChessProp::HasContentsIdx(r, c, type)] = False();
    }
  }
}

inline Prop HasContents(const Board &board, int r, int c, int t) {
  CHECK(r >= 0 && r < 8 && c >= 0 && c < 8);
  return board.props[ChessProp::HasContentsIdx(r, c, t)];
}

inline Prop EnPassantCol(const Board &board, int c) {
  CHECK(c >= 0 && c < 8);
  return board.props[ChessProp::EnPassantColIdx(c)];
}

inline Prop Castling(const Board &board, bool w, bool k) {
  return board.props[ChessProp::CastlingIdx(w, k)];
}

inline Prop IsEmpty(const Board &board, int r, int c) {
  CHECK(r >= 0 && r < 8 && c >= 0 && c < 8);
  return HasContents(board, r, c, EMPTY);
}

// Is there a black piece at r,c that can be captured? We don't
// include the king because no legal move would pose such a
// question, and it simplifes the expression.
inline Prop IsCapturable(const Board &board, int r, int c) {
  return Or(HasContents(board, r, c, BLACK_PAWN),
            HasContents(board, r, c, BLACK_KNIGHT),
            HasContents(board, r, c, BLACK_BISHOP),
            HasContents(board, r, c, BLACK_ROOK),
            HasContents(board, r, c, BLACK_QUEEN));
}

inline bool OnBoard(int r, int c) {
  return r >= 0 && c >= 0 && r < 8 && c < 8;
}

// For the Attacked checks, it is convenient to check outside
// the board as well; we know that this is false at "compilation"
// time.
inline Prop OnBoardAndHasContents(const Board &board,
                                  int r, int c, int t) {
  if (r < 0 || c < 0 || r >= 8 || c >= 8) return False();
  return HasContents(board, r, c, t);
}


// True if the square is attacked by a black piece.
// (Can't castle out of check, etc.)
Prop ChessProp::Attacked(const Board &board, int r, int c) {
  Prop knight = False();
  Prop king = False();
  for (int dr = -2; dr <= +2; dr++) {
    for (int dc = -2; dc <= +2; dc++) {

      int distr = std::abs(dr);
      int distc = std::abs(dc);

      // Must be an L-shaped move.
      if ((distr == 2 && distc == 1) ||
          (distr == 1 && distc == 2)) {
        knight = knight |
          OnBoardAndHasContents(board, r + dr, c + dc, BLACK_KNIGHT);
      }

      if (distr <= 1 && distc <= 1 &&
          (distr == 1 || distc == 1)) {
        king = king |
          OnBoardAndHasContents(board, r + dr, c + dc, BLACK_KING);
      }
    }
  }

  // Two specific squares attack a square with a pawn.
  Prop pawn =
    OnBoardAndHasContents(board, r - 1, c - 1, BLACK_PAWN) |
    OnBoardAndHasContents(board, r - 1, c + 1, BLACK_PAWN);

  // Trace out from the square to find a rook, bishop, or queen.
  Prop traced = False();

  for (int dr : {-1, 0, +1}) {
    for (int dc : {-1, 0, +1}) {

      // Most natural to build this from the bottom up.
      std::function<Prop(int, int)> TraceRec{[&](int r, int c) {
          // Base case.
          if (!OnBoard(r, c)) return False();

          Prop trace_rest = TraceRec(r + dr, c + dc);

          Prop attacked_from_here = False();

          if (dr == 0 || dc == 0) {
            // Rook (and queen).
            attacked_from_here =
              HasContents(board, r, c, BLACK_QUEEN) |
              HasContents(board, r, c, BLACK_ROOK);

          } else if (dr != 0 && dc != 0) {
            // Bishop (and queen).
            attacked_from_here =
              HasContents(board, r, c, BLACK_QUEEN) |
              HasContents(board, r, c, BLACK_BISHOP);

          } else {
            LOG(FATAL) << "Invalid trace direction.";
          }

          return attacked_from_here |
            (IsEmpty(board, r, c) & trace_rest);
        }
      };

      // All 8 directions are needed here, but not the zero vector.
      if (dr != 0 || dc != 0) {
        traced = traced | TraceRec(r + dr, c + dc);
      }
    }
  }

  return Or(pawn, knight, traced, king);
}

// Return the step (-1, 0, or 1) in the row and column directions for
// a queen-like move. Returns nullopt if this is not such a move.
static std::optional<std::pair<int, int>>
GetDir(int srcr, int srcc, int dstr, int dstc) {
  // (0, 0) is not a valid direction.
  if (srcr == dstr && srcc == dstc) return std::nullopt;
  if (srcr == dstr) {
    // Horizontal
    return {std::make_pair(0, srcc < dstc ? 1 : -1)};
  }
  if (srcc == dstc) {
    // Vertical
    return {std::make_pair(srcr < dstr ? 1 : -1, 0)};
  }

  int dr = dstr - srcr;
  int dc = dstc - srcc;
  if (dr == dc || dr == -dc) {
    // Diagonal
    return {std::make_pair(dr > 0 ? 1 : -1, dc > 0 ? 1 : -1)};
  }

  return std::nullopt;
}

static inline int Square(int r, int c) {
  return r * 8 + c;
}

static inline std::pair<int, int> UnSquare(int sq) {
  return std::make_pair(sq / 8, sq % 8);
}

// For horizontal and vertical move shapes, this is the inclusive set
// of squares between the source and destination. For other moves
// (e.g. knight, or invalid moves) it is the empty set.
static SquareSet GetRay(int srcr, int srcc, int dstr, int dstc) {
  CHECK(srcr >= 0 && srcc >= 0 && dstr >= 0 && dstc >= 0);
  CHECK(srcr < 8 && srcc < 8 && dstr < 8 && dstc < 8);

  std::optional<std::pair<int, int>> odir = GetDir(srcr, srcc,
                                                   dstr, dstc);

  if (!odir.has_value()) return SquareSet();

  SquareSet ret;
  const auto &[dr, dc] = odir.value();
  ret.Add(Square(srcr, srcc));
  for (int r = srcr + dr, c = srcc + dc; !(r == dstr && c == dstc); ) {
    ret.Add(Square(r, c));
    r += dr;
    c += dc;
  }
  ret.Add(Square(dstr, dstc));
  return ret;
}

static inline bool IsKnightMove(int srcr, int srcc, int dstr, int dstc) {
  // This is simpler than the above because we don't need to
  // check anything in between.
  int distr = std::abs(dstr - srcr);
  int distc = std::abs(dstc - srcc);

  // Must be an L-shaped move.
  return (distr == 2 && distc == 1) || (distr == 1 && distc == 2);
}

// Another approach: For a non-king and non-ep move, we compute the
// set of squares for which the king might have had its attacked
// status changed. This prevents us from having to test for
// pre-existing check everywhere on the board, even when moves aren't
// related; instead we just use the existing check status. For some
// moves (e.g. a2a3) the king cannot have a discovered check!
static SquareSet KingCheckCheck(
    // Places where the king might be.
    SquareSet king_squares,
    // Squares that we now know are empty.
    SquareSet cleared_squares,
    // Squares that were made into white non-king
    // material.
    SquareSet made_white_squares) {

  SquareSet check_kings;
  for (int ksq : king_squares) {
    if (cleared_squares.Contains(ksq)) continue;
    if (made_white_squares.Contains(ksq)) continue;

    // Supposing the king is here, where could it be
    // attacked from? Are any of those squares affected?
    const auto &[kr, kc] = UnSquare(ksq);

    for (int s = 0; s < 64; s++) {
      // Can't attack itself.
      if (s == ksq) continue;

      const auto &[sr, sc] = UnSquare(s);

      if (IsKnightMove(sr, sc, kr, kc)) {
        // For check given by a knight, it cannot be blocked. So it's
        // only affected if it was a made_white_square (capturing the
        // attacking piece).
        if (made_white_squares.Contains(s)) {
          check_kings.Add(ksq);
          goto next_k;
        }
      } else {
        SquareSet ray = GetRay(sr, sc, kr, kc);

        for (int rsq : ray) {
          // If the ray includes a cleared square or
          // a square made white, then this check could
          // be affected.
          // PERF: We can do better here, because if
          // the ray is completely included within
          // the attacking ray, we know it will still
          // be blocked.

          if (made_white_squares.Contains(rsq) ||
              cleared_squares.Contains(rsq)) {
            check_kings.Add(ksq);
            goto next_k;
          }
        }
      }
    }

  next_k:;
  }

  return check_kings;
}

// New approach to detecting check, which depends on knowing whether
// the king was in check before the move. It only checks the squares
// on which the king's check status might have changed.
//
// Returns true if the king is in check after this move. Must not be a
// king move. Pass the additional squares cleared other than
// the source (only for en passant).
static Prop KingAttackedAfter(
    const Board &board_before,
    int srcr, int srcc,
    int dstr, int dstc,
    SquareSet also_cleared = SquareSet::Bot()) {
  Prop was_in_check = board_before.props[ChessProp::CheckIdx()];

  // Set of squares that are cleared.
  SquareSet cleared = also_cleared;
  cleared.Add(Square(srcr, srcc));

  // The strategy here is to test all king squares. If check might
  // have changed at that square, then we do the full test. Otherwise
  // we just return the current value. Remember that even when we can
  // know that check was blocked (because we put a white piece in the
  // destination square), there is the possibility that the king is
  // in a double check. Although it is possible to reason about what
  // double-checks are possible, we aren't doing that, so we need
  // to just recompute the whole function.

  SquareSet king_squares = SquareSet::Top();
  // These squares are known to be empty now (so not the king).
  for (int csq : cleared) king_squares.Remove(csq);
  // It isn't a king move, so the king can't be here.
  king_squares.Remove(Square(dstr, dstc));
  // And for a queen-like move, the squares must have been clear
  // for the move to be legal. So the white king can't be there.
  for (int rsq : GetRay(srcr, srcc, dstr, dstc)) {
    king_squares.Remove(rsq);
  }

  SquareSet filled;
  filled.Add(Square(dstr, dstc));

  SquareSet check_kings = KingCheckCheck(king_squares, cleared, filled);

  // Now compute the updated board proposition.
  Board board = board_before;
  // Cleared squares are empty.
  for (int csq : cleared) {
    const auto &[r, c] = UnSquare(csq);
    for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
      if (t == EMPTY) {
        board.props[ChessProp::HasContentsIdx(r, c, t)] = True();
      } else {
        board.props[ChessProp::HasContentsIdx(r, c, t)] = False();
      }
    }
  }

  // Squares on the inside of the ray are known to be empty.
  for (int rsq : GetRay(srcr, srcc, dstr, dstc)) {
    const auto &[r, c] = UnSquare(rsq);
    // Starting and ending are handled separately, because we also
    // need those for knight moves.
    if (r == srcr && c == srcc) continue;
    if (r == dstr && c == dstc) continue;

    for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
      if (t == EMPTY) {
        board.props[ChessProp::HasContentsIdx(r, c, t)] = True();
      } else {
        board.props[ChessProp::HasContentsIdx(r, c, t)] = False();
      }
    }
  }

  // PERF: Depending on the move shape, we can also know the specific
  // piece. It's not that interesting for testing legality (generally
  // we just need to know it's a white piece).
  // And the destination has the source piece.
  for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
    board.props[ChessProp::HasContentsIdx(dstr, dstc, t)] =
      board_before.props[ChessProp::HasContentsIdx(srcr, srcc, t)];
  }

  // But as an optimization, we can actually assume it now contains a
  // white piece (and not the King). All legal moves do this. This
  // helps interrupt rays unconditionally (not empty, not an attacking
  // piece).
  for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
    if (t == EMPTY || ChessProp::IsBlackPiece(t) || t == WHITE_KING)
      board.props[ChessProp::HasContentsIdx(dstr, dstc, t)] = False();
  }

  // Now we have two cases. If the square in the set to check, then we
  // perform the full attacked test. Otherwise, we use the existing
  // value.


  // First, all of the full checks.
  Prop full_check = False();
  for (int ksq : check_kings) {
    const auto &[kr, kc] = UnSquare(ksq);
    full_check = full_check |
      (HasContents(board, kr, kc, WHITE_KING) &
       ChessProp::Attacked(board, kr, kc));
  }

  // Then all the squares where check will be unaffected.
  Prop unaffected_king = False();
  for (int ksq : SquareSet::Negation(check_kings)) {
    const auto &[kr, kc] = UnSquare(ksq);

    unaffected_king = unaffected_king |
      HasContents(board, kr, kc, WHITE_KING);
  }

  // Then for any unaffected king square, we are in check iff we
  // were already in check.
  Prop unaffected = unaffected_king & was_in_check;

  return full_check | unaffected;
}


static Prop EnPassantLegal(const Board &board,
                           int srcr, int srcc, int dstr, int dstc,
                           const Details &details) {
  int dc = dstc - srcc;

  // Only on this specific row.
  if (srcr != 3 || dstr != 2 || !(dc == -1 || dc == 1))
    return False();

  // If an en passant column is set, that also implies the destination
  // is empty (pawn had to do a double-move through that square) and
  // there's a pawn to be captured.
  Prop legal = EnPassantCol(board, dstc);
  // Need to explicitly note that the captured pawn's square is cleared.
  SquareSet also_cleared;
  also_cleared.Add(Square(srcr, srcc + dc));
  Prop check_ok = details.check_check ?
    -KingAttackedAfter(board, srcr, srcc, dstr, dstc, also_cleared) :
    True();

  return legal & check_ok;
}

// Is it legal to move the white pawn at srcr, srcc to
// dstr, dstc? This includes promotions (where we assume a
// promotion to queen) but NOT en passant captures.
// Assumes the source piece is a pawn, and not moving into check.
static Prop PawnLegal(const Board &board,
                      int srcr, int srcc, int dstr, int dstc) {
  // Can't move into bottom two rows.
  if (dstr >= 6) return False();

  // Pawns should be moving -1 or -2 only.
  const int dist = dstr - srcr;
  if (!(dist == -1 || dist == -2)) return False();

  const bool vertical = dstc == srcc;
  const bool diagonal = (dstc == srcc + 1) || (dstc == srcc - 1);

  // Double move is straight forward.
  if (dist == -2 && !vertical) return False();

  // Can only do double move from 6th row.
  if (dist == -2 && srcr != 6) return False();

  // Double move.
  Prop legal_double =
    dist == -2 ?
    IsEmpty(board, srcr - 1, srcc) & IsEmpty(board, srcr - 2, srcc) :
    False();

  // Normal capture.
  Prop legal_capture =
    (dist == -1 && diagonal) ? IsCapturable(board, dstr, dstc) : False();

  // Regular push.
  Prop legal_single =
    (dist == -1 && vertical) ?
    IsEmpty(board, srcr - 1, srcc) :
    False();

  // Nothing special to do for promotion: If it's legal
  // to move into the square, it's legal to promote to
  // queen, which is the assumption.

  return Or(legal_single, legal_double, legal_capture);
}

// Normal rook moves; castling is represented by moving the king.
// Assumes source piece is a rook or queen, and is not moving into
// check.
static Prop RookLegal(const Board &board,
                      int srcr, int srcc, int dstr, int dstc) {

  bool horiz = srcc != dstc;
  bool vert = srcr != dstr;

  // Must move only horizontally or vertically.
  if (horiz == vert) return False();

  const auto dir = GetDir(srcr, srcc, dstr, dstc);
  if (!dir.has_value()) return False();

  // All squares in between must be empty.
  Prop clear = True();
  const auto &[dr, dc] = dir.value();

  if (VERBOSE) {
    Print("Rook legal? {},{} to {},{} | {}{} | d {},{}\n",
          srcr, srcc, dstr, dstc,
          horiz ? "h" : "", vert ? "v" : "",
          dr, dc);
  }

  for (int r = srcr + dr, c = srcc + dc; !(r == dstr && c == dstc); ) {
    clear = clear & IsEmpty(board, r, c);
    r += dr;
    c += dc;
    CHECK(r >= 0 && r < 8 && c >= 0 && c < 8);
  }

  return clear & (IsCapturable(board, dstr, dstc) |
                  IsEmpty(board, dstr, dstc));
}

// Assumes source piece is a bishop or queen, and is not moving into
// check.
static Prop BishopLegal(const Board &board,
                        int srcr, int srcc, int dstr, int dstc) {
  const auto dir = GetDir(srcr, srcc, dstr, dstc);
  if (!dir.has_value()) return False();
  const auto &[dr, dc] = dir.value();

  // Must be a diagonal move.
  if (dr == 0 || dc == 0) return False();

  // All squares in between must be empty.
  Prop clear = True();
  for (int r = srcr + dr, c = srcc + dc; r != dstr && c != dstc; ) {
    clear = clear & IsEmpty(board, r, c);
    r += dr;
    c += dc;
    CHECK(r >= 0 && r < 8 && c >= 0 && c < 8);
  }

  return clear & (IsCapturable(board, dstr, dstc) |
                  IsEmpty(board, dstr, dstc));
}

// Assumes source piece is a knight, and is not moving into check.
static Prop KnightLegal(const Board &board,
                        int srcr, int srcc, int dstr, int dstc) {
  // This is simpler than the above because we don't need to
  // check anything in between.
  if (!IsKnightMove(srcr, srcc, dstr, dstc)) return False();

  return IsCapturable(board, dstr, dstc) | IsEmpty(board, dstr, dstc);
}

// Must do its own move-into-check logic.
static Prop KingLegal(const Board &board,
                      int srcr, int srcc, int dstr, int dstc,
                      const Details &details) {
  Prop was_in_check = board.props[ChessProp::CheckIdx()];

  int dr = dstr - srcr;
  int dc = dstc - srcc;

  int distr = std::abs(dr);
  int distc = std::abs(dc);

  // Must move to one of the 8-connected neighbors.
  bool normal = (distr <= 1 && distc <= 1 && (dr != 0 || dc != 0));

  bool castling = srcr == 7 && srcc == 4 && dstr == 7 &&
    (dstc == 2 || dstc == 6);

  Prop normal_move =
    normal ? (IsCapturable(board, dstr, dstc) |
              IsEmpty(board, dstr, dstc)) : False();

  Prop castling_move = False();
  if (castling) {
    bool kingside = dstc == 6;

    CHECK(srcr == 7);
    CHECK(srcc == 4);
    castling_move =
      // Appropriate castling privileges must still exist.
      // (Which implies a rook in the corner.)
      Castling(board, true, kingside) &
      // Can't castle out of check.
      (details.check_check ? -was_in_check : True());

    if (kingside) {
      castling_move = castling_move &
        IsEmpty(board, 7, 5) &
        IsEmpty(board, 7, 6);
    } else {
      castling_move = castling_move &
        IsEmpty(board, 7, 1) &
        IsEmpty(board, 7, 2) &
        IsEmpty(board, 7, 3);
    }

    // Also: Squares *that the king crosses* can't
    // be attacked. Note that the rook (and g1) CAN
    // be attacked in normal chess!
    if (details.castling_attacked) {
      if (kingside) {
        castling_move = castling_move &
          -ChessProp::Attacked(board, 7, 5) &
          -ChessProp::Attacked(board, 7, 6);
      } else {
        castling_move = castling_move &
        -ChessProp::Attacked(board, 7, 2) &
        -ChessProp::Attacked(board, 7, 3);
      }
    }

  }

  // For testing whether the king is attacked in its new square, we need
  // to clear the square the king vacated; the previous king can't block
  // an attack on the new square!
  Board new_board = board;
  SetPiece(&new_board, srcr, srcc, EMPTY);

  // Here, checking for moving into check is much more straightforward,
  // because we know where the king will be, and moving the rooks
  // cannot cause a discovered check (on white). (Note that this would
  // be possible in Chess960.)
  Prop not_into_check =
    details.check_check ? -ChessProp::Attacked(new_board, dstr, dstc) : True();

  return And(Or(normal_move, castling_move), not_into_check);
}


// Is it legal for white to move their piece from srcr,srcc
// to dstr,dstc?
Prop ChessProp::IsLegal(const Board &board,
                        int srcr, int srcc,
                        int dstr, int dstc,
                        Details details) {
  CHECK(srcr >= 0 && srcr < 8);
  CHECK(srcc >= 0 && srcc < 8);
  CHECK(dstr >= 0 && dstr < 8);
  CHECK(dstc >= 0 && dstc < 8);

  // Note: Many pairs of squares can never have a legal move!
  // The below should compute an expression equivalent to false.

  // Self-moves are never legal.
  if (srcr == dstr && srcc == dstc) return False();

  // Most piece moves test that the king is not in check the same
  // way: They remove the source piece and overwrite the destination.
  Prop simple_not_into_check =
    details.check_check ?
    -KingAttackedAfter(board, srcr, srcc, dstr, dstc) :
    True();

  Prop simple_piece_move =
    simple_not_into_check &
    Or(HasContents(board, srcr, srcc, WHITE_PAWN) &
       PawnLegal(board, srcr, srcc, dstr, dstc),

       // To reduce expression size, we treat the queen as both a
       // rook and bishop.
       (HasContents(board, srcr, srcc, WHITE_ROOK) |
        HasContents(board, srcr, srcc, WHITE_QUEEN)) &
       RookLegal(board, srcr, srcc, dstr, dstc),

       (HasContents(board, srcr, srcc, WHITE_BISHOP) |
        HasContents(board, srcr, srcc, WHITE_QUEEN)) &
       BishopLegal(board, srcr, srcc, dstr, dstc),

       HasContents(board, srcr, srcc, WHITE_KNIGHT) &
       KnightLegal(board, srcr, srcc, dstr, dstc));

  // But king moves test check directly.
  Prop king_move =
    HasContents(board, srcr, srcc, WHITE_KING) &
    KingLegal(board, srcr, srcc, dstr, dstc, details);

  // ... and en passant has a rare corner case to deal with.
  Prop en_passant_move =
    details.en_passant ?
    (HasContents(board, srcr, srcc, WHITE_PAWN) &
     EnPassantLegal(board, srcr, srcc, dstr, dstc, details)) :
    False();

  return Or(simple_piece_move,
            king_move,
            en_passant_move);
}

Board ChessProp::BoardFromPosition(const Position &pos) {
  Board board{
    .props = std::vector<Prop>(NUM_BOARD_PROPS, False()),
  };

  CHECK(!pos.BlackMove()) << "Can only represent positions "
    "where it's white's move.";

  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      uint8_t cp = pos.SimplePieceAt(r, c);
      uint8_t ct = cp & Position::TYPE_MASK;
      bool cblack = (cp & Position::COLOR_MASK) == Position::BLACK;

      int t = EMPTY;
      if (cp != Position::EMPTY) {
        t = cblack ? BLACK_START : WHITE_START;
        switch (ct) {
        case Position::PAWN: t += PAWN; break;
        case Position::KNIGHT: t += KNIGHT; break;
        case Position::BISHOP: t += BISHOP; break;
        case Position::ROOK: [[fallthrough]];
        case Position::C_ROOK: t += ROOK; break;
        case Position::QUEEN: t += QUEEN; break;
        case Position::KING: t += KING; break;
        default:
          LOG(FATAL) << std::format("Invalid piece? {:02x}", cp);
        }
      }

      // The rest remain false.
      board.props[HasContentsIdx(r, c, t)] = True();
    }
  }

  for (bool white : {false, true}) {
    for (bool kingside : {false, true}) {
      if (pos.CanStillCastle(white, kingside)) {
        board.props[CastlingIdx(white, kingside)] = True();
      }
    }
  }

  if (std::optional<uint8_t> oep = pos.EnPassantColumn()) {
    board.props[EnPassantColIdx(oep.value())] = True();
  }

  board.props[CheckIdx()] = Position(pos).IsInCheck() ? True() : False();

  return board;
}

std::vector<Position> ChessProp::LegalPositions(ArcFour *rc, int num) {
  // Seed positions; always white to move.
  static std::initializer_list<std::string_view> SEED_FEN = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rn3r2/pbppq1p1/1p2pN2/8/3P2NP/6P1/PPP1BP1R/R3K1k1 w Q - 5 18",
    "1B6/8/7P/4p3/3b3k/8/8/2K5 w - - 0 1",
    "2rq2kb/pb1r3p/2n1R1pB/1pp2pN1/3p1Q2/P1PP2P1/1P3PBP/4R1K1 w - - 0 1",
    "1nbqkbnr/prpppppp/8/2p5/6P1/2BQ1BN1/P1PPPP1P/R3K2R w KQk - 0 1",
    "r1bqkbr1/pp1nnpp1/B6p/1Pppp3/4P3/B4N2/P1PPQPPP/RN2K2R w KQq c6 0 8",
  };

  std::unordered_set<Position, PositionHash, PositionEq> seen;
  seen.reserve(num);
  // Unique entries.
  std::vector<Position> pool;
  pool.reserve(num);

  for (std::string_view fen : SEED_FEN) {
    Position pos;
    CHECK(Position::ParseFEN(fen, &pos)) << fen;
    CHECK(!pos.BlackMove()) << fen;
    CHECK(!seen.contains(pos));
    seen.insert(pos);
    pool.push_back(pos);
  }

  while (pool.size() < (int)num) {
    size_t idx = RandTo(rc, pool.size());
    Position pos = pool[idx];

    auto moves = pos.GetLegalMoves();
    if (moves.empty()) {
      continue;
    }

    size_t move_idx = RandTo(rc, moves.size());
    pos.ApplyMove(moves[move_idx]);

    // Switch sides so that it's always white to move.
    pos = Position::FlipSides(pos);

    if (seen.insert(pos).second) {
      pool.push_back(pos);
    }
  }

  // If num is smaller than the initial seed size, trim it.
  if (pool.size() > (int)num) {
    pool.resize(num);
  }

  Shuffle(rc, &pool);
  return pool;
}
