// Some native utilities on the std::vector type (or sometimes, std::span).
// Everything in here is single-threaded to improve portability; for
// multithreaded stuff, see threadutil.h.
//
// PERF: Several of these can be done in place with some care.

#ifndef _CC_LIB_VECTOR_UTIL_H
#define _CC_LIB_VECTOR_UTIL_H

#include <cstdint>
#include <vector>
#include <span>

template<class A, class F>
static auto VectorMap(const std::vector<A> &vec, const F &f) ->
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
static void VectorApp(const std::vector<A> &vec, const F &f) {
  for (const auto &elt : vec) f(elt);
}

// Keep the elements x in the vector for which f(x) returns true.
// Retains the original order. Linear time.
template<class A, class F>
static void VectorFilter(std::vector<A> *vec, const F &f) {
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

template<class A>
static void VectorReverse(std::vector<A> *vec) {
  std::vector<A> rev;
  rev.reserve(vec->size());
  for (size_t i = 0; i < vec->size(); i++) {
    rev.push_back((*vec)[vec->size() - 1 - i]);
  }
  *vec = std::move(rev);
}

template<class A>
static bool VectorContains(const std::vector<A> &vec, const A &a) {
  for (const auto &aa : vec) {
    if (a == aa) return true;
  }
  return false;
}

// Rotate right in place. A positive offset means each element moves
// to the right (wrapping around).
template<class A>
static void VectorRotateRight(std::vector<A> *vec,
                              int64_t offset) {
  const int64_t size = vec->size();

  if (vec->empty()) return;
  int64_t d = offset % size;
  if (d < 0) d = (d + size) % size;
  if (d == 0) return;

  std::vector<A> rot;
  rot.resize(size);
  for (int i = 0; i < size; i++) {
    rot[(i + d) % size] = (*vec)[i];
  }

  *vec = std::move(rot);
}

// Concatenate one or more containers into a vector<T>,
// e.g. as VectorConcat(vec1, span2, vec3)
template<class First, class... Rest>
auto VectorConcat(const First &first, const Rest &...rest) {
  using T = std::remove_const_t<typename First::value_type>;

  size_t total_size = first.size() + (rest.size() + ...);

  std::vector<T> out;
  out.reserve(total_size);

  auto Append = [&](const auto& container) {
      out.insert(out.end(), container.begin(), container.end());
    };

  Append(first);
  (Append(rest), ...);

  return out;
}

#endif
