
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
