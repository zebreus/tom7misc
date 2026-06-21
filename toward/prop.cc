
#include "prop.h"

#include <cstdint>
#include <format>
#include <functional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"


// TODO: Macros, simplifications, etc.

bool EvaluateProp(const World &world,
                  const std::vector<bool> &assignments,
                  const Prop &prop) {
  // This is just used for error messages, but something is wrong
  // if they're not the same size.
  CHECK(assignments.size() == world.symbol_names.size());

  std::function<bool(const Prop&)> EvalRec = [&](const Prop &p) -> bool {
      if (const Value *v = std::get_if<Value>(&p.p)) {
        return v->value;
      } else if (const Var *v = std::get_if<Var>(&p.p)) {
        CHECK(v->id >= 0 && v-> id < assignments.size());
        return assignments[v->id];
      } else if (const Unop *u = std::get_if<Unop>(&p.p)) {
        CHECK(u->op == UnopOp::NOT);
        return !EvalRec(*u->a);
      } else if (const Binop *b = std::get_if<Binop>(&p.p)) {
        bool lhs = EvalRec(*b->a);
        bool rhs = EvalRec(*b->b);
        switch (b->op) {
        case BinopOp::AND: return lhs && rhs;
        case BinopOp::OR: return lhs || rhs;
        case BinopOp::XOR: return lhs != rhs;
        default:
          LOG(FATAL) << "Unknown binop?";
        }
      } else {
        LOG(FATAL) << "Bad variant?";
      }
    };

  return EvalRec(prop);
}

size_t PropSize(const Prop &prop) {
  if (std::holds_alternative<Value>(prop.p)) {
    return 1;
  } else if (std::holds_alternative<Var>(prop.p)) {
    return 1;
  } else if (const Unop *u = std::get_if<Unop>(&prop.p)) {
    return 1 + PropSize(*u->a);
  } else if (const Binop *b = std::get_if<Binop>(&prop.p)) {
    size_t lhs = PropSize(*b->a);
    size_t rhs = PropSize(*b->b);
    return 1 + lhs + rhs;
  } else {
    LOG(FATAL) << "Bad variant?";
    return 0;
  }
}

static bool IsTrue(const Prop &prop) {
  if (const Value *v = std::get_if<Value>(&prop.p)) {
    return v->value;
  }

  return false;
}

static bool IsFalse(const Prop &prop) {
  if (const Value *v = std::get_if<Value>(&prop.p)) {
    return !v->value;
  }

  return false;
}

Prop SimplifyProp(const Prop &prop) {

  std::function<Prop(const Prop&)> SimpRec = [&](const Prop &p) -> Prop {
      if (std::holds_alternative<Value>(p.p) ||
          std::holds_alternative<Var>(p.p)) {
        return p;
      } else if (const Unop *u = std::get_if<Unop>(&p.p)) {
        CHECK(u->op == UnopOp::NOT);
        // TODO: Consider de Morgan rewrites?
        Prop a = SimpRec(*u->a);
        if (const Unop *uu = std::get_if<Unop>(&a.p)) {
          // ¬¬A -> A
          CHECK(uu->op == UnopOp::NOT);
          return *uu->a;
        } else if (IsTrue(a)) {
          return False();
        } else if (IsFalse(a)) {
          return True();
        }

        return -a;

      } else if (const Binop *b = std::get_if<Binop>(&p.p)) {
        Prop lhs = SimpRec(*b->a);
        Prop rhs = SimpRec(*b->b);
        switch (b->op) {
        case BinopOp::AND: {
          if (IsTrue(lhs))
            return rhs;
          if (IsTrue(rhs))
            return lhs;
          if (IsFalse(lhs) || IsFalse(rhs))
            return False();

          return lhs & rhs;
        }
        case BinopOp::OR: {
          if (IsTrue(lhs) || IsTrue(rhs))
            return True();
          if (IsFalse(lhs))
            return rhs;
          if (IsFalse(rhs))
            return lhs;

          return lhs | rhs;
        }

        case BinopOp::XOR: {
          if (IsFalse(lhs)) return rhs;
          if (IsFalse(rhs)) return lhs;
          if (IsTrue(lhs)) return -rhs;
          if (IsTrue(rhs)) return -lhs;

          // PERF: This will not completely simplify e.g. false ^ true.

          return lhs ^ rhs;
        }
        default:
          LOG(FATAL) << "Unknown binop?";
        }
      } else {
        LOG(FATAL) << "Bad variant?";
      }
    };

  return SimpRec(prop);
}

