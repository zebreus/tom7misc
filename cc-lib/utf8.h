
// Basic header-only UTF-8 routines. These are often needed in
// unrelated libraries, so they are here with minimal dependencies.

#ifndef _CC_LIB_UTF8_H
#define _CC_LIB_UTF8_H

#include <string_view>
#include <cstdint>
#include <string>
#include <vector>

struct UTF8 {
  // Encode the codepoint as a 1-4 byte string. Returns the empty string
  // if the codepoint is out of range.
  static inline std::string Encode(uint32_t codepoint);

  static constexpr uint32_t REPLACEMENT_CODEPOINT = 0xFFFD;
  // Decode the string as a vector of codepoints, assuming it is
  // encoded correctly. This treats no codepoints specially. Invalid
  // encodings are transformed to U+FFFD, the replacement character,
  // but the algorithm for doing this is unspecified.
  static inline std::vector<uint32_t> Codepoints(std::string_view utf8);

  // Get the number of codepoints in the UTF-8 string, assuming it is
  // encoded correctly. Doesn't handle stuff like combining
  // characters, but it's better than strlen if the string is not pure
  // ASCII.
  static inline size_t Length(std::string_view utf8);

  // Truncate to at most the given number of codepoints.
  static inline std::string Truncate(std::string_view utf8, int max_length);
};


// Implementations follow.

std::string UTF8::Encode(uint32_t codepoint) {
  if (codepoint < 0x80) {
    std::string s;
    s.resize(1);
    s[0] = (uint8_t)codepoint;
    return s;
  } else if (codepoint < 0x800) {
    std::string s;
    s.resize(2);
    s[0] = 0xC0 | (codepoint >> 6);
    s[1] = 0x80 | (codepoint & 0x3F);
    return s;
  } else if (codepoint < 0x10000) {
    std::string s;
    s.resize(3);
    s[0] = 0xE0 | (codepoint >> 12);
    s[1] = 0x80 | ((codepoint >> 6) & 0x3F);
    s[2] = 0x80 | (codepoint & 0x3F);
    return s;
  } else if (codepoint < 0x110000) {
    std::string s;
    s.resize(4);
    s[0] = 0xF0 | (codepoint >> 18);
    s[1] = 0x80 | ((codepoint >> 12) & 0x3F);
    s[2] = 0x80 | ((codepoint >> 6) & 0x3F);
    s[3] = 0x80 | (codepoint & 0x3F);
    return s;
  }
  return "";
}

size_t UTF8::Length(std::string_view utf8) {
  size_t len = 0;
  for (size_t i = 0; i < utf8.size(); i++) {
    uint8_t c = utf8[i];
    // valid sequences are
    // 0xxxxxxx
    // 110xxxxx 10xxxxxx
    // 1110xxxx 10xxxxxx 10xxxxxx
    // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if (c & 0x80) {
      // high bit set.
      if ((c & 0b11100000) == 0b11000000) {
        // two-byte sequence.
        // we just skip the next byte because we're not validating.
        i++;
      } else if ((c & 0b11110000) == 0b11100000) {
        // three bytes
        i += 2;
      } else if ((c & 0b11111000) == 0b11110000) {
        i += 3;
      }
    } else {
      // ASCII
    }
    len++;
  }
  return len;
}

std::vector<uint32_t> UTF8::Codepoints(std::string_view utf8) {
  std::vector<uint32_t> ret;
  ret.reserve(utf8.size());
  for (size_t i = 0; i < utf8.size(); i++) {
    uint8_t c = utf8[i];
    // valid sequences are
    // 0xxxxxxx
    // 110xxxxx 10xxxxxx
    // 1110xxxx 10xxxxxx 10xxxxxx
    // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    if (c & 0x80) {
      // high bit set.
      if ((c & 0b11100000) == 0b11000000) {
        // two-byte sequence.
        if (i + 1 >= utf8.size()) {
          ret.push_back(REPLACEMENT_CODEPOINT);
          break;
        }

        uint8_t d = utf8[++i];

        // 110xxxxx 10xxxxxx
        if ((d & 0b11000000) == 0b10000000) {
          uint32_t cp = c & 0b00011111;
          cp <<= 6;
          cp |= (d & 0b00111111);
          ret.push_back(cp);
        } else {
          // second byte is invalid.
          ret.push_back(REPLACEMENT_CODEPOINT);
        }

      } else if ((c & 0b11110000) == 0b11100000) {
        // three bytes
        if (i + 2 >= utf8.size()) {
          ret.push_back(REPLACEMENT_CODEPOINT);
          break;
        }

        uint8_t d = utf8[++i];
        uint8_t e = utf8[++i];

        // 1110xxxx 10xxxxxx 10xxxxxx
        if ((d & 0b11000000) == 0b10000000 &&
            (e & 0b11000000) == 0b10000000) {
          uint32_t cp = c & 0b00001111;
          cp <<= 6;
          cp |= (d & 0b00111111);
          cp <<= 6;
          cp |= (e & 0b00111111);
          ret.push_back(cp);
        } else {
          // second byte is invalid.
          ret.push_back(REPLACEMENT_CODEPOINT);
        }

      } else if ((c & 0b11111000) == 0b11110000) {
        // four bytes
        if (i + 3 >= utf8.size()) {
          ret.push_back(REPLACEMENT_CODEPOINT);
          break;
        }

        uint8_t d = utf8[++i];
        uint8_t e = utf8[++i];
        uint8_t f = utf8[++i];

        // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        if ((d & 0b11000000) == 0b10000000 &&
            (e & 0b11000000) == 0b10000000 &&
            (f & 0b11000000) == 0b10000000) {
          uint32_t cp = c & 0b00000111;
          cp <<= 6;
          cp |= (d & 0b00111111);
          cp <<= 6;
          cp |= (e & 0b00111111);
          cp <<= 6;
          cp |= (f & 0b00111111);
          ret.push_back(cp);
        } else {
          // second byte is invalid.
          ret.push_back(REPLACEMENT_CODEPOINT);
        }

      } else {
        // If the broken encoding is multi-byte, there might be a
        // better choice than inserting multiple replacement chars,
        // but for now this is simplest.
        ret.push_back(REPLACEMENT_CODEPOINT);
      }
    } else {
      // ASCII
      ret.push_back(c);
    }
  }

  return ret;
}


std::string UTF8::Truncate(std::string_view utf8, int max_length) {
  std::vector<uint32_t> codepoints = Codepoints(utf8);
  if ((int)codepoints.size() > max_length) {
    codepoints.resize(max_length);
    std::string ret;
    ret.resize(utf8.size());
    for (uint32_t cp : codepoints) {
      ret += Encode(cp);
    }
    return ret;
  } else {
    return std::string(utf8);
  }
}

#endif

