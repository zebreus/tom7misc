
#include "pem.h"

#include <format>
#include <string>
#include <vector>
#include <cstdint>
#include <string_view>

#include "base/logging.h"
#include "base64.h"
#include "util.h"

std::string PEM::ToPEM(const std::vector<uint8_t> &der_bytes,
                       std::string_view header) {
  std::string b64 = Base64::EncodeV(der_bytes);
  std::string pem = std::format("-----BEGIN {}-----\n", header);
  for (size_t i = 0; i < b64.length(); i += 64) {
    pem += b64.substr(i, 64) + "\n";
  }
  pem += std::format("-----END {}-----\n", header);
  return pem;
}

std::vector<std::vector<uint8_t>> PEM::ParsePEMs(
      std::string_view contents,
      std::string_view header) {

  std::string begin = std::format("-----BEGIN {}-----\n", header);
  std::string end = std::format("-----END {}-----\n", header);

  std::vector<std::vector<uint8_t>> bytes;

  for (;;) {
    size_t bpos = contents.find(begin);
    if (bpos == std::string_view::npos) break;
    contents.remove_prefix(bpos + begin.size());

    size_t epos = contents.find(end);
    CHECK(epos != std::string_view::npos) <<
      "PEM content has BEGIN without END.";

    std::string_view body = contents.substr(0, epos);

    std::string base64 =
      Util::RemoveCharsMatching(body, Util::IsWhitespace);
    bytes.push_back(Base64::DecodeV(base64));

    contents.remove_prefix(epos + end.size());
  }

  return bytes;
}
