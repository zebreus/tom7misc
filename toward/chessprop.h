
#ifndef _TOWARD_CHESSPROP_H
#define _TOWARD_CHESSPROP_H

#include "prop.h"

#include <format>
#include <string>
#include <string_view>
#include <vector>

// These are just used for position conversions and examples.
#include "chess.h"
#include "arcfour.h"

// Rule details. All of these should be turned on for full chess,
// but turning off rules about check make the propositions massively
// simpler, for example.
struct ChessProp_Details {
  bool castling = true;
  bool castling_attacked = true;
  bool check_check = true;
  bool en_passant = true;
};

struct ChessProp {
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
  static std::string_view ShortType(uint8_t t);

  static bool IsBlackPiece(uint8_t t) {
    return t >= BLACK_PAWN && t <= BLACK_KING;
  }

  static bool IsWhitePiece(uint8_t t) {
    return t >= WHITE_PAWN && t <= WHITE_KING;
  }

  static constexpr int NUM_BOARD_PROPS =
    // board contents
    8 * 8 * NUM_TYPES +
    // en passant columns
    8 +
    // castling flags
    4 +
    // is white in check?
    1;

  // The configuration of the board.
  //
  // Following chess.h, we always represent the board as though it's
  // white's turn to move.
  struct Board {
    // NUM_BOARD_PROPS.
    std::vector<Prop> props;
  };

  // Following chess.h:
  // Row 0 is the top row of the board, black's back
  // rank, aka. rank 8. We try to use "row" to mean
  // this zero-based top-to-bottom notion. "rank"
  // would be the 1-based bottom-to-top version from
  // standard chess notation, which we avoid.

  // The bit that indicates whether the square at r,c contains
  // the specific type.
  static int HasContentsIdx(int r, int c, uint8_t type) {
    // To simplify matters, we have a regular bit structure
    // even if it's impossible (e.g. pawns in back row).
    int idx = r * 8 + c;
    return idx * NUM_TYPES + type;
  }

  static int EnPassantColIdx(int c) {
    return 8 * 8 * NUM_TYPES + c;
  }

  static int CastlingIdx(bool white, bool kingside) {
    int off = (white ? 0b10 : 0b00) | (kingside ? 0b01 : 0b00);
    return 8 * 8 * NUM_TYPES + 8 + off;
  }

  static int CheckIdx() {
    return 8 * 8 * NUM_TYPES + 8 + 4;
  }

  // Creates a new unconstrained chessboard with a variable for every
  // bit, inserting those at the end of the world.
  static Board NewBoard(World *world);

  using Details = ChessProp_Details;
  static Prop IsLegal(const Board &board,
                      int srcr, int srcc,
                      int dstr, int dstc,
                      Details details = Details());

  static constexpr Details REAL_CHESS = {
    .castling = true,
    .castling_attacked = true,
    .check_check = true,
    .en_passant = true,
  };

  static constexpr Details KID_CHESS = {
    .castling = true,
    .castling_attacked = false,
    .check_check = false,
    .en_passant = true,
  };

  // Every prop will be a constant true or false, so this is mostly
  // just useful for testing. Must be white's move.
  static Board BoardFromPosition(const Position &pos);

  // Mainly exposed for testing.
  static Prop Attacked(const Board &board, int r, int c);

  static std::string Square(int row, int col) {
    return std::format("{:c}{:c}", 'a' + col, '1' + (7 - row));
  }

  // Get the requested number of valid positions; white to move.
  static std::vector<Position> LegalPositions(ArcFour *rc, int num);
};

#endif
