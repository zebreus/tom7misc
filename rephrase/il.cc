
#include "il.h"

#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"

namespace il {

const char *TypeTypeString(TypeType t) {
  switch (t) {
  case TypeType::VAR: return "VAR";
  case TypeType::SUM: return "SUM";
  case TypeType::ARROW: return "ARROW";
  case TypeType::MU: return "MU";
  case TypeType::RECORD: return "RECORD";
  case TypeType::EVAR: return "EVAR";
  case TypeType::STRING: return "STRING";
  case TypeType::INT: return "INT";
  default: return "???MISSING???";
  }
}

const Type *AstPool::Product(const std::vector<const Type *> &v) {
  std::vector<std::pair<std::string, const Type *>> record_type;
  record_type.reserve(v.size());
  for (int i = 0; i < (int)v.size(); i++) {
    record_type.emplace_back(StringPrintf("%d", i + 1), v[i]);
  }
  return RecordType(std::move(record_type));
}


std::string TypeString(const Type *t) {
  switch (t->type) {
  case TypeType::VAR:
    if (t->children.empty()) {
      return t->var;
    } else {
      return StringPrintf("(?TODO?) %s", t->var.c_str());
    }
  case TypeType::ARROW:
    return StringPrintf("(%s -> %s)",
                        TypeString(t->a).c_str(),
                        TypeString(t->b).c_str());
  case TypeType::RECORD: {
    std::string ret = "{";
    for (int i = 0; i < (int)t->labeled_children.size(); i++) {
      const auto &[lab, child] = t->labeled_children[i];
      if (i != 0)
        StringAppendF(&ret, ", ");
      StringAppendF(&ret, "%s: %s",
                    lab.c_str(),
                    TypeString(child).c_str());
    }
    ret.push_back('}');
    return ret;
  }
  default:
    return "unknown type type??";
  }
}

std::string DecString(const Dec *d) {
  switch (d->type) {
  case DecType::VAL:
    return StringPrintf("val %s = %s",
                        d->str.c_str(),
                        ExpString(d->exp).c_str());
  case DecType::FUN:
    return StringPrintf("fun %s (XXX) = %s",
                        d->str.c_str(),
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
  case ExpType::RECORD: {
    std::string ret = "{";
    for (int i = 0; i < (int)e->labeled_children.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      const auto &[l, v] = e->labeled_children[i];
      StringAppendF(&ret, "%s: %s", l.c_str(), ExpString(v).c_str());
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
  default:
    return "ILLEGAL EXPRESSION";
  }
}

}  // namespace el
