#ifndef _REPHRASE_UTF_H
#define _REPHRASE_UTF_H

// XXX To cc-lib?

#include <utility>
#include <cstdint>
#include <string_view>

struct UTF8 {

  // Convert up to len bytes of utf8 data to get one codepoint. Return
  // the number of bytes read and the codepoint. If the data are invalid,
  // reads one byte and returns 0xFFFFFFFF, which is an invalid codepoint.
  static std::pair<int, uint32_t> UTF8ToUTF32(const char *utf8, int len);

  static constexpr uint32_t INVALID = 0xFFFFFFFF;
};

// Iterate over the codepoints in a string.
struct UTF8Codepoints {
  UTF8Codepoints(std::string_view s);

  struct const_iterator {
    constexpr const_iterator(const char *ptr, const char *limit) :
      ptr(ptr), limit(limit) {}
    constexpr const_iterator(const const_iterator &other) = default;
    constexpr bool operator =(const const_iterator &other) const {
      return other.ptr == ptr;
    }
    constexpr bool operator !=(const const_iterator &other) const {
      return other.ptr != ptr;
    }
    const_iterator &operator ++();
    const_iterator operator ++(int postfix);
    uint32_t operator *() const;

  private:
    const char *ptr = nullptr;
    const char *limit = nullptr;
  };

  constexpr const_iterator begin() const {
    return begin_it;
  }

  constexpr const_iterator end() const {
    return end_it;
  }

private:
  const const_iterator begin_it, end_it;
};


#endif
