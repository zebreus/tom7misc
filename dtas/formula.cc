#include "formula.h"

#include <format>
#include <string>
#include <memory>
#include <cinttypes>
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
