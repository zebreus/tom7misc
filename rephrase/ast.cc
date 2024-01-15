
#include "ast.h"

#include "base/logging.h"

std::string LayoutString(const Layout *lay) {
  switch (lay->type) {
    case LayoutType::TEXT:
      return lay->str;
    default:
      return "TODO LAYOUT TYPE";
  }
}

std::string ExpString(const Exp *e) {
  switch (e->type) {
  case ExpType::STRING:
    // XXX escaping
    return StringPrintf("\"%s\"", e->str.c_str());
  case ExpType::VAR:
    return e->str;
  case ExpType::INTEGER:
    return StringPrintf("%lld", e->integer);
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
  case ExpType::LAYOUT:
    return StringPrintf("[%s]", LayoutString(e->layout).c_str());
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
