#ifndef _FORMULA_H
#define _FORMULA_H

#include <variant>
#include <memory>
#include <cstdint>

#include "byteset.h"

struct ByteSetForm {
  ByteSet bs;
};

struct Word8Form {
  uint8_t word = 0;
};

struct Word16Form {
  uint16_t word = 0;
};

enum class Binop {
  // Conjunction of two formulas
  AND,
  // Disjunction of two formulas
  OR,
  // 8-bit value in ByteSet
  IN8,
};

struct BinForm;
using Form = std::variant<ByteSetForm, Word8Form, Word16Form, BinForm>;

struct BinForm {
  Binop op;
  std::shared_ptr<Form> lhs, rhs;
};

#endif
