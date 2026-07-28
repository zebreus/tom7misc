
#include "aig.h"

#include <bit>
#include <cstdint>
#include <utility>

#include "prop.h"

using Node = AIG::Node;
using Row = AIG::Row;

static uint64_t HashVar(int v) {
  return 0x77773333CAFE0000 + v;
}

static uint64_t HashTrue() {
  return 0x123456789;
}

static uint64_t HashFalse() {
  return 0x987654321;
}

static uint64_t HashAnd(Node a, Node b) {
  DCHECK(a <= b);
  return 0xC0FFEE ^ ((uint64_t)a * 0x9e3779b97f4a7c15ULL) ^
    std::rotr<uint64_t>((uint64_t)b, 51);
}

// Register (in the hash table) an entry that is already in the pool,
// using its index.
void AIG::Register(int idx) {
  CHECK(idx >= 0 && idx < pool.size());
  CHECK(idx < 0x3FFFFFFF);
  uint64_t h = pool[idx].hash;
  table[h].push_back(idx);
}

AIG::AIG(const World &world) :
  AIG(world.symbol_names.size()) {
}

AIG::AIG(int num_vars) : num_vars(num_vars) {
  pool.emplace_back(Row{
      .data = 0x00,
      .hash = HashFalse(),
    });

  pool.emplace_back(Row{
      .data = 0x00,
      .hash = HashTrue(),
    });

  for (int i = 0; i < num_vars; i++) {
    Row var{
      .data = 0x0,
      .hash = HashVar(i),
    };

    pool.push_back(var);
  }

  CHECK(pool.size() == num_vars + 2);
  for (int i = 0; i < pool.size(); i++) {
    Register(i);
  }
}


AIG::Node AIG::AND(Node lhs, Node rhs) {
  // Normalize to encourage sharing.
  if (rhs < lhs) std::swap(lhs, rhs);

  // (a AND a) = a.
  if (lhs == rhs)
    return lhs;

  // (a AND ¬a) = false
  if ((lhs ^ rhs )== 0b1)
    return F();

  // (a AND false) = false
  if (lhs == F() ||
      rhs == F()) return F();

  // (a AND true) = a
  if (lhs == T()) return rhs;
  if (rhs == T()) return lhs;

  Row row{
    .data = (uint64_t(lhs) << 32) | rhs,
    .hash = HashAnd(lhs, rhs),
  };

  auto it = table.find(row.hash);
  if (it != table.end()) {
    for (size_t i = 0; i < it->second.size(); i++) {
      uint32_t match_idx = it->second[i];
      // AND cannot equal constants or variables.
      if (match_idx >= 2 + uint32_t(num_vars) &&
          // And the row data must actually equal.
          pool[match_idx].data == row.data) {
        return Pack(match_idx);
      }
    }
  }

  size_t idx = pool.size();
  pool.emplace_back(row);
  Register(idx);
  return Pack(idx);
}
