
#include "el.h"

#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"
#include "bignum/big.h"
#include "pp.h"
#include "ansi.h"

namespace el {

std::string TypeString(const Type *t) {
  if (t == nullptr) return "NULL!?";
  switch (t->type) {
  case TypeType::VAR:
    switch (t->children.size()) {
    case 0:
      return t->var;
    case 1:
      return std::format("{} {}",
                         TypeString(t->children[0]),
                         t->var);
    default: {
      std::string args;
      for (int i = 0; i < (int)t->children.size(); i++) {
        if (i != 0) args += ", ";
        AppendFormat(&args, "{}", TypeString(t->children[i]));
      }
      return std::format("({}) {}", args, t->var);
    }
  }

  case TypeType::ARROW:
    return std::format("({} -> {})",
                       TypeString(t->a),
                       TypeString(t->b));

  case TypeType::PRODUCT: {
    std::string ret = "(";
    for (int i = 0; i < (int)t->children.size(); i++) {
      const Type *child = t->children[i];
      if (i != 0)
        ret.append(" * ");
      ret.append(TypeString(child));
    }
    ret.push_back(')');
    return ret;
  }

  case TypeType::RECORD: {
    std::string ret = "{";
    for (int i = 0; i < (int)t->str_children.size(); i++) {
      const auto &child = t->str_children[i];
      if (i != 0)
        ret.append(", ");
      AppendFormat(&ret, "{}: {}",
                   child.first,
                   TypeString(child.second));
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
  }
  return "??LAYOUT TYPE??";
}

std::string LayoutString(const Layout *lay) {
  if (lay == nullptr) return "NULL!?";
  switch (lay->type) {
    case LayoutType::TEXT:
      return lay->str;
  case LayoutType::EXP:
    return std::format("[{}]", ExpString(lay->exp));
  case LayoutType::JOIN: {
    std::vector<std::string> body;
    for (const Layout *child : lay->children) {
      body.push_back(LayoutString(child));
    }
    return std::format("JOIN[{}]", Util::Join(body, ","));
  }
  }
  return "??LAYOUT??";
}

const char *TypeTypeString(TypeType tt) {
  switch (tt) {
  case TypeType::VAR: return "VAR";
  case TypeType::ARROW: return "ARROW";
  case TypeType::PRODUCT: return "PRODUCT";
  case TypeType::RECORD: return "RECORD";
  }
  return "invalid typetype";
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
  return "invalid pattype";
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
        ret.append(", ");
      ret.append(PatString(child));
    }
    ret.push_back(')');
    return ret;
  }

  case PatType::RECORD: {
    std::string ret = "{";
    for (int i = 0; i < (int)p->str_children.size(); i++) {
      const auto [lab, child] = p->str_children[i];
      if (i != 0)
        ret.append(", ");
      AppendFormat(&ret, "{} = {}",
                   lab,
                   PatString(child));
    }
    ret.push_back('}');
    return ret;
  }

  case PatType::OBJECT: {
    std::string ret = std::format("{{" "({}) ", p->str);
    for (int i = 0; i < (int)p->str_children.size(); i++) {
      const auto [lab, child] = p->str_children[i];
      if (i != 0)
        ret.append(", ");
      AppendFormat(&ret, "{} = {}",
                   lab,
                   PatString(child));
    }
    ret.push_back('}');
    return ret;
  }

  case PatType::ANN: {
    return std::format("{} : {}",
                       PatString(p->a),
                       TypeString(p->ann));
  }
  case PatType::AS: {
    return std::format("{} as {}",
                       PatString(p->a),
                       PatString(p->b));
  }

  case PatType::APP: {
    return std::format("{} {}",
                       p->str,
                       PatString(p->a));
  }
  }
  return "??PAT??";
}

