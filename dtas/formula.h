#ifndef _FORMULA_H
#define _FORMULA_H

#include <string>
#include <variant>
#include <memory>
#include <cstdint>
#include <vector>

#include "byte-set.h"

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

  EQ,

  // Unsigned comparisons
  LESS,
  LESSEQ,
  GREATER,
  GREATEREQ,

  MULT,
};

enum class Unop {
  // Read RAM
  RAM,
  // Casts
  AS_INT,
  AS_WORD8,
  AS_WORD16,
  // Boolean negation
  NOT,
};

enum class Naryop {
  // Form a set of the arguments
  SET,
};

struct BoolForm {
  bool value = false;
};

struct SetForm;
struct BinForm;
struct NaryForm;
struct UnForm;

using Form = std::variant<ByteSetForm, IntForm, VarForm, NaryForm,
                          BinForm, UnForm, BoolForm>;

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

// Assuming the formula computes a boolean condition, simplify it.
// When we have comparisons like ram[x] < 200, put the literal on
// the right-hand side.
// The result should be taken as a conjunction.
std::vector<std::shared_ptr<Form>> SimplifyBoolFormula(
    std::shared_ptr<Form> form);

// Simplify a formula that produces a number.
std::shared_ptr<Form> SimplifyNumberFormula(
    const std::shared_ptr<Form> &form);

#endif
