
#ifndef _CC_LIB_SPAN_UTIL_H
#define _CC_LIB_SPAN_UTIL_H

#include <span>
#include <cstddef>

template<class CA, class CB>
constexpr inline bool SpanEq(const CA &a, const CB &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (!(a[i] == b[i]))
      return false;
  return true;
}

// This allows you to create a literal span to pass to
// a function that takes std::span. For example, in a
// compiler you might want to do
//    const Exp *e1, *e2;
//    return Primop(INT_PLUS, {e1, e2});
// which works if Primop takes a std::vector<const Exp*>
// but not if it takes a span. This class lets you write
//    return Primop(INT_PLUS, S{e1, e2});
// which forces it to construct an initializer_list and
// pass a span viewing that. Of course you need to be
// careful about lifetime, but the initializer_list will
// last "until the semicolon".
//
// In c++26 this may not be necessary.
template <class T>
struct Span {
  constexpr Span(std::initializer_list<T> il) : il(il) {}

  // Implicit conversion to span.
  constexpr operator std::span<const T>() const {
    return std::span<const T>(il.begin(), il.size());
  }
 private:
  std::initializer_list<T> il;
};

#endif
