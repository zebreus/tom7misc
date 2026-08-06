
#ifndef _TOWARD_SIMPLIFICATION_H
#define _TOWARD_SIMPLIFICATION_H

#include <memory>

#include "minitable.h"
#include "prop.h"

struct Simplification {
  static constexpr uint32_t OPT_AND = MiniTable::OPT_AND;
  static constexpr uint32_t OPT_NOT = MiniTable::OPT_NOT;
  static constexpr uint32_t OPT_XOR = MiniTable::OPT_XOR;
  static constexpr uint32_t OPT_OR = MiniTable::OPT_OR;
  static constexpr uint32_t OPT_NAND = MiniTable::OPT_NAND;
  static constexpr uint32_t OPT_NOR = MiniTable::OPT_NOR;
  static constexpr uint32_t OPT_ITE = MiniTable::OPT_ITE;

  // Default leaving out XOR.
  Simplification(uint32_t opts =
                 OPT_AND | OPT_NOT | OPT_OR |
                 OPT_NAND | OPT_NOR | OPT_ITE);

  Prop Simplify(const Prop &in) const;

 private:
  std::unique_ptr<MiniTable> table;
};

#endif
