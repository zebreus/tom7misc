
#ifndef _HTTPI_PEM_H
#define _HTTPI_PEM_H

#include <cstdint>
#include <string>
#include <vector>

struct PEM {
  static std::string ToPEM(const std::vector<uint8_t> &der_bytes,
                           const std::string &header);
};

#endif
