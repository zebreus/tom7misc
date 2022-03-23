
#include "nes-tetris.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <cmath>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "tetris.h"

using namespace std;

// EF = empty
// 7B = white square w/shine, like in T, square, line
// 7D = blue, like in J
// 7C = cyan, like Z

static std::vector<uint8_t> StringBoard(const std::string &s) {
  CHECK(s.size() == 20 * 10);
  std::vector<uint8_t> ret(20 * 10, 0);
  for (int i = 0; i < 20 * 10; i++) {
	switch (s[i]) {
	case ' ': ret[i] = 0xEF; break;
	case '*': ret[i] = 0x7B; break;
	case '#': ret[i] = 0x7C; break;
	case '@': ret[i] = 0x7D; break;
	case '%': ret[i] = 0x77; break;
	default:
	  LOG(FATAL) << "Unknown board char " << s[i];
	}
  }
  return ret;
}

static std::string BoardString(const std::vector<uint8_t> &b) {
  CHECK(b.size() == 20 * 10);
  std::string ret(20 * 10, '?');
  for (int i = 0; i < 20 * 10; i++) {
	switch (b[i]) {
	case 0xEF: ret[i] = ' '; break;
	case 0x7B: ret[i] = '*'; break;
	case 0x7C: ret[i] = '#'; break;
	case 0x7D: ret[i] = '@'; break;
	case 0x77: ret[i] = '%'; break;
	default:
	  LOG(FATAL) << "Unknown board byte " << b[i];
	}
  }
  return ret;
}


static void TestDraw() {
  string s =
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"          "
	"   #  ****"
	"   # @@   "
	"#### @@   ";
  vector<uint8_t> board = StringBoard(s);
  CHECK(s == BoardString(board));

  DrawShapeOnBoard(0x77, I_VERT, 0, 3, &board);
  DrawShapeOnBoard(0x77, SQUARE, 8, 19, &board);
  DrawShapeOnBoard(0x7D, S_HORIZ, 3, 0, &board);
  
  // printf("%s\n", BoardString(board).c_str());
  CHECK(BoardString(board) ==
		"%  @@     "
		"%         "
		"%         "
		"%         "
		"          "
		"          "
		"          "
		"          "
		"          "
		"          "
		"          "
		"          "
		"          "
		"          "
		"          "
		"          "
		"          "
		"   #  ****"
		"   # @@ %%"
		"#### @@ %%");

}


static void TestRNG() {
  {
    RNGState s1{.rng1 = 0x67, .rng2 = 0x05,
                .last_drop = 0x07, .drop_count = 0x03};
    RNGState s2 = NextPiece(s1);
    // I think this is correct?
    CHECK(s1.rng1 == s2.rng1 &&
          s1.rng2 == s2.rng2 &&
          s1.drop_count + 1 == s2.drop_count &&
          s2.last_drop == 0x0a);
  }

  {
    RNGState s1{.rng1 = 0xb3, .rng2 = 0x82,
                .last_drop = 0x07, .drop_count = 0x03};
    // my code gives 59c1.0e.04
    // game gives 59c1.07.04
    RNGState s2 = NextPiece(s1);
    CHECK(s2.rng1 == 0x59 && s2.rng2 == 0xc1);
    CHECK(s2.last_drop == 0x07);
    CHECK(s2.drop_count == s1.drop_count + 1);
  }
  
}

static void TestFastRNG() {
  for (int a = 0; a < 256; a++) {
    for (int b = 0; b < 256; b++) {
      RNGState input;
      input.rng1 = a;
      input.rng2 = b;

      RNGState expected = NextRNG(input);
      RNGState got = FastNextRNG(input);
      CHECK(EqualRNG(expected, got)) << RNGString(input) << " wanted "
                                     << RNGString(expected) << " got "
                                     << RNGString(got);
    }
  }
}

static void TestFastNextPiece() {
  for (int a = 0; a < 256; a++) {
    for (int b = 0; b < 256; b++) {
      for (uint8 p : {0x02, 0x07, 0x08, 0x0A, 0x0B, 0x0E, 0x12}) {
        for (int c = 0; c < 256; c++) {
          RNGState input;
          input.rng1 = a;
          input.rng2 = b;
          input.last_drop = p;
          input.drop_count = c;

          RNGState expected = NextPiece(input);
          RNGState got = FastNextPiece(input);
          CHECK(EqualRNG(expected, got)) << RNGString(input) << " wanted "
                                         << RNGString(expected) << " got "
                                         << RNGString(got);
        }
      }
    }
  }
}


int main(int argc, char **argv) {
  TestDraw();
  TestRNG();
  TestFastRNG();
  TestFastNextPiece();
  printf("OK\n");
  return 0;
}
