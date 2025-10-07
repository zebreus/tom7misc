#include "sourcemap.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "crypt/sha256.h"
#include "map-util.h"
#include "util.h"

SourceMap::SourceMap(const std::string &filename,
                     const std::string &contents) : filename(filename) {
  lines = Util::SplitToLines(contents);
  hash = SHA256::Ascii(SHA256::HashString(contents));
}

void SourceMap::Save(const std::string &outfile) const {
  std::string out = std::format("{}\n{}\n", filename, hash);

  std::vector<std::pair<uint16_t, int>> sorted =
    MapToSortedVec(code);

  for (const auto &[addr, line] : sorted) {
    AppendFormat(&out, "{:04x} {}\n", addr, line);
  }

  Util::WriteFile(outfile, out);
}

SourceMap SourceMap::Empty() {
  SourceMap sm;
  sm.filename = "empty";
  sm.hash = SHA256::Ascii(SHA256::HashString(""));
  return sm;
}

std::unordered_map<int, uint16_t> SourceMap::InvertCode() const {
  std::unordered_map<int, uint16_t> invert;
  for (const auto &[addr, line] : code) {
    invert[line] = addr;
  }
  return invert;
}

SourceMap SourceMap::FromFile(const std::string &sourcemap_filename) {
  std::vector<std::string> slines =
    Util::ReadFileToLines(sourcemap_filename);

  if (slines.size() < 2) return Empty();
  SourceMap ret;
  ret.filename = std::move(slines[0]);
  ret.hash = std::move(slines[1]);
  std::string contents = Util::ReadFile(ret.filename);
  std::string actual_hash = SHA256::Ascii(SHA256::HashString(contents));

  if (actual_hash != ret.hash) {
    Print(stderr, AORANGE("Warning") ": Source file " AWHITE("%s")
          " hash does not agree with source map " AWHITE("%s") ".\n"
          "It must have changed; using empty source map.\n",
          ret.filename, sourcemap_filename);
    return Empty();
  }

  ret.lines = Util::SplitToLines(contents);

  for (int i = 2; i < slines.size(); i++) {
    std::vector<std::string> parts = Util::Tokenize(slines[i], ' ');
    if (parts.empty()) continue;
    CHECK(parts.size() == 2) << "Malformed " << sourcemap_filename
                             << " on line " << i;
    uint16_t addr = strtol(parts[0].c_str(), nullptr, 16);
    int line = strtol(parts[1].c_str(), nullptr, 10);
    CHECK(line >= 0 && line < ret.lines.size()) << "Malformed " <<
      sourcemap_filename << " on line " << i << ": Index is out of "
      "bounds for the source file.";
    ret.code[addr] = line;
  }

  return ret;
}
