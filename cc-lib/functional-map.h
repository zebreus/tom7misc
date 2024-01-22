
#ifndef _CC_LIB_FUNCTIONAL_MAP_H
#define _CC_LIB_FUNCTIONAL_MAP_H

#include <unordered_map>
#include <variant>
#include <functional>

#include "hashing.h"
#include "base/logging.h"

// This is a limited functional map type. The canonical use is a
// context in a programming language implementation, which contains
// statically-scoped variables mapped to e.g. their types. It supports
// reasonably efficient copy-and-insert and lookup, and could perhaps
// be extended to include more.
//
// Values can be copied, so they should be wrapped somehow if their
// identity needs to be preserved.
template<
  class Key_, class Value_,
  // These must be efficiently default-constructible.
  class Hash_ = Hashing<Key_>,
  class KeyEqual_ = std::equal_to<Key_>>
struct FunctionalMap {
  using Key = Key_;
  using Value = Value_;
  using Hash = Hash_;
  using KeyEqual = KeyEqual_;

  // Empty.
  FunctionalMap() : depth(0), data(HashMap{}) {}

  // Or returns null if not present.
  const Value *FindPtr(const Key &k) const {
    const FunctionalMap *f = this;
    for (;;) {
      if (const Cell *cell = std::get_if<Cell>(&f->data)) {
        const auto &[kk, vv, pp] = *cell;
        if (KeyEqual()(k, kk)) {
          return &vv;
        } else {
          f = pp;
        }
      } else {
        CHECK(std::holds_alternative<HashMap>(f->data));
        const HashMap &m = std::get<HashMap>(f->data);
        auto it = m.find(k);
        if (it == m.end()) return nullptr;
        else return &it->second;
      }
    }
  }

  bool Contains(const Key &k) const {
    return FindPtr(k) != nullptr;
  }

  // The returned instance may refer to 'this', and so
  // this object must outlive the copy.
  FunctionalMap Insert(const Key &k, Value v) const {
    if (depth > kLinearDepth) {
      CHECK(std::holds_alternative<HashMap>(data)) << "Bug: Depth implies "
        "pointer representation.";
      HashMap m = GetAll(this);
      m[k] = std::move(v);
      return FunctionalMap(std::move(m));
    } else {
      return FunctionalMap(
          std::make_tuple(k, std::move(v), this),
          depth + 1);
    }
  }

 private:
  using Ptr = const FunctionalMap *;
  using Cell = std::tuple<Key, Value, Ptr>;
  using HashMap =
    std::unordered_map<Key, Value, Hash, KeyEqual>;
  int depth = 0;

  // XXX tune
  // Every 10 we create a cell with a hash map of
  // everything beneath it.
  // PERF: Perhaps better to actually flatten the nodes
  // in place, but we have to deal with "mutable" and
  // type safety then.
  static constexpr int kLinearDepth = 10;

  static HashMap GetAll(const FunctionalMap *f) {
    if (const Cell *cell = std::get_if<Cell>(&f->data)) {
      const auto &[kk, vv, pp] = *cell;
      HashMap ret = GetAll(pp);
      ret[kk] = vv;
      return ret;
    } else {
      CHECK(std::holds_alternative<HashMap>(f->data));
      return std::get<HashMap>(f->data);
    }
  }

  explicit FunctionalMap(HashMap m) : depth(0), data(std::move(m)) {}

  // Make a new list cell.
  FunctionalMap(Cell cell, int depth) :
    depth(depth), data(std::move(cell)) {}

  std::variant<Cell, HashMap> data;
};

#endif
