
#ifndef _PINGU_ENCODING_H
#define _PINGU_ENCODING_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <bit>
#include <initializer_list>

#include "tetris.h"

struct Move {
  Shape shape = I_VERT;
  uint8_t col = 0;
};

struct Encoding {
  static constexpr uint16_t STDPOS1 = 0b1000000000;
  static constexpr uint16_t STDPOS2 = 0b1100000000;
  static constexpr uint16_t STDPOS3 = 0b0100000000;

  static std::map<uint8_t, std::vector<Move>>
  ParseSolutions(const std::string &filename);

  static std::string GraphicalMoveString(Move m);

  static std::string MovieString(const std::vector<Move> &moves);

  // The full row expected to encode the byte.
  static uint16_t FullTarget(uint8_t target) {
    const uint16_t full_target = (uint16_t)target |
      (target == 0xFF ?
       /* special case for FF so we don't complete line */
       0b00 :
       ((std::popcount(target) & 1) ? 0b01 : 0b11)) << 8;
    CHECK((full_target & ~0b1111111111) == 0) << full_target;
    CHECK((std::popcount(full_target) & 1) == 0) << full_target;
    return full_target;
  }

  // The standard position is an S, but we don't need to get there by
  // placing an S, which means that it could cause a mismatch (some
  // impossible repeated piece) with the beginning of the next byte.
  // We could require that they all end in S, but this is quite
  // hard for some bytes (e.g. in 0xFF it is completely unsupported),
  // and the clearing phase is already the harder part. So as a
  // balance, we allow any of these pieces to end a phase, and don't
  // allow any of them to start.
  static constexpr std::initializer_list<Piece> OK_TO_END = {
    PIECE_S, PIECE_I,
  };
  static constexpr bool IsOkToEnd(Piece p) {
    for (Piece pp : OK_TO_END) if (p == pp) return true;
    return false;
  }
};

#endif
