/* "FM7" format reader and writer.
   Like SimpleFM2, assumes a sequence of inputs from hard power-on;
   no metadata. No subtitle support. The only advantage over FM2 is
   that the files are much, much smaller.

   The runtime representation is compatible with SimpleFM2, so it's
   easy to use the two together.
 */

#ifndef _FCEULIB_SIMPLEFM7_H
#define _FCEULIB_SIMPLEFM7_H

#include <vector>
#include <string>
#include <utility>
#include <cstdint>

#include "types.h"

struct SimpleFM7 {
  static std::vector<std::pair<uint8_t, uint8_t>>
  ReadInputs2P(const std::string &filename);
  // Same, but discards the second player.
  static std::vector<uint8_t> ReadInputs(const std::string &filename);

  // Same, but from a string.
  static std::vector<std::pair<uint8_t, uint8_t>>
  ParseString2P(const std::string &contents);
  static std::vector<uint8> ParseString(const std::string &contents);
  
  static void WriteInputs2P(
      const std::string &outputfile,
      const std::vector<std::pair<uint8_t, uint8_t>> &inputs);
  // Second player always 0.
  static void WriteInputs(const std::string &outputfile,
                          const std::vector<uint8_t> &inputs);

  // Same, but to a string.
  static std::string EncodeInputs(const std::vector<uint8_t> &inputs);
  static std::string EncodeInputs2P(
      const std::vector<std::pair<uint8_t, uint8_t>> &inputs);

  // Encode as a C++ string literal for easy embedding in source code.
  static std::string EncodeInputsLiteral(
      const std::vector<uint8_t> &inputs,
      // max line length is not guaranteed, so give some slack
      int indent, int line_length);
  static std::string EncodeInputsLiteral2P(
      const std::vector<std::pair<uint8_t, uint8_t>> &inputs,
      int indent, int line_length);
};

#endif
