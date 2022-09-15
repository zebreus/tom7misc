
// Generic hashing for use in e.g. std::unordered_map. Not guaranteed to
// be stable over time.
//
// It is not allowed to extend std::hash (or anything in std::) unless
// the declaration involves a user-defined type, so the absence of
// std::hash on containers like std::pair is pretty annoying.
//
// Use like:
//  using HashMap =
//     std::unordered_map<std::pair<int, std::string>, int,
//                        Hashing<std::pair<int, std::string>>;

#ifndef _CC_LIB_HASHING_H
#define _CC_LIB_HASHING_H

#include <utility>
#include <functional>

namespace hashing_internal {
static constexpr inline std::size_t RotateSizeT(size_t v, int bits) {
  return (v << bits) | (v >> (sizeof v * 8 - bits));
}
}

// Forward anything that already has std::hash overloaded for it.
template<class T>
struct Hashing {
  // TODO: Better to use perfect forwarding here?
  // It of course gave me an inscrutable error when I tried.
  std::size_t operator()(const T &t) const {
    return std::hash<T>()(t);
  }
};

template<class T, class U>
struct Hashing<std::pair<T, U>> {
  std::size_t operator()(const std::pair<T, U> &p) const {
    size_t th = Hashing<T>()(p.first), uh = Hashing<U>()(p.second);
    // This can certainly be improved. Keep in mind that size_t
    // is commonly either 32 or 64 bits.
    return th + 0x9e3779b9 + hashing_internal::RotateSizeT(uh, 15);
  }
};

// PERF: Probably should at least specialize for numeric types like
// uint8_t.
template<class T>
struct Hashing<std::vector<T>> {
  std::size_t operator()(const std::vector<T> &v) const {
    size_t h = 0xCAFED00D + v.size();
    for (const T &t : v) {
      h += Hashing<T>()(t);
      h = hashing_internal::RotateSizeT(h, 13);
    }
    return h;
  }
};

template<class T, size_t N>
struct Hashing<std::array<T, N>> {
  std::size_t operator()(const std::array<T, N> &v) const {
    size_t h = 0xDECADE00 + N;
    for (const T &t : v) {
      h += Hashing<T>()(t);
      h = hashing_internal::RotateSizeT(h, 17);
    }
    return h;
  }
};

#endif
