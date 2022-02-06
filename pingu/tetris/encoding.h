
#ifndef _PINGU_ENCODING_H
#define _PINGU_ENCODING_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <bit>

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
       ((std::popcount(target) & 1) ? 0b11 : 0b01)) << 8;
    CHECK((full_target & ~0b1111111111) == 0) << full_target;
    return full_target;
  }

};

#endif
