
#include "chessprop.h"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "chess.h"
#include "prop.h"

using Board = ChessProp::Board;
using enum ChessProp::Type;

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
      CHECK(world->symbol_names[start + idx].empty());
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

  return board;
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
            HasContents(board, r, c, BLACK_QUEEN),
            HasContents(board, r, c, BLACK_KING));
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

// True if the king is attacked in the resulting state.
// We should take a modified board. This will only be
// used for non-king and non-ep moves, so we can assume that the
// src is vacated by the move and the dst is populated
// by a white piece/pawn (not king).
//
// Castling is a king move and moving into check is already
// handled by king movement code.
//
// en passant captures are an annoying corner case. Here the
// modified board doesn't just move the src to the dst; we
// also need to vacate the captured pawn. (Consider "K1Pp1r"
// where white captures its adjacent pawn en passant,
// revealing a discovered attack from the rook on the king.)
// Fortunately there are only 14 distinct en passant
// captures, so we don't get a huge combinatorial blow-up.
// Some clever optimization may be possible here; I think
// that a discovered attack through the captured pawn can
// only come from a rook or queen on the same file. It can't
// be along the vertical (the white pawn is now in the way)
// or the diagonal (we know black's last move was a double
// pawn move, but then white would have already been in
// check along that diagonal; this situation is impossible!).
// So rather than a fully different board, we could just
// OR the standard one with a check for this one kind of
// configuration along just that row.
Prop KingAttackedAfter(const Board &board_before,
                       int srcr, int srcc, int dstr, int dstc) {

  Board board = board_before;
  // The source is empty.
  for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
    if (t == EMPTY) {
      board.props[ChessProp::HasContentsIdx(srcr, srcc, t)] = True();
    } else {
      board.props[ChessProp::HasContentsIdx(srcr, srcc, t)] = False();
    }
  }

  // And the destination has the source piece.
  for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
    board.props[ChessProp::HasContentsIdx(dstr, dstc, t)] =
      board_before.props[ChessProp::HasContentsIdx(srcr, srcc, t)];
  }
  // But as an optimization, we can actually assume it now contains a
  // white piece (and not the King). All legal moves do this. This
  // helps interrupt traces unconditionally (not empty, not an attacking
  // piece).
  for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
    if (t == EMPTY || ChessProp::IsBlackPiece(t) || t == WHITE_KING)
      board.props[ChessProp::HasContentsIdx(dstr, dstc, t)] = False();
  }

  Prop any_attacked = False();

  // Loop over all possible king positions.
  for (int kr = 0; kr < 8; kr++) {
    for (int kc = 0; kc < 8; kc++) {
      // Since we know this is not a king move (and we can assume it
      // is otherwise valid) we do not need to include the
      // src and dst.
      if ((kr == srcr && kc == srcc) ||
          (kr == dstr && kc == dstc)) {
        continue;
      }

      any_attacked = any_attacked |
        (HasContents(board, kr, kc, WHITE_KING) &
         ChessProp::Attacked(board, kr, kc));
    }
  }

  return any_attacked;
}

Prop EnPassantLegal(const Board &board,
                    int srcr, int srcc, int dstr, int dstc) {
  int dc = dstc - srcc;

  // Only on this specific row.
  if (srcr != 3 || dstr != 2 || !(dc == -1 || dc == 1))
    return False();

  // En passant column also implies the destination is empty,
  // and there's a pawn to be captured.
  Prop legal = EnPassantCol(board, dstc);

  // TODO: Not moving into check.
  // This requires some special logic to remove the captured piece!
  return legal;
}

// Is it legal to move the white pawn at srcr, srcc to
// dstr, dstc? This includes promotions (where we assume a
// promotion to queen) but NOT en passant captures.
// Assumes the source piece is a pawn, and not moving into check.
Prop PawnLegal(const Board &board, int srcr, int srcc, int dstr, int dstc) {
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

// Normal rook moves; castling is represented by moving the king.
// Assumes source piece is a rook or queen, and is not moving into
// check.
Prop RookLegal(const Board &board,
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

  // TODO: Not moving into check.
  return clear & (IsCapturable(board, dstr, dstc) |
                  IsEmpty(board, dstr, dstc));
}

// Assumes source piece is a bishop or queen, and is not moving into
// check.
Prop BishopLegal(const Board &board,
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
Prop KnightLegal(const Board &board,
                 int srcr, int srcc, int dstr, int dstc) {
  // This is simpler than the above because we don't need to
  // check anything in between.
  int distr = std::abs(dstr - srcr);
  int distc = std::abs(dstc - srcc);

  // Must be an L-shaped move.
  if (!((distr == 2 && distc == 1) ||
        (distr == 1 && distc == 2))) return False();

  return IsCapturable(board, dstr, dstc) | IsEmpty(board, dstr, dstc);
}

// Must do its own move-into-check logic.
Prop KingLegal(const Board &board,
               int srcr, int srcc, int dstr, int dstc) {

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
      -ChessProp::Attacked(board, 7, 4);

    // Also: Squares in between can't be attacked.
    if (kingside) {
      castling_move = castling_move &
        IsEmpty(board, 7, 5) &
        IsEmpty(board, 7, 6) &
        -ChessProp::Attacked(board, 7, 5) &
        -ChessProp::Attacked(board, 7, 6);
    } else {
      castling_move = castling_move &
        IsEmpty(board, 7, 1) &
        IsEmpty(board, 7, 2) &
        IsEmpty(board, 7, 3) &
        -ChessProp::Attacked(board, 7, 1) &
        -ChessProp::Attacked(board, 7, 2) &
        -ChessProp::Attacked(board, 7, 3);
    }
  }

  // Here, checking for moving into check is much more straightforward,
  // because we know where the king will be, and moving the rooks
  // cannot cause a discovered check (on white). (Note that this would
  // be possible in Chess960.)
  return And(Or(normal_move, castling_move),
             -(ChessProp::Attacked(board, dstr, dstc)));
}


// Is it legal for white to move their piece from srcr,srcc
// to dstr,dstc?
Prop ChessProp::IsLegal(const Board &board,
                        int srcr, int srcc,
                        int dstr, int dstc) {
  CHECK(srcr >= 0 && srcr < 8);
  CHECK(srcc >= 0 && srcc < 8);
  CHECK(dstr >= 0 && dstr < 8);
  CHECK(dstc >= 0 && dstc < 8);

  // PERF: Many pairs of squares can never have a legal move!
  // The code below should already compute an expression equivalent
  // to false for them, but we should make sure it optimizes
  // away.

  // PERF: These generally all need to check that the destination
  // is empty and/or capturable. So we should compute those
  // expressions up front and factor them out, or make sure that
  // optimization can do this.

  // Self-moves are never legal.
  if (srcr == dstr && srcc == dstc) return False();

  // Most piece moves test that the king is not in check the same
  // way: They remove the source piece and overwrite the destination.
  Prop simple_not_into_check =
    -KingAttackedAfter(board, srcr, srcc, dstr, dstc);

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
    KingLegal(board, srcr, srcc, dstr, dstc);

  // ... and en passant has a rare corner case to deal with.
  Prop en_passant_move =
    HasContents(board, srcr, srcc, WHITE_PAWN) &
    EnPassantLegal(board, srcr, srcc, dstr, dstc);

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

  return board;
}
