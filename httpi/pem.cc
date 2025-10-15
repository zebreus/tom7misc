
#include "pem.h"

#include <string>
#include <vector>
#include <cstdint>

#include "base64.h"

std::string PEM::ToPEM(const std::vector<uint8_t> &der_bytes,
                       const std::string &header) {
  std::string b64 = Base64::EncodeV(der_bytes);
  std::string pem = "-----BEGIN " + header + "-----\n";
  for (size_t i = 0; i < b64.length(); i += 64) {
    pem += b64.substr(i, 64) + "\n";
  }
  pem += "-----END " + header + "-----\n";
  return pem;
}
