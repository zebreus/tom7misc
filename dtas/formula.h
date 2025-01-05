#ifndef _FORMULA_H
#define _FORMULA_H

#include <string>
#include <variant>
#include <memory>
#include <cstdint>
#include <vector>

#include "byteset.h"

struct ByteSetForm {
  ByteSet bs;
};

struct IntForm {
  int64_t value = 0;
};

struct VarForm {
  std::string name;
};

enum class Binop {
  // Conjunction of two formulas
  AND,
  // Disjunction of two formulas
  OR,
  // Value in Set
  IN,
};

enum class NaryOp {
  SET,
};

struct SetForm;
struct BinForm;
struct NaryForm;

using Form = std::variant<ByteSetForm, IntForm, VarForm, NaryForm, BinForm>;

struct NaryForm {
  NaryOp op;
  std::vector<std::shared_ptr<Form>> v;
};

struct BinForm {
  Binop op;
  std::shared_ptr<Form> lhs, rhs;
};

std::string ColorForm(const std::shared_ptr<Form> &form);

#endif
