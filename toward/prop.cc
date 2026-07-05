
#include "prop.h"

#include <compare>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <iterator>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "set-util.h"
#include "util.h"

// TODO: Macros, simplifications, etc.

bool EvaluateProp(const std::vector<bool> &assignments,
                  const Prop &prop) {
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

static std::pair<Prop, int> BalancePropInternal(const Prop &p);

static std::vector<std::pair<Prop, int>> GatherBin(const Binop *b) {
  BinopOp op = b->op;
  std::vector<std::pair<Prop, int>> flat;

  // Everything in here should be joined with op, but also
  // flattened if it is also an op.
  std::vector<Prop> todo = {*b->a, *b->b};

  while (!todo.empty()) {
    Prop p = std::move(todo.back());
    todo.pop_back();

    if (const Binop *child = std::get_if<Binop>(&p.p)) {
      if (child->op == op) {
        todo.push_back(*child->a);
        todo.push_back(*child->b);
      } else {
        flat.push_back(BalancePropInternal(p));
      }
    } else {
      flat.push_back(BalancePropInternal(p));
    }
  }

  return flat;
}

// Return the new prop and the depth of the subtree.
static std::pair<Prop, int> BalancePropInternal(const Prop &p) {
  if (std::holds_alternative<Value>(p.p) ||
      std::holds_alternative<Var>(p.p)) {
    return {p, 1};

  } else if (const Unop *u = std::get_if<Unop>(&p.p)) {
    CHECK(u->op == UnopOp::NOT);
    auto [a, d] = BalancePropInternal(*u->a);
    return {-std::move(a), d + 1};

  } else if (const Binop *b = std::get_if<Binop>(&p.p)) {
    // Gather into a flat vector. The subtrees have already
    // been balanced.
    std::vector<std::pair<Prop, int>> flat = GatherBin(b);

    auto Cmp = [](const std::pair<Prop, int> &x,
                  const std::pair<Prop, int> &y) {
      return x.second > y.second;
    };
    std::make_heap(flat.begin(), flat.end(), Cmp);

    while (flat.size() > 1) {
      std::pop_heap(flat.begin(), flat.end(), Cmp);
      std::pair<Prop, int> p1 = std::move(flat.back());
      flat.pop_back();

      std::pop_heap(flat.begin(), flat.end(), Cmp);
      std::pair<Prop, int> p2 = std::move(flat.back());
      flat.pop_back();

      Prop combined = Prop{
        .p = Binop{
          .op = b->op,
          .a = std::make_shared<Prop>(std::move(p1.first)),
          .b = std::make_shared<Prop>(std::move(p2.first)),
        }};
      int depth = std::max(p1.second, p2.second) + 1;

      flat.emplace_back(std::move(combined), depth);
      std::push_heap(flat.begin(), flat.end(), Cmp);
    }

    return std::move(flat.front());
  }

  LOG(FATAL) << "Bad variant?";
}

Prop BalanceProp(const Prop &prop) {
  return BalancePropInternal(prop).first;
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

std::strong_ordering operator<=>(const Prop &a, const Prop &b) {
  if (auto cmp = a.p.index() <=> b.p.index(); cmp != 0) {
    return cmp;
  }

  if (const Value *v_a = std::get_if<Value>(&a.p)) {
    const Value *v_b = std::get_if<Value>(&b.p);
    return v_a->value <=> v_b->value;
  } else if (const Var *v_a = std::get_if<Var>(&a.p)) {
    const Var *v_b = std::get_if<Var>(&b.p);
    return v_a->id <=> v_b->id;
  } else if (const Unop *u_a = std::get_if<Unop>(&a.p)) {
    const Unop *u_b = std::get_if<Unop>(&b.p);
    if (auto cmp = u_a->op <=> u_b->op; cmp != 0) {
      return cmp;
    }
    return *u_a->a <=> *u_b->a;
  } else if (const Binop *b_a = std::get_if<Binop>(&a.p)) {
    const Binop *b_b = std::get_if<Binop>(&b.p);
    if (auto cmp = b_a->op <=> b_b->op; cmp != 0) {
      return cmp;
    }
    if (auto cmp = *b_a->a <=> *b_b->a; cmp != 0) {
      return cmp;
    }
    return *b_a->b <=> *b_b->b;
  }

  return std::strong_ordering::equal;
}


// Return all the variable indices that appear in the proposition.
std::vector<int> PropVars(const Prop &a) {
  std::unordered_set<int> var_set;

  std::function<void(const Prop&)> GetRec = [&](const Prop &p) {
      if (std::holds_alternative<Value>(p.p)) {
        return;
      } else if (const Var *v = std::get_if<Var>(&p.p)) {
        var_set.insert(v->id);
      } else if (const Unop *u = std::get_if<Unop>(&p.p)) {
        GetRec(*u->a);
      } else if (const Binop *b = std::get_if<Binop>(&p.p)) {
        GetRec(*b->a);
        GetRec(*b->b);
      } else {
        LOG(FATAL) << "Bad variant?";
      }
    };

  GetRec(a);

  return SetToSortedVec(var_set);
}


bool PropEq(const Prop &a_in, const Prop &b_in) {
  const Prop a = SimplifyProp(a_in);
  const Prop b = SimplifyProp(b_in);

  std::vector<int> avars = PropVars(a);
  std::vector<int> bvars = PropVars(b);

  std::vector<int> allvars;
  // Must be at least this big. Typically we will have the same set of
  // vars.
  allvars.reserve(std::max(avars.size(), bvars.size()));

  std::set_union(avars.begin(), avars.end(),
                 bvars.begin(), bvars.end(),
                 std::back_inserter(allvars));

  int max_var = allvars.empty() ? -1 : allvars.back();

  CHECK(allvars.size() <= 32) << "This is exponential time!";
  uint64_t powset_size = uint64_t{1} << allvars.size();

  std::vector<bool> assignments(max_var + 1, false);
  for (uint64_t bitmask = 0; bitmask < powset_size; bitmask++) {
    for (int i = 0; i < allvars.size(); i++) {
      bool value = !!(bitmask & (uint64_t{1} << i));
      assignments[allvars[i]] = value;
    }
    if (EvaluateProp(assignments, a) != EvaluateProp(assignments, b)) {
      return false;
    }
  }

  // True for any possible assignment, so they are equivalent.
  return true;
}

// Or nullptr with no world.
static std::string PropAtom(const World *world,
                            const Prop &prop, int max_depth) {
  if (max_depth == 0) return "…";
  if (const Binop *b = std::get_if<Binop>(&prop.p)) {
    std::string lhs = PropAtom(world, *b->a, max_depth - 1);
    std::string rhs = PropAtom(world, *b->b, max_depth - 1);
    switch (b->op) {
    case BinopOp::AND: return std::format("({} ⋀ {})", lhs, rhs);
    case BinopOp::OR: return std::format("({} ⋁ {})", lhs, rhs);
    case BinopOp::XOR: return std::format("({} ⊕ {})", lhs, rhs);
    default:
      LOG(FATAL) << "Unknown binop?";
    }
  } else if (const Value *v = std::get_if<Value>(&prop.p)) {
    return v->value ? "⟙" : "⟘";
  } else if (const Var *v = std::get_if<Var>(&prop.p)) {
    if (world == nullptr) {
      return std::format("v{}", v->id);
    } else {
      if (v->id >= 0 && v->id < world->symbol_names.size()) {
        return world->symbol_names[v->id];
      } else {
        return std::format("UNKNOWN-VAR-v{}", v->id);
      }
    }
  } else if (const Unop *u = std::get_if<Unop>(&prop.p)) {
    CHECK(u->op == UnopOp::NOT);
    return std::format("¬{}", PropAtom(world, *u->a, max_depth - 1));
  } else {
    LOG(FATAL) << "Bad variant?";
  }
}

std::string PropStringInternal(const World *world,
                               const Prop &prop,
                               std::optional<int> max_depth) {
  int depth = max_depth.value_or(std::numeric_limits<int>::max());
  if (const Binop *b = std::get_if<Binop>(&prop.p)) {
    std::string lhs = PropAtom(world, *b->a, depth - 1);
    std::string rhs = PropAtom(world, *b->b, depth - 1);
    switch (b->op) {
    case BinopOp::AND: return std::format("{} ⋀ {}", lhs, rhs);
    case BinopOp::OR: return std::format("{} ⋁ {}", lhs, rhs);
    case BinopOp::XOR: return std::format("{} ⊕ {}", lhs, rhs);
    default:
      LOG(FATAL) << "Unknown binop?";
    }
  } else {
    return PropAtom(world, prop, depth);
  }
}


std::string PropString(const Prop &prop, std::optional<int> max_depth) {
  return PropStringInternal(nullptr, prop, max_depth);
}

std::string PropString(const World &world,
                       const Prop &prop,
                       std::optional<int> max_depth) {
  return PropStringInternal(&world, prop, max_depth);
}

// Negation but with peephole simplification.
static Prop N(const Prop &prop) {
  if (const Unop *u = std::get_if<Unop>(&prop.p)) {
    CHECK(u->op == UnopOp::NOT);
    return *u->a;
  } else if (const Value *v = std::get_if<Value>(&prop.p)) {
    return {Value{.value = !v->value}};
  } else {
    return -prop;
  }
}

// Normalize to only AND/NOT operators.
Prop NormalizeToAnd(const Prop &prop) {
  std::function<Prop(const Prop&)> NormRec = [&](const Prop &p) -> Prop {
      if (std::holds_alternative<Value>(p.p) ||
          std::holds_alternative<Var>(p.p)) {
        return p;
      } else if (const Unop *u = std::get_if<Unop>(&p.p)) {
        CHECK(u->op == UnopOp::NOT);
        return N(NormRec(*u->a));
      } else if (const Binop *b = std::get_if<Binop>(&p.p)) {
        Prop lhs = NormRec(*b->a);
        Prop rhs = NormRec(*b->b);
        switch (b->op) {
        case BinopOp::AND:
          return lhs & rhs;
        case BinopOp::OR:
          return -(N(lhs) & N(rhs));
        case BinopOp::XOR:
          // Note the proposition duplication! We should
          // probably add LET since we'll benefit from this
          // elsewhere.
          return -(N(lhs) & N(rhs)) & -(lhs & rhs);
        default:
          LOG(FATAL) << "Unknown binop?";
        }
      } else {
        LOG(FATAL) << "Bad variant?";
      }
    };

  return NormRec(prop);
}

Prop NormalizeRemoveXor(const Prop &prop) {
  std::function<Prop(const Prop&)> NormRec = [&](const Prop &p) -> Prop {
      if (std::holds_alternative<Value>(p.p) ||
          std::holds_alternative<Var>(p.p)) {
        return p;
      } else if (const Unop *u = std::get_if<Unop>(&p.p)) {
        CHECK(u->op == UnopOp::NOT);
        return N(NormRec(*u->a));
      } else if (const Binop *b = std::get_if<Binop>(&p.p)) {
        Prop lhs = NormRec(*b->a);
        Prop rhs = NormRec(*b->b);
        switch (b->op) {
        case BinopOp::AND:
          return lhs & rhs;
        case BinopOp::OR:
          return lhs | rhs;
        case BinopOp::XOR:
          // Note the proposition duplication! We should
          // probably add LET since we'll benefit from this
          // elsewhere.
          return (lhs | rhs) & -(lhs & rhs);
        default:
          LOG(FATAL) << "Unknown binop?";
        }
      } else {
        LOG(FATAL) << "Bad variant?";
      }
    };

  return NormRec(prop);
}

std::string SerializeProp(const Prop &prop) {
  if (const Value *v = std::get_if<Value>(&prop.p)) {
    return v->value ? "T" : "F";
  } else if (const Var *v = std::get_if<Var>(&prop.p)) {
    return Util::itos(v->id);
  } else if (const Unop *u = std::get_if<Unop>(&prop.p)) {
    CHECK(u->op == UnopOp::NOT);
    return "!" + SerializeProp(*u->a);
  } else if (const Binop *b = std::get_if<Binop>(&prop.p)) {
    std::string lhs = SerializeProp(*b->a);
    std::string rhs = SerializeProp(*b->b);
    char op = '?';
    switch (b->op) {
    case BinopOp::AND: op = '&'; break;
    case BinopOp::OR:op = '|'; break;
    case BinopOp::XOR: op = '^'; break;
    default:
      LOG(FATAL) << "Unknown binop?";
    }
    return std::format("({}{}{})", lhs, op, rhs);
  }
  LOG(FATAL) << "Bad variant?";
}

static std::optional<Prop> ParsePropRec(std::string_view *s);

static std::optional<Prop> ParseAtom(std::string_view *s) {
  if (s->empty()) return std::nullopt;
  if (Util::TryStripPrefix("T", s)) {
    return True();
  } else if (Util::TryStripPrefix("F", s)) {
    return False();
  } else if (Util::TryStripPrefix("!", s)) {
    auto a = ParseAtom(s);
    if (!a) return std::nullopt;
    return -std::move(*a);
  } else if (Util::TryStripPrefix("(", s)) {
    auto a = ParsePropRec(s);
    if (!a || !Util::TryStripPrefix(")", s)) return std::nullopt;
    return a;
  } else {
    // Must be digits then.
    std::string_view var_str = Util::ConsumePrefixMatching(
        [](char ch) { return ch >= '0' && ch <= '9'; }, s);
    if (var_str.empty()) return std::nullopt;
    std::optional<int64_t> id = Util::ParseInt64Opt(var_str);
    if (!id.has_value() || id.value() < 0) return std::nullopt;
    return Prop{.p = Var{.id = (int)id.value()}};
  }
  return std::nullopt;
}

static std::optional<Prop> ParseAnd(std::string_view *s) {
  auto a = ParseAtom(s);
  if (!a) return std::nullopt;
  while (Util::TryStripPrefix("&", s)) {
    auto b = ParseAtom(s);
    if (!b) return std::nullopt;
    a = std::move(*a) & std::move(*b);
  }
  return a;
}

static std::optional<Prop> ParseXor(std::string_view *s) {
  auto a = ParseAnd(s);
  if (!a) return std::nullopt;
  while (Util::TryStripPrefix("^", s)) {
    auto b = ParseAnd(s);
    if (!b) return std::nullopt;
    a = std::move(*a) ^ std::move(*b);
  }
  return a;
}

static std::optional<Prop> ParsePropRec(std::string_view *s) {
  auto a = ParseXor(s);
  if (!a) return std::nullopt;
  while (Util::TryStripPrefix("|", s)) {
    auto b = ParseXor(s);
    if (!b) return std::nullopt;
    a = std::move(*a) | std::move(*b);
  }
  return a;
}

std::optional<Prop> ParseProp(std::string_view s) {
  auto a = ParsePropRec(&s);
  // A valid parse must consume the entire string.
  if (!a || !s.empty()) return std::nullopt;
  return a;
}

