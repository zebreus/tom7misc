
#ifndef _PINGU_TETRU_VIZ_H
#define _PINGU_TETRU_VIZ_H

#include <string>
#include <vector>
#include <cstdint>

#include "base/logging.h"

struct BoardPic {
  // TODO: Encode current piece pos too, or just blit it in there?
  // Board in native NES format.
  // Output string is lowercase letters only.
  static std::string ToString(const std::vector<uint8_t> &board) {
	CHECK(board.size() == 20 * 10);
	std::string out;
	out.reserve((20 * 10) >> 1);
	for (int i = 0; i < (20 * 10) >> 1; i++) {
	  int a = ToInt(board[i * 2 + 0]);
	  int b = ToInt(board[i * 2 + 1]);
	  out.push_back('a' + (a * 5 + b));
	}
	return out;
  }

  // Uses uint8, but only 0 (empty), 1,2,3 (colored blocks), 4 (other)
  // are used.
  inline static std::vector<uint8_t> ToPixels(const std::string &s) {
	std::vector<uint8_t> out(20 * 10, 0);
	if (s.size() != ((20 * 10) >> 1))
	  return out;
	for (int i = 0; i < (20 * 10) >> 1; i++) {
	  int ab = s[i] - 'a';
	  if (ab < 0) return out;
	  if (ab >= 25) return out;
	  int a = ab / 5;
	  int b = ab % 5;
	  out[i * 2 + 0] = a;
	  out[i * 2 + 1] = b;
	}
	return out;
  }

  inline static int ToInt(uint8_t ch) {
	switch (ch) {
	case 0xEF: return 0;
	case 0x7B: return 1;
	case 0x7D: return 2;
	case 0x7C: return 3;
	default: return 4;
	}
  }
};


#endif
