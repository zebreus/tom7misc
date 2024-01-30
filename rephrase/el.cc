
#include "el.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"
#include "bignum/big.h"

namespace el {

std::string TypeString(const Type *t) {
  switch (t->type) {
  case TypeType::VAR:
    switch (t->children.size()) {
    case 0:
      return t->var;
    case 1:
      return StringPrintf("%s %s",
                          TypeString(t->children[0]).c_str(),
                          t->var.c_str());
    default: {
      std::string args;
      for (int i = 0; i < (int)t->children.size(); i++) {
        if (i != 0) StringAppendF(&args, ", ");
        StringAppendF(&args, "%s", TypeString(t->children[i]).c_str());
      }
      return StringPrintf("(%s) %s", args.c_str(), t->var.c_str());
    }
  }

  case TypeType::ARROW:
    return StringPrintf("(%s -> %s)",
                        TypeString(t->a).c_str(),
                        TypeString(t->b).c_str());

  case TypeType::PRODUCT: {
    std::string ret = "(";
    for (int i = 0; i < (int)t->children.size(); i++) {
      const Type *child = t->children[i];
      if (i != 0)
        StringAppendF(&ret, " * ");
      StringAppendF(&ret, "%s", TypeString(child).c_str());
    }
    ret.push_back(')');
    return ret;
  }

  case TypeType::RECORD: {
    std::string ret = "{";
    for (int i = 0; i < (int)t->str_children.size(); i++) {
      const auto &child = t->str_children[i];
      if (i != 0)
        StringAppendF(&ret, ", ");
      StringAppendF(&ret, "%s: %s",
                    child.first.c_str(),
                    TypeString(child.second).c_str());
    }
    ret.push_back('}');
    return ret;
  }
  default:
    return "unknown type type??";
  }
}

std::string LayoutString(const Layout *lay) {
  switch (lay->type) {
    case LayoutType::TEXT:
      return lay->str;
    default:
      return "TODO LAYOUT TYPE";
  }
}

std::string PatString(const Pat *p) {
  switch (p->type) {
  case PatType::WILD: return "_";
  case PatType::VAR: return p->var;
  case PatType::TUPLE: {
    std::string ret = "(";
    for (int i = 0; i < (int)p->children.size(); i++) {
      const Pat *child = p->children[i];
      if (i != 0)
        StringAppendF(&ret, ", ");
      StringAppendF(&ret, "%s", PatString(child).c_str());
    }
    ret.push_back(')');
    return ret;
  }
  default:
    return "unknown pat type??";
  }
}

std::string DecString(const Dec *d) {
  switch (d->type) {
  case DecType::VAL:
    return StringPrintf("val %s = %s",
                        PatString(d->pat).c_str(),
                        ExpString(d->exp).c_str());
  case DecType::FUN:
    return StringPrintf("fun %s %s = %s",
                        d->str.c_str(),
                        PatString(d->pat).c_str(),
                        ExpString(d->exp).c_str());
  default:
    return "TODO DECTYPE";
  }
}

// TODO: Make destructuring bind for each expression type,
// so you can do like
//    const auto &[code, t, f] = exp->If();
// ... which also lets us use better representations internally.
// TODO: Some kind of pretty-printing
std::string ExpString(const Exp *e) {
  switch (e->type) {
  case ExpType::STRING:
    // XXX escaping
    return StringPrintf("\"%s\"", e->str.c_str());

  case ExpType::VAR:
    return e->str;

  case ExpType::INTEGER:
    return e->integer.ToString();

  case ExpType::TUPLE: {
    std::string ret = "(";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      ret += ExpString(e->children[i]);
    }
    ret += ")";
    return ret;
  }

  case ExpType::JOIN: {
    std::string ret = "[";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      ret += ExpString(e->children[i]);
    }
    ret += "]";
    return ret;
  }

  case ExpType::LET: {
    std::vector<std::string> decs;
    for (const Dec *d : e->decs) {
      decs.push_back(DecString(d));
    }

    return StringPrintf("let %s in %s end",
                        Util::Join(decs, " ").c_str(),
                        ExpString(e->a).c_str());
  }

  case ExpType::IF: {
    return StringPrintf("(if %s then %s else %s)",
                        ExpString(e->a).c_str(),
                        ExpString(e->b).c_str(),
                        ExpString(e->c).c_str());
  }

  case ExpType::APP: {
    return StringPrintf("(%s %s)",
                        ExpString(e->a).c_str(),
                        ExpString(e->b).c_str());
  }

  case ExpType::FN: {
    const std::string as =
      e->str.empty() ? "" : StringPrintf(" as %s", e->str.c_str());
    return StringPrintf("(fn%s %s => %s)",
                        as.c_str(),
                        PatString(e->pat).c_str(),
                        ExpString(e->a).c_str());
  }

  case ExpType::LAYOUT:
    return StringPrintf("[%s]", LayoutString(e->layout).c_str());

  case ExpType::ANN:
    return StringPrintf("(%s : %s)",
                        ExpString(e->a).c_str(),
                        TypeString(e->t).c_str());
  default:
    return "ILLEGAL EXPRESSION";
  }
}


std::vector<const Layout *> FlattenLayout(const Layout *lay) {
  switch (lay->type) {
  case LayoutType::TEXT:
    if (lay->str.empty()) return {};
    else return {lay};

  case LayoutType::JOIN: {
    std::vector<const Layout *> ret;
    ret.reserve(lay->children.size());
    for (const Layout *child : lay->children) {
      for (const Layout *f : FlattenLayout(child)) {
        ret.push_back(f);
      }
    }
    return ret;
  }

  case LayoutType::EXP:
    return {lay};

  default:
    CHECK(false) << "Unimplemented layout type.";
    return {};
  }
}

}  // namespace el
