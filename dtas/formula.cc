#include "formula.h"

#include <string>
#include <memory>
#include <cinttypes>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/stringprintf.h"
#include "util.h"

#define ACONSTRAINT(s) AFGCOLOR(240, 240, 120, s)
#define AKEYWORD(s) AFGCOLOR(220, 220, 140, s)

const char *BinopString(Binop op) {
  switch (op) {
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
  // ByteSetForm, , VarForm, NaryForm, BinForm
  if (const IntForm *i = std::get_if<IntForm>(form.get())) {
    return StringPrintf("%" PRId64, i->value);
  } else if (const VarForm *v = std::get_if<VarForm>(form.get())) {
    return StringPrintf(AFGCOLOR(150, 170, 220, "%s"),
                        v->name.c_str());
  } else if (const NaryForm *n = std::get_if<NaryForm>(form.get())) {
    std::vector<std::string> s;
    s.reserve(n->v.size());
    for (const auto &f : n->v) s.push_back(ColorForm(f));

    switch (n->op) {
    case Naryop::SET:
      return StringPrintf(AWHITE("{") "%s" AWHITE("}"),
                          Util::Join(s, ", ").c_str());
    default:
      // TODO
      return "(unknown n-ary op)";
    }

  } else if (const UnForm *u = std::get_if<UnForm>(form.get())) {
    std::string a = ColorForm(u->arg);
    switch (u->op) {
    case Unop::RAM:
      return StringPrintf(AKEYWORD("ram") AWHITE("[") "%s" AWHITE("]"),
                          a.c_str());
    default:
      return ARED("(unknown unop)");
    }

  } else if (const BinForm *b = std::get_if<BinForm>(form.get())) {
    std::string l = ColorForm(b->lhs);
    std::string r = ColorForm(b->rhs);

    return StringPrintf("%s " AKEYWORD("%s") " %s",
                        l.c_str(),
                        BinopString(b->op),
                        r.c_str());
  } else {
    return ARED("?? BAD VARIANT ??");
  }
}

std::string ColorConstraint(const Constraint &c) {
  if (const AlwaysConstraint *always = std::get_if<AlwaysConstraint>(&c)) {
    return StringPrintf(ACONSTRAINT("always") " %s",
                        ColorForm(always->form).c_str());
  } else if (const HereConstraint *here = std::get_if<HereConstraint>(&c)) {
    return StringPrintf(ACYAN("%04x") ": " ACONSTRAINT("here") " %s",
                        here->address,
                        ColorForm(here->form).c_str());
  } else {
    return ARED("?? BAD VARIANT ??");
  }
}
