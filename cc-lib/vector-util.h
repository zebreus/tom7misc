// Some native utilities on the std::vector type.
// Everything in here is single-threaded to improve portability; for
// multithreaded stuff, see threadutil.h.

#ifndef _CC_LIB_VECTOR_UTIL_H
#define _CC_LIB_VECTOR_UTIL_H

#include <vector>

template<class A, class F>
static auto MapVector(const std::vector<A> &vec, const F &f) ->
  std::vector<decltype(f(vec[0]))> {
  using B = decltype(f(vec[0]));
  std::vector<B> ret;
  ret.resize(vec.size());
  for (int i = 0; i < vec.size(); i++) {
    ret[i] = f(vec[i]);
  }
  return ret;
}

// Apply the function to each element in the vector.
template<class A, class F>
static void AppVector(const std::vector<A> &vec, const F &f) {
  for (const auto &elt : vec) f(elt);
}

// Keep the elements x in the vector for which f(x) returns true.
// Retains the original order. Linear time.
template<class A, class F>
static void FilterVector(std::vector<A> *vec, const F &f) {
  size_t dst = 0, src = 0;
  for (; src < vec->size(); src++) {
    if (f((*vec)[src])) {
      if (dst != src) {
        (*vec)[dst] = std::move(*vec)[src];
      }
      dst++;
    }
  }
  vec->resize(dst);
}

#endif
