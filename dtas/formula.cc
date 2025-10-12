#include "formula.h"

#include <format>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "util.h"

#define ACONSTRAINT(s) AFGCOLOR(240, 240, 120, s)
#define AKEYWORD(s) AFGCOLOR(220, 220, 140, s)

const char *BinopString(Binop op) {
  switch (op) {
    case Binop::AND: return "&&";
    case Binop::OR: return "||";
    case Binop::IN: return "in";
    case Binop::LESS: return "<";
    case Binop::LESSEQ: return "<=";
    case Binop::GREATER: return ">";
    case Binop::GREATEREQ: return ">=";
    case Binop::EQ: return "=";
    default: return "???";
  }
}


std::string ColorForm(const std::shared_ptr<Form> &form) {
  // ByteSetForm, VarForm, NaryForm, BinForm
  if (const IntForm *i = std::get_if<IntForm>(form.get())) {
    return std::format("{}", i->value);
  } else if (const BoolForm *b = std::get_if<BoolForm>(form.get())) {
    return std::format("{}", b->value ? AFGCOLOR(200, 220, 200, "true") :
                       AFGCOLOR(220, 200, 200, "false"));
  } else if (const VarForm *v = std::get_if<VarForm>(form.get())) {
    return std::format(AFGCOLOR(150, 170, 220, "{}"), v->name);
  } else if (const NaryForm *n = std::get_if<NaryForm>(form.get())) {
    std::vector<std::string> s;
    s.reserve(n->v.size());
    for (const auto &f : n->v) s.push_back(ColorForm(f));

    switch (n->op) {
    case Naryop::SET:
      return std::format(AWHITE("{{") "{}" AWHITE("}}"),
                          Util::Join(s, ", "));
    default:
      // TODO
      return "(unknown n-ary op)";
    }

  } else if (const UnForm *u = std::get_if<UnForm>(form.get())) {
    std::string a = ColorForm(u->arg);
    switch (u->op) {
    case Unop::RAM:
      return std::format(AKEYWORD("ram") AWHITE("[") "{}" AWHITE("]"), a);
    case Unop::AS_INT:
      return std::format(AKEYWORD("(int)") AWHITE("(") "{}" AWHITE(")"), a);
    case Unop::AS_WORD8:
      return std::format(AKEYWORD("(u8)") AWHITE("(") "{}" AWHITE(")"), a);
    case Unop::AS_WORD16:
      return std::format(AKEYWORD("(u16)") AWHITE("(") "{}" AWHITE(")"), a);
    default:
      return ARED("(unknown unop)");
    }

  } else if (const BinForm *b = std::get_if<BinForm>(form.get())) {
    std::string l = ColorForm(b->lhs);
    std::string r = ColorForm(b->rhs);

    return std::format("{} " AKEYWORD("{}") " {}",
                        l,
                        BinopString(b->op),
                        r);
  } else if (const ByteSetForm *s = std::get_if<ByteSetForm>(form.get())) {
    (void)s;
    return std::format(AWHITE("{{") " "
                       ARED("unimplemented: byteset")
                       " " AWHITE("}}"));
  } else {
    return ARED("?? BAD VARIANT ??");
  }
}

std::string ColorConstraint(const Constraint &c) {
  if (const AlwaysConstraint *always = std::get_if<AlwaysConstraint>(&c)) {
    return std::format(ACONSTRAINT("always") " {}",
                       ColorForm(always->form));
  } else if (const HereConstraint *here = std::get_if<HereConstraint>(&c)) {
    return std::format(ACYAN("{:04x}") ": " ACONSTRAINT("here") " {}",
                       here->address,
                       ColorForm(here->form));
  } else {
    return ARED("?? BAD VARIANT ??");
  }
}

static std::shared_ptr<Form> BoolConst(bool b) {
  return std::make_shared<Form>(BoolForm{
        .value = b,
    });
}

static std::shared_ptr<Form> NaryAnd(
    const std::vector<std::shared_ptr<Form>> &args) {
  if (args.empty()) {
    return BoolConst(true);
  } else {
    std::shared_ptr<Form> form = args[0];
    for (int i = 1 ; i < args.size(); i++) {
      form = std::make_shared<Form>(BinForm{
          .op = Binop::AND,
          .lhs = std::move(form),
          .rhs = args[i],
        });
    }
    return form;
  }
}

std::shared_ptr<Form> SimplifyNumberFormula(
    const std::shared_ptr<Form> &form) {
  return form;
}

