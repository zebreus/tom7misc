
// Assembly metadata that indexes the .asm file directly.
// It maps ROM addresses to source lines.
//
// The reason to do this is so that I can generate annotated
// source code during model checking.

#ifndef _DTAS_SOURCEMAP_H
#define _DTAS_SOURCEMAP_H

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

#include "base/logging.h"

struct SourceMap {
  std::string filename;
  // Ascii SHA-256 of the source, so that we can
  // detect if it has been modified.
  std::string hash;

  // Maps code memory address (0x8000 - 0xffff)
  // to 0-based source line offset (of the instruction).
  // Mid-instruction addresses are not included.
  std::unordered_map<uint16_t, int> code;

  // The lines of the file, which we have available when
  // the SourceMap is in memory.
  std::vector<std::string> lines;

  // TODO: We could also annotate memory locations,
  // but perhaps it is better to do that symbolically.

  // Get the inverted map, which maps each relevant line index to the
  // address that is defined there.
  std::unordered_map<int, uint16_t> InvertCode() const;

  // Create empty source map for this file, to build it during
  // assembly.
  SourceMap(const std::string &filename, const std::string &contents);

  void Save(const std::string &outfile) const;

  // An empty, degenerate sourcemap. This is used for error conditions
  // when loading. To create a new sourcemap, use the constructor above.
  static SourceMap Empty();

  // Load from a previously saved sourcemap. This also loads the
  // source file. If either file can't be opened or its contents
  // doesn't match the saved hash, an error is printed and the
  // sourcemap will be empty. If the file is malformed, abort.
  static SourceMap FromFile(const std::string &sourcemap_filename);
  bool IsEmpty() const { return code.empty(); }


 private:
  // Use Empty().
  SourceMap() {}
};

#endif