std::string DecString(const Dec *d) {
  if (d == nullptr) return "NULL!?";
  switch (d->type) {

  case DecType::VAL:
    return std::format("val {} = {}",
                       PatString(d->pat),
                       ExpString(d->exp));

  case DecType::FUN: {
    std::string ret;
    for (int i = 0; i < (int)d->funs.size(); i++) {
      const FunDec &fd = d->funs[i];
      AppendFormat(&ret, "{} ", i == 0 ? "fun" : "and");
      bool first = true;
      for (const auto &[cpat, cexp] : fd.clauses) {
        if (!first) {
          ret.append(" | ");
        }
        ret.append(fd.name);
        for (const Pat *p : cpat) {
          AppendFormat(&ret, " ({})", PatString(p));
        }
        AppendFormat(&ret, " = {}\n",
                     ExpString(cexp));
        first = false;
      }
    }
    return ret;
  }

  case DecType::OBJECT: {
    std::vector<std::string> fs;
    for (const auto &[lab, t] : d->object.fields) {
      fs.push_back(std::format("{} : {}", lab, TypeString(t)));
    }
    return std::format("object {} of {{ {} }}\n",
                        d->object.name,
                        Util::Join(fs, ", "));
  }

  case DecType::TYPE: {
    CHECK(d->tyvars.empty()) << "unimplemented";
    return std::format("type {} = {}\n",
                       d->str,
                       TypeString(d->t));
  }

  case DecType::OPEN:
    return std::format("open {}\n", ExpString(d->exp));

  case DecType::LOCAL: {
    std::string ret = "local\n";
    for (const Dec *dec : d->decs1) {
      AppendFormat(&ret, "  {}\n", DecString(dec));
    }
    ret += "in\n";
    for (const Dec *dec : d->decs2) {
      AppendFormat(&ret, "  {}\n", DecString(dec));
    }
    ret += "end\n";
    return ret;
  }

  case DecType::DATATYPE: {
    std::string tyvars;
    if (!d->tyvars.empty()) {
      tyvars = "(" + Util::Join(d->tyvars, ",") + ") ";
    }
    std::string ret = std::format("datatype {}", tyvars);
    for (int i = 0; i < (int)d->datatypes.size(); i++) {
      const DatatypeDec &dd = d->datatypes[i];
      if (i != 0) ret.append("\nand");
      AppendFormat(&ret, " {} =\n", dd.name);
      for (int j = 0; j < (int)dd.arms.size(); j++) {
        const auto &arm = dd.arms[j];
        if (j == 0) ret += "\n    ";
        else ret += "\n  | ";
        ret += arm.first;
        if (arm.second != nullptr){
          AppendFormat(&ret, " of {}", TypeString(arm.second));
        }
      }
    }
    return ret;
  }
  }
  return "??DEC??";
}

// TODO: Some kind of pretty-printing
std::string ExpString(const Exp *e) {
  if (e == nullptr) return "NULL!?";
  switch (e->type) {
  case ExpType::STRING:
    return std::format("\"{}\"", EscapeString(e->str));

  case ExpType::VAR:
    return e->str;

  case ExpType::INT:
    return e->integer.ToString();

  case ExpType::BOOL:
    return e->boolean ? "true" : "false";

  case ExpType::FLOAT:
    return std::format("{:.17g}", e->d);

  case ExpType::ANDALSO:
    return std::format("({}) andalso ({})",
                        ExpString(e->a),
                        ExpString(e->b));
  case ExpType::ORELSE:
    return std::format("({}) orelse ({})",
                       ExpString(e->a),
                       ExpString(e->b));

  case ExpType::TUPLE: {
    std::string ret = "(";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) ret.append(", ");
      ret += ExpString(e->children[i]);
    }
    ret += ")";
    return ret;
  }

  case ExpType::RECORD: {
    std::string ret = "{";
    for (int i = 0; i < (int)e->str_children.size(); i++) {
      const auto &[lab, child] = e->str_children[i];
      if (i != 0) ret.append(", ");
      AppendFormat(&ret, "{} = {}",
                   lab, ExpString(child));
    }
    ret += "}";
    return ret;
  }

  case ExpType::OBJECT: {
    std::string ret =
      std::format("{{" "({}) ", e->str);
    for (int i = 0; i < (int)e->str_children.size(); i++) {
      const auto &[lab, child] = e->str_children[i];
      if (i != 0) ret.append(", ");
      AppendFormat(&ret, "{} = {}",
                   lab, ExpString(child));
    }
    ret += "}";
    return ret;
  }

  case ExpType::WITH:
    return std::format("({} with ({}) {} = {})",
                        ExpString(e->a),
                        e->str,
                        e->str2,
                        ExpString(e->b));

  case ExpType::WITHOUT:
    return std::format("({} without ({}) {})",
                        ExpString(e->a),
                        e->str,
                        e->str2);

  case ExpType::JOIN: {
    std::string ret = "[";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) ret += ", ";
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

    return std::format("let {} in {} end",
                       Util::Join(decs, "\n"),
                       ExpString(e->a));
  }

  case ExpType::IF: {
    return std::format("(if {} then {} else {})",
                       ExpString(e->a),
                       ExpString(e->b),
                       ExpString(e->c));
  }

  case ExpType::APP: {
    return std::format("({} {})",
                       ExpString(e->a),
                       ExpString(e->b));
  }

  case ExpType::FN: {
    const std::string as =
      e->str.empty() ? "" : std::format(" as {}", e->str);
    std::string ret = std::format("(fn{} ", as);
    bool first = true;
    for (const auto &[pat, body] : e->clauses) {
      if (!first) ret.append("\n | ");
      AppendFormat(&ret, "{} => {}",
                   PatString(pat),
                   ExpString(body));
      first = false;
    }
    return ret + ")";
  }

  case ExpType::CASE: {
    std::vector<std::string> arms;
    arms.reserve(e->clauses.size());
    for (const auto &[pat, exp] : e->clauses) {
      arms.push_back(std::format("{} => {}",
                                 PatString(pat),
                                 ExpString(exp)));
    }
    return std::format("(case {} of\n"
                       "   {})",
                       ExpString(e->a),
                       Util::Join(arms, "\n | "));
  }

  case ExpType::FAIL:
    return std::format("(fail {})", ExpString(e->a));

  case ExpType::LAYOUT:
    return std::format("[{}]", LayoutString(e->layout));

  case ExpType::ANN:
    return std::format("({} : {})",
                       ExpString(e->a),
                       TypeString(e->t));
  }
  return "??EXP??";
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
  }

  LOG(FATAL) << "Unimplemented layout type.";
  return {};
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
  }

  LOG(FATAL) << "Unimplemented layout type.";
  return false;
}



