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

enum class Unop {
  // Read RAM
  RAM,
};

enum class Naryop {
  // Form a set of the arguments
  SET,
};

struct SetForm;
struct BinForm;
struct NaryForm;
struct UnForm;

using Form = std::variant<ByteSetForm, IntForm, VarForm, NaryForm,
                          BinForm, UnForm>;

struct NaryForm {
  Naryop op;
  std::vector<std::shared_ptr<Form>> v;
};

struct BinForm {
  Binop op;
  std::shared_ptr<Form> lhs, rhs;
};

struct UnForm {
  Unop op;
  std::shared_ptr<Form> arg;
};

// Constraints

struct AlwaysConstraint {
  std::shared_ptr<Form> form;
};

using Constraint = std::variant<AlwaysConstraint>;

std::string ColorForm(const std::shared_ptr<Form> &form);
std::string ColorConstraint(const Constraint &c);

#endif
