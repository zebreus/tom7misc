
// PEM files are base64-encoded files like certificates and keys,
// used in some public-key cryptography systems and TLS.

#ifndef _CC_LIB_CRYPT_PEM_H
#define _CC_LIB_CRYPT_PEM_H

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct PEM {
  static std::string ToPEM(std::span<const uint8_t> der_bytes,
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
