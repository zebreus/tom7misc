
#include "el.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"
#include "bignum/big.h"
#include "pp.h"

namespace el {

std::string TypeString(const Type *t) {
  if (t == nullptr) return "NULL!?";
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

const char *LayoutTypeString(LayoutType lt) {
  switch (lt) {
  case LayoutType::EXP: return "EXP";
  case LayoutType::TEXT: return "TEXT";
  case LayoutType::JOIN: return "JOIN";
  default: return "??LAYOUT TYPE??";
  }
}

std::string LayoutString(const Layout *lay) {
  if (lay == nullptr) return "NULL!?";
  switch (lay->type) {
    case LayoutType::TEXT:
      return lay->str;
  case LayoutType::EXP:
    return StringPrintf("[%s]", ExpString(lay->exp).c_str());
  case LayoutType::JOIN: {
    std::vector<std::string> body;
    for (const Layout *child : lay->children) {
      body.push_back(LayoutString(child));
    }
    return StringPrintf("JOIN[%s]", Util::Join(body, ",").c_str());
  }
  default:
      return "TODO LAYOUT TYPE";
  }
}

const char *PatTypeString(PatType pt) {
  switch (pt) {
  case PatType::VAR: return "VAR";
  case PatType::WILD: return "WILD";
  case PatType::TUPLE: return "TUPLE";
  case PatType::RECORD: return "RECORD";
  case PatType::OBJECT: return "OBJECT";
  case PatType::ANN: return "ANN";
  case PatType::AS: return "AS";
  case PatType::INT: return "INT";
  case PatType::BOOL: return "BOOL";
  case PatType::STRING: return "STRING";
  case PatType::APP: return "APP";
  }
}


std::string PatString(const Pat *p) {
  if (p == nullptr) return "NULL!?";
  switch (p->type) {
  case PatType::STRING: return EscapeString(p->str);
  case PatType::INT: return p->integer.ToString();
  case PatType::BOOL: return p->boolean ? "true" : "false";
  case PatType::WILD: return "_";
  case PatType::VAR: return p->str;
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

  case PatType::RECORD: {
    std::string ret = "{";
    for (int i = 0; i < (int)p->str_children.size(); i++) {
      const auto [lab, child] = p->str_children[i];
      if (i != 0)
        StringAppendF(&ret, ", ");
      StringAppendF(&ret, "%s = %s",
                    lab.c_str(),
                    PatString(child).c_str());
    }
    ret.push_back('}');
    return ret;
  }

  case PatType::OBJECT: {
    std::string ret = StringPrintf("{(%s) ", p->str.c_str());
    for (int i = 0; i < (int)p->str_children.size(); i++) {
      const auto [lab, child] = p->str_children[i];
      if (i != 0)
        StringAppendF(&ret, ", ");
      StringAppendF(&ret, "%s = %s",
                    lab.c_str(),
                    PatString(child).c_str());
    }
    ret.push_back('}');
    return ret;
  }

  case PatType::ANN: {
    return StringPrintf("%s : %s",
                        PatString(p->a).c_str(),
                        TypeString(p->ann).c_str());
  }
  case PatType::AS: {
    return StringPrintf("%s as %s",
                        PatString(p->a).c_str(),
                        PatString(p->b).c_str());
  }

  case PatType::APP: {
    return StringPrintf("%s %s",
                        p->str.c_str(),
                        PatString(p->a).c_str());
  }

  default:
    return "unknown pat type??";
  }
}

std::string DecString(const Dec *d) {
  if (d == nullptr) return "NULL!?";
  switch (d->type) {
  case DecType::VAL:
    return StringPrintf("val %s = %s",
                        PatString(d->pat).c_str(),
                        ExpString(d->exp).c_str());

  case DecType::FUN: {
    std::string ret;
    for (int i = 0; i < (int)d->funs.size(); i++) {
      const FunDec &fd = d->funs[i];
      StringAppendF(&ret, "%s ", i == 0 ? "fun" : "and");
      bool first = true;
      for (const auto &[cpat, cexp] : fd.clauses) {
        if (!first) {
          StringAppendF(&ret, " | ");
        }
        StringAppendF(&ret, "%s",
                      fd.name.c_str());
        for (const Pat *p : cpat) {
          StringAppendF(&ret, " (%s)", PatString(p).c_str());
        }
        StringAppendF(&ret, " = %s\n",
                      ExpString(cexp).c_str());
        first = false;
      }
    }
    return ret;
  }

  case DecType::DATATYPE: {
    std::string tyvars;
    if (!d->tyvars.empty()) {
      tyvars = "(" + Util::Join(d->tyvars, ",") + ") ";
    }
    std::string ret = StringPrintf("datatype %s", tyvars.c_str());
    for (int i = 0; i < (int)d->datatypes.size(); i++) {
      const DatatypeDec &dd = d->datatypes[i];
      if (i != 0) StringAppendF(&ret, "\nand");
      StringAppendF(&ret, " %s =\n", dd.name.c_str());
      for (int j = 0; j < (int)dd.arms.size(); j++) {
        const auto &arm = dd.arms[j];
        if (j == 0) StringAppendF(&ret, "\n    ");
        else StringAppendF(&ret, "\n  | ");
        StringAppendF(&ret, "%s", arm.first.c_str());
        if (arm.second != nullptr){
          StringAppendF(&ret, " of %s", TypeString(arm.second).c_str());
        }
      }
    }
    return ret;
  }

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
  if (e == nullptr) return "NULL!?";
  switch (e->type) {
  case ExpType::STRING:
    return StringPrintf("\"%s\"", EscapeString(e->str).c_str());

  case ExpType::VAR:
    return e->str;

  case ExpType::INT:
    return e->integer.ToString();

  case ExpType::BOOL:
    return e->boolean ? "true" : "false";

  case ExpType::FLOAT:
    return StringPrintf("%.17g", e->d);

  case ExpType::ANDALSO:
    return StringPrintf("(%s) andalso (%s)",
                        ExpString(e->a).c_str(),
                        ExpString(e->b).c_str());
  case ExpType::ORELSE:
    return StringPrintf("(%s) orelse (%s)",
                        ExpString(e->a).c_str(),
                        ExpString(e->b).c_str());

  case ExpType::TUPLE: {
    std::string ret = "(";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      ret += ExpString(e->children[i]);
    }
    ret += ")";
    return ret;
  }

  case ExpType::RECORD: {
    std::string ret = "{";
    for (int i = 0; i < (int)e->str_children.size(); i++) {
      const auto &[lab, child] = e->str_children[i];
      if (i != 0) StringAppendF(&ret, ", ");
      StringAppendF(&ret, "%s = %s",
                    lab.c_str(), ExpString(child).c_str());
    }
    ret += "}";
    return ret;
  }

  case ExpType::OBJECT: {
    std::string ret =
      StringPrintf("{(%s) ", e->str.c_str());
    for (int i = 0; i < (int)e->str_children.size(); i++) {
      const auto &[lab, child] = e->str_children[i];
      if (i != 0) StringAppendF(&ret, ", ");
      StringAppendF(&ret, "%s = %s",
                    lab.c_str(), ExpString(child).c_str());
    }
    ret += "}";
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
    std::string ret = StringPrintf("(fn%s ", as.c_str());
    bool first = true;
    for (const auto &[pat, body] : e->clauses) {
      if (!first) StringAppendF(&ret, "\n | ");
      StringAppendF(&ret, "%s => %s",
                    PatString(pat).c_str(),
                    ExpString(body).c_str());
      first = false;
    }
    return ret + ")";
  }

  case ExpType::CASE: {
    std::vector<std::string> arms;
    arms.reserve(e->clauses.size());
    for (const auto &[pat, exp] : e->clauses) {
      arms.push_back(StringPrintf("%s => %s",
                                  PatString(pat).c_str(),
                                  ExpString(exp).c_str()));
    }
    return StringPrintf("(case %s of\n"
                        "   %s)",
                        ExpString(e->a).c_str(),
                        Util::Join(arms, "\n | ").c_str());
  }

  case ExpType::FAIL:
    return StringPrintf("(fail %s)", ExpString(e->a).c_str());

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

bool IsLayoutValuable(const Layout *lay) {
  switch (lay->type) {
  case LayoutType::TEXT:
    return true;

  case LayoutType::JOIN:
    for (const Layout *child : lay->children)
      if (!IsLayoutValuable(child))
        return false;
    return true;

  case LayoutType::EXP:
    return IsValuable(lay->exp);

  default:
    CHECK(false) << "Unimplemented layout type.";
    return false;
  }
}

bool IsValuable(const Exp *e) {
  switch (e->type) {
  case ExpType::STRING: return true;
  case ExpType::VAR: return true;
  case ExpType::INT: return true;
  case ExpType::BOOL: return true;

  case ExpType::TUPLE:
    for (const Exp *child : e->children)
      if (!IsValuable(child))
        return false;
    return true;

  case ExpType::JOIN:
    for (const Exp *child : e->children)
      if (!IsValuable(child))
        return false;
    return true;

  case ExpType::LET:
    return false;

  case ExpType::IF:
    return false;

  case ExpType::APP:
    // TODO: Could allow constructor application if
    // we could determine it at parse time!
    return false;

  case ExpType::FN:
    return true;

  case ExpType::LAYOUT:
    return IsLayoutValuable(e->layout);

  case ExpType::ANN:
    return true;

  default:
    return "ILLEGAL EXPRESSION";
  }
}

std::string AstPool::NewInternalVar(const std::string &hint) {
  next_internal_var++;
  // Need something that's different from the IL's reserved symbol ($)
  // -- or else we would need to coordinate the counter with IL --
  // and can't be written by the user.
  return StringPrintf("%s#%d",
                      hint.empty() ? "internal" : hint.c_str(),
                      next_internal_var);
}

}  // namespace el