bool IsValuable(const Exp *e) {
  switch (e->type) {
  case ExpType::STRING: return true;
  case ExpType::VAR: return true;
  case ExpType::INT: return true;
  case ExpType::BOOL: return true;
  case ExpType::FLOAT: return true;

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

  case ExpType::CASE:
    return false;
  case ExpType::ANDALSO:
    return false;
  case ExpType::ORELSE:
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

  case ExpType::RECORD:
    for (const auto &[lab, child] : e->str_children)
      if (!IsValuable(child))
        return false;
    return true;

  case ExpType::OBJECT:
    for (const auto &[lab, child] : e->str_children)
      if (!IsValuable(child))
        return false;
    return true;

  case ExpType::WITH:
    return false;
  case ExpType::WITHOUT:
    return false;

  case ExpType::FAIL:
    return false;
  }

  LOG(FATAL) << "ILLEGAL EXPRESSION";
  return false;
}

std::string AstPool::NewInternalVar(const std::string &hint) {
  next_internal_var++;
  // Need something that's different from the IL's reserved symbol ($)
  // -- or else we would need to coordinate the counter with IL --
  // and can't be written by the user.
  return std::format("{}#{}",
                     hint.empty() ? "internal" : hint,
                     next_internal_var);
}

#define AKEYWORD(s) AFGCOLOR(221, 221, 170, s)
#define ALABEL(s) APURPLE(s)
#define AOBJNAME(s) AORANGE(s)
#define AVAR(s) ABLUE(s)


// Just atomic expressions.
static std::string VeryShortColorExpString(const Exp *e) {
  if (e == nullptr) return ARED("NULL!?");
  switch (e->type) {
  case ExpType::STRING: {
    std::string ss = EscapeString(e->str);
    std::string s = ss.size() > 10 ?
      std::format("{}" AGREY("..."), ss.substr(0, 10)) :
      ss;
    return std::format(ANSI_GREEN "\"{}" ANSI_GREEN "\"" ANSI_RESET,
                       s);
  }

  case ExpType::VAR:
    return std::format(AVAR("{}"), e->str);

  case ExpType::INT:
    return e->integer.ToString();

  case ExpType::BOOL:
    return e->boolean ? "true" : "false";

  case ExpType::FLOAT:
    return std::format("{:.17g}", e->d);

  default: return AGREY("...");
  }
}

std::string VeryShortColorTypeString(const Type *t) {
  if (t == nullptr) return ARED("NULL!?");
  switch (t->type) {
  case TypeType::VAR:
    if (t->children.empty()) {
      return std::format(AVAR("{}"), t->var);
    } else if (t->children.size() == 1) {
      return std::format("{} " AVAR("{}"),
                         VeryShortColorTypeString(t->children[0]),
                         t->var);
    } else {
      return std::format("(" AGREY("{} var") ") " AVAR("{}"),
                         t->children.size(),
                         t->var);
    }

  default:
    return std::format(AWHITE("{}"), TypeTypeString(t->type));
  }
}


