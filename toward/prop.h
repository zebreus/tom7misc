
#ifndef _TOWARD_PROP_H
#define _TOWARD_PROP_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <compare>
#include <cstddef>
#include <functional>

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
  NAND,
  NOR,
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

inline Prop Nand(Prop a, Prop b) {
  return {.p = Binop{
      .op = BinopOp::NAND,
      .a = std::make_shared<Prop>(std::move(a)),
      .b = std::make_shared<Prop>(std::move(b)),
    }};
}

inline Prop Nor(Prop a, Prop b) {
  return {.p = Binop{
      .op = BinopOp::NOR,
      .a = std::make_shared<Prop>(std::move(a)),
      .b = std::make_shared<Prop>(std::move(b)),
    }};
}


// Just using integers for variables.
std::string PropString(const Prop &prop, std::optional<int> max_depth = {});
std::string PropString(const World &world, const Prop &prop,
                       std::optional<int> max_depth = {});

// Counting the number of nodes in the tree.
size_t PropSize(const Prop &prop);

// Counting the number of distinct nodes in the tree.
size_t SharedPropSize(const Prop &prop);

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

// Rewrite n-ary chains of AND/OR into equivalent expressions that
// are more balanced (in terms of height of the subtrees).
Prop BalanceProp(const Prop &prop);

// Return all the distinct variable indices that appear in the
// proposition in ascending order.
std::vector<int> PropVars(const Prop &a);

// Used in tests. Semantic equality of propositions in the same world.
// Computed by bit-blasting free variables. Not fast.
bool PropEq(const Prop &a, const Prop &b);

// Normalize to only AND/NOT operators.
Prop NormalizeToAnd(const Prop &prop);

// Normalize to AND/OR/NOT.
Prop NormalizeRemoveXor(const Prop &prop);

// Can assume no spaces or newlines in serialized propositions.
std::string SerializeProp(const Prop &prop);
std::optional<Prop> ParseProp(std::string_view s);

// Give arbitrary (new) names to the variables that occur in the
// proposition, for situations where we require a world but don't have
// one.
void NameVars(World *world, const Prop &prop);

// Overloads for std::unordered_set, etc.
namespace std {
template <>
struct hash<Prop> {
  size_t operator()(const Prop &prop) const;
};
}

#endif
