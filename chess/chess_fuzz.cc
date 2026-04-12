
#include "chess.h"
#include "pgn.h"

#include <string>
#include <cstdio>
#include <vector>
#include <cstring>

#include <unistd.h>

// must compile with afl-clang-fast++
__AFL_FUZZ_INIT();

void FuzzFEN(const std::string &bytes) {
  Position pos;
  if (Position::ParseFEN(bytes.c_str(), &pos)) {
    // If we have a valid position and \0 mid-string,
    // then also parse the tail as a move.
    size_t off = strlen(bytes.c_str());
    if (off < bytes.size()) {
      Position::Move move;
      pos.ParseMove(bytes.c_str() + off + 1, &move);
    }
  }
}

void FuzzPGN(const std::string &bytes) {
  PGN pgn;
  PGN::Parse(bytes.c_str(), &pgn);
}

void FuzzMoves(const std::string &bytes) {
  std::vector<PGN::Move> moves;
  PGN::ParseMoves(bytes.c_str(), &moves);
}

int main(int argc, char **argv) {

  #ifdef __AFL_HAVE_MANUAL_CONTROL
  __AFL_INIT();
  #endif

  unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

  while (__AFL_LOOP(10000)) {
    int len = __AFL_FUZZ_TESTCASE_LEN;
    if (len < 2) continue;

    // Determines which function we run.
    const uint8_t code = buf[0];

    // Remainder copied to string.
    std::string payload((const char *)buf + 1, len - 1);

    switch (code) {
    case 0x00: FuzzFEN(payload); break;
    case 0x01: FuzzPGN(payload); break;
    case 0x02: FuzzMoves(payload); break;
    default:
      break;
    }
  }

  return 0;
};
