
#include "utf.h"

#include <cstdint>
#include <utility>
#include <string_view>

#include "base/logging.h"

std::pair<int, uint32_t> UTF8::UTF8ToUTF32(const char *utf8, int len) {
  CHECK(len > 0 && utf8 != nullptr);

  uint8_t mask = 0;
  uint32_t ch = *(const uint8_t *)utf8;
  if ((ch & 0x80) == 0) {
    len = 1;
    mask = 0x7f;
  } else if ((ch & 0xe0) == 0xc0 && len >= 2) {
    len = 2;
    mask = 0x1f;
  } else if ((ch & 0xf0) == 0xe0 && len >= 3) {
    len = 3;
    mask = 0xf;
  } else if ((ch & 0xf8) == 0xf0 && len >= 4) {
    len = 4;
    mask = 0x7;
  } else {
    return std::make_pair(1, INVALID);
  }

  ch = 0;
  for (int i = 0; i < len; i++) {
    int shift = (len - i - 1) * 6;
    if (!*utf8)
      return std::make_pair(1, INVALID);
    if (i == 0)
      ch |= ((uint32_t)(*utf8++) & mask) << shift;
    else
      ch |= ((uint32_t)(*utf8++) & 0x3f) << shift;
  }

  return std::make_pair(len, ch);
}

UTF8Codepoints::UTF8Codepoints(std::string_view s) :
  begin_it(s.data(), s.data() + s.size()),
  end_it(s.data() + s.size(), s.data() + s.size()) {}


UTF8Codepoints::const_iterator &UTF8Codepoints::const_iterator::operator ++() {
  // prefix.
  const auto &[code_len, code] = UTF8::UTF8ToUTF32(ptr, limit - ptr);
  CHECK(code != UTF8::INVALID) << "Invalid UTF-8 encoding.";
  ptr += code_len;
  return *this;
}

UTF8Codepoints::const_iterator
UTF8Codepoints::const_iterator::operator ++(int postfix) {
  auto old = *this;
  const auto &[code_len, code] = UTF8::UTF8ToUTF32(ptr, limit - ptr);
  CHECK(code != UTF8::INVALID) << "Invalid UTF-8 encoding.";
  ptr += code_len;
  return old;
}

uint32_t UTF8Codepoints::const_iterator::operator *() const {
  const auto &[code_len_, code] = UTF8::UTF8ToUTF32(ptr, limit - ptr);
  return code;
}

