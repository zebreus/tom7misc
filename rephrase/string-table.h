
#ifndef _REPHRASE_STRING_TABLE_H
#define _REPHRASE_STRING_TABLE_H

#include <algorithm>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "dense-int-set.h"

// Table containing a fixed set of distinct strings (e.g.
// variable names) that are given dense, unique indices.
// This can be used to construct an efficient set or map.
struct StringTable {
  StringTable(const std::unordered_set<std::string> &universe) {
    value.reserve(universe.size());
    for (const std::string &s : universe) {
      value.push_back(s);
    }
    std::sort(value.begin(), value.end());
    for (int i = 0; i < (int)value.size(); i++) {
      index[value[i]] = i;
    }
  }

  int Index(const std::string &value) const {
    auto it = index.find(value);
    CHECK(it != index.end()) << "String outside of universe: " << value;
    return it->second;
  }

  const std::string &Value(int idx) const {
    return value[idx];
  }

  size_t size() const { return value.size(); }
  size_t Size() const { return size(); }

 private:
  std::unordered_map<std::string, int> index;
  std::vector<std::string> value;
};

struct StringSet {
  // Create an empty set using the table.
  StringSet(const StringTable *table) : table(table), ds(table->Size()) {}

  // Value semantics.
  StringSet(const StringSet &other) = default;
  StringSet(StringSet &&other) = default;
  StringSet &operator=(const StringSet &other) = default;
  StringSet &operator=(StringSet &&other) = default;

  void Remove(const std::string &s) {
    ds.Remove(table->Index(s));
  }

  void Add(const std::string &s) {
    ds.Add(table->Index(s));
  }

  bool Contains(const std::string &s) const {
    return ds.Contains(table->Index(s));
  }

  void Clear() {
    ds.Clear();
  }

  // Compatibility with std::set etc.
  void insert(const std::string &s) { Add(s); }
  void erase(const std::string &s) { return Remove(s); }
  bool contains(const std::string &s) const { return Contains(s); }
  void clear() { Clear(); }

  template<class Iter>
  void insert(Iter start, Iter limit) {
    while (start != limit) {
      Add(*start);
      ++start;
    }
  }

  struct const_iterator {
    using value_type = std::string;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    const std::string &operator *() const {
      return parent->table->Value(*dsi);
    }

    const_iterator &operator++() {
      ++dsi;
      return *this;
    }

    bool operator==(const const_iterator& other) const {
      DCHECK(parent == other.parent);
      return dsi == other.dsi;
    }
    bool operator!=(const const_iterator& other) const {
      DCHECK(parent == other.parent);
      return dsi != other.dsi;
    }

   private:
    friend struct StringSet;
    const_iterator(const StringSet *parent,
                   DenseIntSet::const_iterator dsi) :
      parent(parent), dsi(std::move(dsi)) {}

    const StringSet *parent = nullptr;
    DenseIntSet::const_iterator dsi;
  };

  const_iterator begin() const {
    return const_iterator(this, ds.begin());
  }

  const_iterator end() const {
    return const_iterator(this, ds.end());
  }

 private:
  const StringTable *table;
  DenseIntSet ds;
};

#endif
