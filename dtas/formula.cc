#include "formula.h"

#include <string>
#include <memory>
#include <cinttypes>
#include <variant>

#include "base/stringprintf.h"
#include "ansi.h"

#define AKEYWORD(s) AFGCOLOR(220, 220, 140, s)

std::string ColorForm(const std::shared_ptr<Form> &form) {
  // ByteSetForm, , VarForm, NaryForm, BinForm
  if (const IntForm *i = std::get_if<IntForm>(form.get())) {
    return StringPrintf("%" PRId64, i->value);
  } else if (const VarForm *v = std::get_if<VarForm>(form.get())) {
    return StringPrintf(AFGCOLOR(150, 170, 220, "%s"),
                        v->name.c_str());
  } else if (const NaryForm *n = std::get_if<NaryForm>(form.get())) {
    // TODO
    return "(nary)";
  } else if (const BinForm *b = std::get_if<BinForm>(form.get())) {
    std::string l = ColorForm(b->lhs);
    std::string r = ColorForm(b->rhs);
    switch (b->op) {
    case Binop::IN:
      return StringPrintf("%s " AKEYWORD("in") " %s",
                          l.c_str(), r.c_str());
    default:
      // TODO
      return "(bin)";
    }
  } else {
    return "?? BAD VARIANT ??";
  }
}
