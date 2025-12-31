
#ifndef _HTTPI_PEM_H
#define _HTTPI_PEM_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct PEM {
  static std::string ToPEM(const std::vector<uint8_t> &der_bytes,
                           std::string_view header);

  // Get the decoded bytes, possibly multiple in the same file.
  // Aborts if the data are detectably malformed.
  static std::vector<std::vector<uint8_t>> ParsePEMs(
      std::string_view contents,
      std::string_view header);

  // Parse just one. Aborts if malformed, or if multiple ones.
  static std::vector<uint8_t> ParsePEM(
      std::string_view contents,
      std::string_view header);
};

#endif