std::string VeryShortColorPatString(const Pat *p) {
  if (p == nullptr) return ARED("NULL!?");
  switch (p->type) {
  case PatType::STRING: {
    std::string ss = EscapeString(p->str);
    std::string s = ss.size() > 10 ?
      std::format("{}" AGREY("..."), ss.substr(0, 10)) :
      ss;
    return std::format(ANSI_GREEN "\"{}" ANSI_GREEN "\"" ANSI_RESET, s);
  }

  case PatType::INT: return p->integer.ToString();
  case PatType::BOOL: return p->boolean ? "true" : "false";
  case PatType::WILD: return "_";
  case PatType::VAR: return std::format(AVAR("{}"), p->str);
  case PatType::TUPLE:
    if (p->children.empty()) return "()";
    else return "(" AGREY("...") ")";
  case PatType::RECORD:
    if (p->str_children.empty()) return "{}";
    else return "{" AGREY("...") "}";
  case PatType::OBJECT:
    return std::format("{{" "(" AOBJNAME("{}") ")" AGREY("...") "}}",
                       p->str);
  case PatType::ANN: return AGREY("...") " : " AGREY("...");
  case PatType::AS: return AGREY("...") " " AKEYWORD("as") " " AGREY("...");
  case PatType::APP:
    return std::format(ABLUE("{}") " " AGREY("..."), p->str);
  }
  return ARED("??PAT??");
}

std::string ShortColorPatString(const Pat *p) {
  // TODO: Expand some cases.
  return VeryShortColorPatString(p);
}

std::string ShortColorExpString(const Exp *e) {
  if (e == nullptr) return ARED("NULL!?");
  switch (e->type) {
  case ExpType::ANDALSO:
    return std::format("{} " AKEYWORD("andalso") " {}",
                       VeryShortColorExpString(e->a),
                       VeryShortColorExpString(e->b));
  case ExpType::ORELSE:
    return std::format("{} " AKEYWORD("orelse") " {}",
                       ExpString(e->a),
                       ExpString(e->b));

  case ExpType::TUPLE: {
    std::string ret = "(";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) ret += ", ";
      ret += VeryShortColorExpString(e->children[i]);
    }
    ret += ")";
    return ret;
  }

  case ExpType::RECORD: {
    std::string ret = "{";
    for (int i = 0; i < (int)e->str_children.size(); i++) {
      const auto &[lab, child] = e->str_children[i];
      if (i != 0) ret.append(", ");
      AppendFormat(&ret, ALABEL("{}") " = {}",
                   lab,
                   VeryShortColorExpString(child));
    }
    ret += "}";
    return ret;
  }

  case ExpType::OBJECT: {
    std::string ret =
      std::format("{{" "(" AOBJNAME ("{}") ") ", e->str);
    for (int i = 0; i < (int)e->str_children.size(); i++) {
      const auto &[lab, child] = e->str_children[i];
      if (i != 0) ret.append(", ");
      AppendFormat(&ret, ALABEL("{}") " = {}",
                   lab,
                   VeryShortColorExpString(child));
    }
    ret += "}";
    return ret;
  }

  case ExpType::WITH:
    return std::format("{} " AKEYWORD("with") "(" AOBJNAME("{}") ")"
                       " " ALABEL("{}") " = {}",
                       VeryShortColorExpString(e->a),
                       e->str,
                       e->str2,
                       VeryShortColorExpString(e->b));

  case ExpType::WITHOUT:
    return std::format("{} " AKEYWORD("without") " (" AOBJNAME("{}") ") "
                       ALABEL("{}"),
                       VeryShortColorExpString(e->a),
                       e->str,
                       e->str2);

  case ExpType::JOIN: {
    std::string ret = "[";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) ret.append(", ");
      ret.append(VeryShortColorExpString(e->children[i]));
    }
    ret += "]";
    return ret;
  }

  case ExpType::LET: {
    return std::format(AKEYWORD("let") " " AGREY("...") " "
                       AKEYWORD("in") " {} "
                       AKEYWORD("end"),
                       VeryShortColorExpString(e->a));
  }

  case ExpType::IF: {
    return std::format(AKEYWORD("if") " {} "
                       AKEYWORD("then") " {} "
                       AKEYWORD("else") " {}",
                       VeryShortColorExpString(e->a),
                       VeryShortColorExpString(e->b),
                       VeryShortColorExpString(e->c));
  }

  case ExpType::APP: {
    return std::format("{} {}",
                       VeryShortColorExpString(e->a),
                       VeryShortColorExpString(e->b));
  }

  case ExpType::FN: {
    const std::string as =
      e->str.empty() ? "" : std::format(" " AKEYWORD("as") " "
                                        AVAR("{}"), e->str);
    std::string ret = std::format(AKEYWORD("fn") "{} ", as);
    CHECK(!e->clauses.empty());
    AppendFormat(&ret, "{} => {}",
                 VeryShortColorPatString(e->clauses[0].first),
                 VeryShortColorExpString(e->clauses[0].second));
    if (e->clauses.size() > 1)
      ret += " | " AGREY("...");

    return ret;
  }

  case ExpType::CASE: {
    // XXX show something
    return AKEYWORD("case") " " AGREY("...");
  }

  case ExpType::FAIL:
    return std::format(AKEYWORD("fail") " {}",
                       VeryShortColorExpString(e->a));

  case ExpType::LAYOUT:
    // XXX show something
    // LayoutString(e->layout);
    return "[" AGREY("...") "]";

  case ExpType::ANN:
    return std::format("{} : {}",
                       VeryShortColorExpString(e->a),
                       VeryShortColorTypeString(e->t));

  default:
    return VeryShortColorExpString(e);
  }
  LOG(FATAL) << "Impossible";
  return ARED("??EXP??");
}

