
#ifndef _TOWARD_PROP_H
#define _TOWARD_PROP_H

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <compare>

struct World {
  // Just for input/output. A variable is uniquely
  // identified by its index.
  std::vector<std::string> symbol_names;
};

struct Prop;

struct Var {
  int id = 0;
};

struct Value {
  bool value = false;
};

enum class BinopOp {
  AND,
  OR,
  XOR,
};

enum class UnopOp {
  NOT,
};

struct Binop {
  BinopOp op = BinopOp::AND;
  std::shared_ptr<Prop> a, b;
};

struct Unop {
  UnopOp op = UnopOp::NOT;
  std::shared_ptr<Prop> a;
};

// This is recursive, so we need a wrapper struct for the forward
// declaration.
struct Prop {
  std::variant<Value, Var, Binop, Unop> p;
};

inline Prop False() { return {.p = Value{.value = false}}; }
inline Prop True() { return {.p = Value{.value = true}}; }

inline Prop operator |(Prop a, Prop b) {
  return {.p = Binop{
      .op = BinopOp::OR,
      .a = std::make_shared<Prop>(std::move(a)),
      .b = std::make_shared<Prop>(std::move(b)),
    }};
}

inline Prop operator &(Prop a, Prop b) {
  return {.p = Binop{
      .op = BinopOp::AND,
      .a = std::make_shared<Prop>(std::move(a)),
      .b = std::make_shared<Prop>(std::move(b)),
    }};
}

inline Prop operator ^(Prop a, Prop b) {
  return {.p = Binop{
      .op = BinopOp::XOR,
      .a = std::make_shared<Prop>(std::move(a)),
      .b = std::make_shared<Prop>(std::move(b)),
    }};
}

inline Prop operator -(Prop a) {
  return {.p = Unop{
      .op = UnopOp::NOT,
      .a = std::make_shared<Prop>(std::move(a)),
    }};
}

inline Prop Or() {
  return False();
}

template <typename... Args>
inline Prop Or(Prop first, Args... args) {
  return (std::move(first) | ... | std::move(args));
}

inline Prop And() {
  return True();
}

template <typename... Args>
inline Prop And(Prop first, Args... args) {
  return (std::move(first) & ... & std::move(args));
}

// Just using integers for variables.
std::string PropString(const Prop &prop);

size_t PropSize(const Prop &prop);

bool EvaluateProp(const std::vector<bool> &assignments,
                  const Prop &prop);

// Arbitrary total order on props (structural).
std::strong_ordering operator<=>(const Prop &a, const Prop &b);

inline bool operator==(const Prop &a, const Prop &b) {
  return (a <=> b) == std::strong_ordering::equal;
}

// Conservative simplifications, especially with constant
// values.
Prop SimplifyProp(const Prop &prop);

// Return all the distinct variable indices that appear in the
// proposition in ascending order.
std::vector<int> PropVars(const Prop &a);

// Used in tests. Semantic equality of propositions in the same world.
// Computed by bit-blasting free variables. Not fast.
bool PropEq(const Prop &a, const Prop &b);

// Normalize to only AND/NOT operators.
Prop NormalizeToAnd(const Prop &prop);

#endif
