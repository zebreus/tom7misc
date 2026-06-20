
#include "prop.h"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include "base/logging.h"

enum Type : uint8_t {
  BLACK_PAWN = 0,
  BLACK_KNIGHT = 1,
  BLACK_BISHOP = 2,
  BLACK_ROOK = 3,
  BLACK_QUEEN = 4,
  BLACK_KING = 5,
  WHITE_PAWN = 6,
  WHITE_KNIGHT = 7,
  WHITE_BISHOP = 8,
  WHITE_ROOK = 9,
  WHITE_QUEEN = 10,
  WHITE_KING = 11,
  EMPTY = 12,
};

static constexpr int NUM_TYPES = 13;

static constexpr uint8_t PAWN = 0;
static constexpr uint8_t KNIGHT = 1;
static constexpr uint8_t BISHOP = 2;
static constexpr uint8_t ROOK = 3;
static constexpr uint8_t QUEEN = 4;
static constexpr uint8_t KING = 5;

// such that BLACK_START + PIECE = BLACK_PIECE.
static constexpr uint8_t BLACK_START = 0;
static constexpr uint8_t WHITE_START = 0;

static std::string_view ShortType(uint8_t t) {
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

// Following chess.h:
// Row 0 is the top row of the board, black's back
// rank, aka. rank 8. We try to use "row" to mean
// this zero-based top-to-bottom notion. "rank"
// would be the 1-based bottom-to-top version from
// standard chess notation, which we avoid.

// The bit that indicates whether the square at r,c contains
// the specific type.
int HasContentsIdx(int r, int c, uint8_t type) {
  // To simplify matters, we have a regular bit structure
  // even if it's impossible (e.g. pawns in back row).
  int idx = r * 8 + c;
  return idx * NUM_TYPES + type;
}

int EnPassantColIdx(int c) {
  return 8 * 8 * NUM_TYPES + c;
}

int CastlingIdx(bool white, bool kingside) {
  int off = (white ? 0b10 : 0b00) | (kingside ? 0b01 : 0b01);
  return 8 * 8 * NUM_TYPES + 8 + off;
}

static constexpr int WORLD_SYMS =
  // board contents
  8 * 8 * NUM_TYPES +
  // en passant columns
  8 +
  // castling flags
  4;

// A chessboard. Following chess.h, we always represent
// the board as though it's white's turn to move.
World BoardWorld() {
  World world;
  world.symbol_names.resize(WORLD_SYMS);
  auto SetSym = [&world](int idx, std::string_view s) {
      CHECK(world.symbol_names[idx].empty());
      world.symbol_names[idx] = std::move(s);
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

  return world;
}

inline Prop HasContents(int r, int c, int t) {
  CHECK(r >= 0 && r < 8 && c >= 0 && c < 8);
  return {Var{.id = HasContentsIdx(r, c, t)}};
}

inline Prop EnPassantCol(int c) {
  CHECK(c >= 0 && c < 8);
  return {Var{.id = EnPassantColIdx(c)}};
}

inline Prop Castling(bool w, bool k) {
  return {Var{.id = CastlingIdx(w, k)}};
}

inline Prop IsEmpty(int r, int c) {
  CHECK(r >= 0 && r < 8 && c >= 0 && c < 8);
  return HasContents(r, c, EMPTY);
}

// Is there a black piece at r,c that can be captured? We don't
// include the king because no legal move would pose such a
// question, and it simplifes the expression.
inline Prop IsCapturable(int r, int c) {
  return Or(HasContents(r, c, BLACK_PAWN),
            HasContents(r, c, BLACK_KNIGHT),
            HasContents(r, c, BLACK_BISHOP),
            HasContents(r, c, BLACK_ROOK),
            HasContents(r, c, BLACK_QUEEN),
            HasContents(r, c, BLACK_KING));
}

// For the Attacked checks, it is convenient to check outside
// the board as well; we know that this is false at "compilation"
// time.
inline Prop OnBoardAndHasContents(int r, int c, int t) {
  if (r < 0 || c < 0 || r >= 8 || c >= 8) return False();
  return HasContents(r, c, t);
}

// True if the square is attacked by a black piece.
// (Can't castle out of check, etc.)
Prop Attacked(int r, int c) {
  Prop knight = False();
  Prop king = False();
  for (int dr = -2; dr <= +2; dr++) {
    for (int dc = -2; dc <= +2; dc++) {

      int distr = std::abs(dr);
      int distc = std::abs(dc);

      // Must be an L-shaped move.
      if ((distr == 2 && distc == 1) ||
          (distr == 1 && distc == 2)) {
        knight = knight | OnBoardAndHasContents(r + dr, c + dc, BLACK_KNIGHT);
      }

      if (distr == 1 || distc == 1) {
        king = king | OnBoardAndHasContents(r + dr, c + dc, BLACK_KING);
      }
    }
  }

  // Two specific squares attack a square with a pawn.
  Prop pawn =
    OnBoardAndHasContents(r - 1, c - 1, BLACK_PAWN) |
    OnBoardAndHasContents(r - 1, c + 1, BLACK_PAWN);

  // TODO: Rook, Bishop, Queen, Pawn
  // Probably queen is included with the rook/bishop tests
  Prop rook = False();
  Prop bishop = False();

  // TODO
  return Or(pawn, knight, bishop, rook, king);
}

// Is it legal to move the white pawn at srcr, srcc to
// dstr, dstc? This includes promotions (where we assume a
// promotion to queen).
Prop PawnLegal(int srcr, int srcc, int dstr, int dstc) {
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
    IsEmpty(srcr - 1, srcc) & IsEmpty(srcr - 2, srcc) :
    False();

  // Normal capture.
  Prop legal_capture =
    (dist == -1 && diagonal) ? IsCapturable(dstr, dstc) : False();

  // En passant capture.
  Prop legal_en_passant =
    (dist == -1 && diagonal && srcr == 3) ?
    // En passant column also implies the destination is empty,
    // and there's a pawn to be captured.
    EnPassantCol(dstc) :
    False();

  // Regular push.
  Prop legal_single =
    (dist == -1 && vertical) ?
    IsEmpty(srcr - 1, srcc) :
    False();

  // Nothing special to do for promotion: If it's legal
  // to move into the square, it's legal to promote to
  // queen, which is the assumption.

  // TODO: Not moving into check.
  return Or(legal_single, legal_double, legal_capture, legal_en_passant);
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
Prop RookLegal(int srcr, int srcc, int dstr, int dstc) {
  bool horiz = srcc != dstc;
  bool vert = srcr != dstr;

  // Must move only horizontally or vertically.
  if (horiz == vert) return False();

  const auto dir = GetDir(srcr, srcc, dstr, dstc);
  if (!dir.has_value()) return False();

  // All squares in between must be empty.
  Prop clear = True();
  const auto &[dr, dc] = dir.value();
  for (int r = srcr + dr, c = srcc + dc; r != dstr && c != dstc; ) {
    clear = clear & IsEmpty(r, c);
    r += dr;
    c += dc;
    CHECK(r >= 0 && r < 8 && c >= 0 && c < 8);
  }

  // TODO: Not moving into check.
  return clear & (IsCapturable(dstr, dstc) | IsEmpty(dstr, dstc));
}

Prop BishopLegal(int srcr, int srcc, int dstr, int dstc) {
  const auto dir = GetDir(srcr, srcc, dstr, dstc);
  if (!dir.has_value()) return False();
  const auto &[dr, dc] = dir.value();

  // Must be a diagonal move.
  if (dr == 0 || dc == 0) return False();

  // All squares in between must be empty.
  Prop clear = True();
  for (int r = srcr + dr, c = srcc + dc; r != dstr && c != dstc; ) {
    clear = clear & IsEmpty(r, c);
    r += dr;
    c += dc;
    CHECK(r >= 0 && r < 8 && c >= 0 && c < 8);
  }

  // TODO: Not moving into check.
  return clear & (IsCapturable(dstr, dstc) | IsEmpty(dstr, dstc));
}

Prop KnightLegal(int srcr, int srcc, int dstr, int dstc) {

  // This is simpler than the above because we don't need to
  // check anything in between.
  int distr = std::abs(dstr - srcr);
  int distc = std::abs(dstc - srcc);

  // Must be an L-shaped move.
  if (!((distr == 2 && distc == 1) ||
        (distr == 1 && distc == 2))) return False();

  // TODO: No moving into check.
  return IsCapturable(dstr, dstc) | IsEmpty(dstr, dstc);
}

Prop KingLegal(int srcr, int srcc, int dstr, int dstc) {

  int dr = dstr - srcr;
  int dc = dstc - srcc;

  int distr = std::abs(dr);
  int distc = std::abs(dc);

  // Must move to one of the 8-connected neighbors.
  bool normal = (distr > 1 || distc > 1 || (dr == 0 && dc == 0));

  bool castling = srcr == 7 && srcc == 4 && dstr == 7 &&
    (dstc == 0 || dstc == 7);

  Prop normal_move =
    normal ? (IsCapturable(dstr, dstc) | IsEmpty(dstr, dstc)) : False();

  // TODO: Castling.
  Prop castling_move = False();

  // Here, checking for moving into check is much more straightforward,
  // because we know where the king will be, and moving the rooks
  // cannot cause a discovered check (on white). (Note that this would
  // be possible in Chess960.)
  return And(Or(normal_move, castling_move),
             -(Attacked(dstr, dstc)));
}


// Is it legal for white to move their piece from srcr,srcc
// to dstr,dstc?
Prop IsLegal(int srcr, int srcc,
             int dstr, int dstc) {
  CHECK(srcr >= 0 && srcr < 8);
  CHECK(srcc >= 0 && srcc < 8);
  CHECK(dstr >= 0 && dstr < 8);
  CHECK(dstc >= 0 && dstc < 8);

  // PERF: Many pairs of squares can never have a legal move!
  // We should compute an expression equivalent to False for
  // them below.

  // PERF: These generally all need to check that the destination
  // is empty and/or capturable. So we should compute those
  // expressions up front and factor them out.

  // Self-moves are never legal.
  if (srcr == dstr && srcc == dstc) return False();

  // Now this is structured as a case analysis. We know that
  // the contents props are mutually disjoint, so we can
  // just OR all of the cases together.

  return Or(
      HasContents(srcr, srcc, WHITE_PAWN) &
      PawnLegal(srcr, srcc, dstr, dstc),

      // To reduce expression size, we treat the queen as both a
      // rook and bishop.
      (HasContents(srcr, srcc, WHITE_ROOK) |
       HasContents(srcr, srcc, WHITE_QUEEN)) &
      RookLegal(srcr, srcc, dstr, dstc),

      (HasContents(srcr, srcc, WHITE_BISHOP) |
       HasContents(srcr, srcc, WHITE_QUEEN)) &
      BishopLegal(srcr, srcc, dstr, dstc),

      HasContents(srcr, srcc, WHITE_KNIGHT) &
      KnightLegal(srcr, srcc, dstr, dstc),

      // TODO: King move

      False());
}