std::string ShortColorDecString(const Dec *d) {
  if (d == nullptr) return ARED("NULL!?");
  switch (d->type) {

  case DecType::VAL:
    return std::format(AKEYWORD("val") " {} = {}",
                       ShortColorPatString(d->pat),
                       ShortColorExpString(d->exp));

  case DecType::FUN: {
    std::string ret = AKEYWORD("fun");
    if (d->funs.empty()) ret += " " ARED("EMPTY??");
    // TODO can use short pat, exp
    else AppendFormat(&ret, " " AVAR("{}") " " AGREY("..."),
                      d->funs[0].name);
    return ret;
  }

  case DecType::OBJECT: {
    return std::format(AKEYWORD("object") " " AVAR("{}") " "
                       AKEYWORD("of") " {{ " AGREY("...") " }}",
                       d->object.name);
  }

  case DecType::TYPE: {
    return std::format(AKEYWORD("type") " " AVAR("{}") " = {}",
                       d->str,
                       VeryShortColorTypeString(d->t));
  }

  case DecType::OPEN:
    return std::format(AKEYWORD("open") " {}\n",
                       ShortColorExpString(d->exp));

  case DecType::LOCAL: {
    // XXX would be good to show one declaration for wayfinding
    std::string ret = AKEYWORD("local") " " AGREY("...") " "
      AKEYWORD("in") " " AGREY("...") " " AKEYWORD("end");
    return ret;
  }

  case DecType::DATATYPE: {
    std::string ret = AKEYWORD("datatype");
    if (d->datatypes.empty()) ret += " " ARED("EMPTY??");
    else ret += std::format(" " AVAR("{}") " = " AGREY("..."),
                            d->datatypes[0].name);
    return ret;
  }
  }

  return ARED("??DEC??");
}

size_t ExpNearbyPos(const el::Exp *exp) {
  if (exp->pos != 0) return exp->pos;
  switch (exp->type) {

  case ExpType::JOIN: {
    for (const Exp *child : exp->children) {
      if (size_t p = ExpNearbyPos(child)) {
        return p;
      }
    }
    return 0;
  }

    // case ExpType::LET:
    // check decs and exp

  case ExpType::IF: {
    if (size_t p = ExpNearbyPos(exp->a)) {
      return p;
    } else if (size_t pp = ExpNearbyPos(exp->b)) {
      return pp;
    } else {
      return ExpNearbyPos(exp->c);
    }
  }

  case ExpType::APP: {
    if (size_t p = ExpNearbyPos(exp->a)) {
      return p;
    } else {
      return ExpNearbyPos(exp->b);
    }
  }

  case ExpType::FN: {
    for (const auto &[pat, e] : exp->clauses) {
      if (size_t p = ExpNearbyPos(e)) {
        return p;
      }
    }
    return 0;
  }

  case ExpType::CASE:
    return ExpNearbyPos(exp->a);

  case ExpType::FAIL:
    return ExpNearbyPos(exp->a);

  case ExpType::ANN:
    return ExpNearbyPos(exp->a);

  default:
    return 0;
  }
}

}  // namespace el
