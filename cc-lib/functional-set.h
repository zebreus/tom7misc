
#ifndef _CC_LIB_FUNCTIONAL_SET_H
#define _CC_LIB_FUNCTIONAL_SET_H

#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "functional-map.h"
#include "hashing.h"

// This is a simple wrapper around FunctionalMap for the case that the
// value type is uninteresting. It also supports functionally removing elements.
//
// Value semantics.
template<
  class Key_,
  // These must be efficiently default-constructible.
  class Hash_ = Hashing<Key_>,
  class KeyEqual_ = std::equal_to<Key_>>
struct FunctionalSet {
  using Key = Key_;
  using Hash = Hash_;
  using KeyEqual = KeyEqual_;

  // Empty.
  FunctionalSet() {}
  // Value semantics.
  FunctionalSet(const FunctionalSet &other) = default;
  FunctionalSet(FunctionalSet &&other) = default;
  FunctionalSet &operator =(const FunctionalSet &other) = default;
  FunctionalSet &operator =(FunctionalSet &&other) = default;

  bool Contains(const Key &k) const {
    if (const Value *v = m.FindPtr(k)) {
      return *v == Value::PRESENT;
    }
    return false;
  }

  FunctionalSet Insert(const Key &k) const {
    return FunctionalSet(m.Insert(k, Value::PRESENT));
  }

  FunctionalSet Remove(const Key &k) const {
    return FunctionalSet(m.Insert(k, Value::ABSENT));
  }

  // i.e., union
  FunctionalSet Insert(const FunctionalSet &other) const {
    FunctionalSet ret = *this;
    for (const Key &k : other.Export()) {
      ret = ret.Insert(k);
    }
    return ret;
  }

  // Initialize with a set of bindings.
  FunctionalSet(const std::vector<Key> &vec) : m(AddPresent(vec)) {}

  // Flatten to a single map, where inner bindings shadow outer
  // ones as expected. Copies the whole map, so this is linear time.
  std::unordered_set<Key, Hash, KeyEqual>
  Export() const {
    std::unordered_set<Key, Hash, KeyEqual> ret;
    for (const auto &[k, v] : m.Export()) {
      if (v == Value::PRESENT) {
        ret.insert(k);
      }
    }
    return ret;
  }

 private:
  enum class Value { PRESENT, ABSENT, };
  using MapType = FunctionalMap<Key, Value>;

  FunctionalSet(MapType &&mm) : m(mm) {}

  static const std::vector<std::pair<Key, Value>> AddPresent(
      const std::vector<Key> &vec) {
    std::vector<std::pair<Key, Value>> ret;
    ret.reserve(vec.size());
    for (const Key &k : vec) {
      ret.emplace_back(k, Value::PRESENT);
    }
    return ret;
  }

  MapType m;
};

#endif
