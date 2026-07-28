
#ifndef _TOWARD_AIG_H
#define _TOWARD_AIG_H

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "inline-vector.h"
#include "prop.h"

struct AIG {
  // Need to know the number of variables up front.
  explicit AIG(const World &world);
  explicit AIG(int num_vars);

  // Packed index into the pool + not bit (lsb).
  using Node = uint32_t;

  // A raw index into the table, optionally negated.
  static Node Pack(size_t idx, bool negate = false) {
    CHECK(idx <= 0x7FFFFFFF);
    return Node((idx << 1) | (negate ? 0b1 : 0b0));
  }

  // All of these combinators are semantic, not structural;
  // they may rewrite to an equivalent expression.

  Node F() const { return Pack(0); }
  Node T() const { return Pack(1); }

  Node V(int id) const {
    CHECK(id >= 0 && id < num_vars);
    return Pack(id + 2);
  }

  // This may allocate new entries in the pool.
  Node AND(Node lhs, Node rhs);

  Node NOT(Node n) const {
    return n ^ 0b1;
  }

  // Most of this is implementation details!

  static_assert(sizeof (uint64_t) >= sizeof (void *));


  struct Row {
    // For an AND, the lhs and rhs (Node).
    // For a variable, zero (index gives its identity).
    uint64_t data;
    // Hash value for this node.
    uint64_t hash;
  };

  // AIG requires knowing the set of variables
  // up front. These are the first entries in the pool.
  const int num_vars = 0;

  // Allocated nodes. The indices are permanent.
  // 0 is always false, 1 is always true. After that,
  // num_vars variable entries.
  std::vector<Row> pool;

  // Hash table mapping hash codes to indices in the table.
  // For x in table[h], we have
  //    pool[x].hash == h.
  std::unordered_map<uint64_t, InlineVector<uint32_t>> table;

 private:
  void Register(int idx);
};


#endif

