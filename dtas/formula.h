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

  // Unsigned comparisons
  LESS,
  LESSEQ,
  GREATER,
  GREATEREQ,

  EQ,
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
  Naryop op = Naryop::SET;
  std::vector<std::shared_ptr<Form>> v;
};

struct BinForm {
  Binop op = Binop::AND;
  std::shared_ptr<Form> lhs, rhs;
};

struct UnForm {
  Unop op = Unop::RAM;
  std::shared_ptr<Form> arg;
};

// Constraints

// An AlwaysConstraint is a formula that should always hold (from the
// initial condition onward). For example, a memory location may take
// on only some specific set of values.
struct AlwaysConstraint {
  std::shared_ptr<Form> form;
};

// A HereConstraint is a formula that should hold at the program point
// (when the PC has the given value; syntactically this is after the
// previous instruction executes, and before the one below does).
struct HereConstraint {
  uint16_t address = 0;
  std::shared_ptr<Form> form;
};

using Constraint = std::variant<AlwaysConstraint, HereConstraint>;

const char *BinopString(Binop op);
std::string ColorForm(const std::shared_ptr<Form> &form);
std::string ColorConstraint(const Constraint &c);

#endif
