
#include "encoding.h"

#include <utility>

#include "base/stringprintf.h"

#include "tetris.h"
#include "util.h"

using namespace std;
using uint8 = uint8_t;
using uint16 = uint16_t;

std::map<uint8, std::vector<Move>>
Encoding::ParseSolutions(const string &filename) {
  std::map<uint8, std::vector<Move>> ret;
  std::vector<string> lines = Util::ReadFileToLines(filename);
  for (string line : lines) {
    line = Util::NormalizeWhitespace(line);
    if (line.empty()) continue;

    string target = Util::chopto(' ', line);
    CHECK(target.size() == 2);
    CHECK(Util::IsHexDigit(target[0]) &&
          Util::IsHexDigit(target[1])) << target;
    uint8 t = Util::HexDigitValue(target[0]) * 16 +
      Util::HexDigitValue(target[1]);

    string encoded = Util::chopto(' ', line);
    CHECK(!encoded.empty());
    CHECK(encoded.size() % 3 == 0);

    const int movie_size = (int)encoded.size() / 3;

    vector<Move> movie;
    movie.reserve(movie_size);
    for (int i = 0; i < movie_size; i++) {
      char d1 = encoded[i * 3 + 0];
      char d2 = encoded[i * 3 + 1];
      char c = encoded[i * 3 + 2];
      CHECK(d1 >= '0' && d1 <= '9');
      CHECK(d2 >= '0' && d2 <= '9');
      CHECK(c >= 'a' && c <= 'j');
      Move m;
      m.shape = (Shape)((d1 - '0') * 10 + (d2 - '0'));
      m.col = c - 'a';
      movie.push_back(m);
    }
    ret[t] = std::move(movie);
  }
  return ret;
}

void Encoding::SaveSolutions(
    const std::string &filename,
    const std::map<uint8_t, std::vector<Move>> &sols) {
  FILE *f = fopen(filename.c_str(), "w");
  CHECK(f != nullptr) << filename;
  for (const auto &[idx, movie] : sols) {
    // no source for timing info here, so we just write the sentinel
    // '!', which is ignored above (it wants to find the space before
    // it, though).
    fprintf(f, "%02x %s !\n", idx,
            MovieString(movie).c_str());
  }
  fclose(f);
}


string Encoding::GraphicalMoveString(Move m) {
  const std::array<uint16_t, 4> mask = ShapeMaskInCol(m.shape, m.col);
  string ret;
  for (int r = 0; r < 4; r++) {
    StringAppendF(&ret, "|%s|\n", RowString(mask[r]).c_str());
  }
  for (char &c : ret) if (c == '#') c = '@';
  return ret;
}


string Encoding::MovieString(const std::vector<Move> &moves) {
  string s;
  for (Move m : moves) {
    CHECK(m.shape >= 0 && m.shape < 100) << m.shape;
    CHECK(m.col >= 0 && m.col < 10) << m.col;
    StringAppendF(&s, "%02d%c", m.shape, "abcdefghij"[m.col]);
  }
  return s;
}
