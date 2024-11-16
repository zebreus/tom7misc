
#include "utf.h"

#include <cstdint>
#include <string_view>

#include "base/logging.h"
#include "utf8.h"

UTF8Codepoints::UTF8Codepoints(std::string_view s) :
  begin_it(s.data(), s.data() + s.size()),
  end_it(s.data() + s.size(), s.data() + s.size()) {}


UTF8Codepoints::const_iterator &UTF8Codepoints::const_iterator::operator ++() {
  // prefix.
  const auto &[code_len, code] = UTF8::ParsePrefix(ptr, limit - ptr);
  CHECK(code != UTF8::INVALID) << "Invalid UTF-8 encoding.";
  ptr += code_len;
  return *this;
}

UTF8Codepoints::const_iterator
UTF8Codepoints::const_iterator::operator ++(int postfix) {
  auto old = *this;
  const auto &[code_len, code] = UTF8::ParsePrefix(ptr, limit - ptr);
  CHECK(code != UTF8::INVALID) << "Invalid UTF-8 encoding.";
  ptr += code_len;
  return old;
}

uint32_t UTF8Codepoints::const_iterator::operator *() const {
  const auto &[code_len_, code] = UTF8::ParsePrefix(ptr, limit - ptr);
  return code;
}