static Binop ReverseBinop(Binop bop) {
  switch (bop) {
  case Binop::EQ: return Binop::EQ;
  case Binop::LESS: return Binop::GREATER;
  case Binop::LESSEQ: return Binop::GREATEREQ;
  case Binop::GREATER: return Binop::LESS;
  case Binop::GREATEREQ: return Binop::LESSEQ;
  case Binop::AND: return Binop::AND;
  case Binop::OR: return Binop::OR;
  case Binop::MULT: return Binop::MULT;
  case Binop::IN:
    LOG(FATAL) << "Can't swap because its types are not symmetric: " <<
      BinopString(bop);
  }
}

static bool IsLiteral(const std::shared_ptr<Form> &form) {
  if (nullptr != std::get_if<BoolForm>(form.get())) {
    return true;
  } else if (nullptr != std::get_if<IntForm>(form.get())) {
    return true;
  } else {
    return false;
  }
}

// TODO: Simplify other types of formulas.

std::vector<std::shared_ptr<Form>> SimplifyBoolFormula(
    std::shared_ptr<Form> form) {

  std::vector<std::shared_ptr<Form>> conj;

  if (const BoolForm *bf = std::get_if<BoolForm>(form.get())) {
    if (bf->value) {
      // Empty conjunction is true.
      return {};
    } else {
      conj.push_back(form);
    }

  } else if (const BinForm *bf = std::get_if<BinForm>(form.get())) {
    if (bf->op == Binop::AND) {
      // Flatten.
      std::vector<std::shared_ptr<Form>> lhs =
        SimplifyBoolFormula(bf->lhs);
      std::vector<std::shared_ptr<Form>> rhs =
        SimplifyBoolFormula(bf->rhs);

      // If we have a constant false, then the whole thing is
      // false.
      bool is_false = false;
      auto One = [&conj, &is_false](auto f) {
          if (const BoolForm *bf = std::get_if<BoolForm>(f.get());
              !bf->value) {
            is_false = true;
          }
          conj.emplace_back(std::move(f));
        };

      for (auto &f : lhs) One(std::move(f));
      for (auto &f : rhs) One(std::move(f));

      if (is_false) {
        return {BoolConst(false)};
      }

    } else if (bf->op == Binop::OR) {
      CHECK(bf->op == Binop::OR);

      // TODO: Can push NOT down and just get a conjunctive
      // normal form?
      std::vector<std::shared_ptr<Form>> lhs =
        SimplifyBoolFormula(bf->lhs);
      std::vector<std::shared_ptr<Form>> rhs =
        SimplifyBoolFormula(bf->rhs);

      // if we have constant true, it's degenerate
      if (lhs.empty()) return lhs;
      if (rhs.empty()) return rhs;

      // TODO: remove constant false

      // now we have (l1 & l2 & ... & ln) || (r1 & r2 & ... & rm)

      // TODO: Check for stuff like addr = 1 || addr = 2 and transform
      // into set tests.
      conj.push_back(std::make_shared<Form>(BinForm{
            .op = Binop::OR,
            .lhs = NaryAnd(lhs),
            .rhs = NaryAnd(rhs),
          }));

    } else if (bf->op == Binop::IN) {
      // TODO: Simplify the byteset.
      // TODO: Could check if we have an empty or universal set.
      conj.push_back(form);

    } else if (bf->op == Binop::EQ ||
               bf->op == Binop::LESS ||
               bf->op == Binop::LESSEQ ||
               bf->op == Binop::GREATER ||
               bf->op == Binop::GREATEREQ) {

      auto lhs = SimplifyNumberFormula(bf->lhs);
      auto rhs = SimplifyNumberFormula(bf->rhs);

      // TODO: Simplify both sides. Comparisons against constants can
      // be sets.

      if (IsLiteral(lhs)) {
        if (IsLiteral(rhs)) {
          // TODO: If we have two literals, simplify it here!
          conj.push_back(std::make_shared<Form>(BinForm{
                .op = bf->op,
                .lhs = std::move(lhs),
                .rhs = std::move(rhs),
              }));
        } else {
          // Swap 'em.
          conj.push_back(std::make_shared<Form>(BinForm{
                .op = ReverseBinop(bf->op),
                .lhs = std::move(rhs),
                .rhs = std::move(lhs),
              }));
        }
      } else {
        // RHS might already be literal; we leave it there either way.
        conj.push_back(std::make_shared<Form>(BinForm{
              .op = bf->op,
              .lhs = std::move(lhs),
              .rhs = std::move(rhs),
            }));
      }
    }

  } else {
    LOG(FATAL) << "Expected a formula that computes a bool, but got: "
               << ColorForm(form);
  }

  return conj;
}

