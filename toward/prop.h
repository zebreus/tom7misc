
#ifndef _TOWARD_PROP_H
#define _TOWARD_PROP_H

#include <utility>
#include <variant>
#include <vector>
#include <string>
#include <memory>

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

size_t PropSize(const Prop &prop);

bool EvaluateProp(const World &world,
                  const std::vector<bool> &assignments,
                  const Prop &prop);

// Conservative simplifications, especially with constant
// values.
Prop SimplifyProp(const Prop &prop);

#endif
