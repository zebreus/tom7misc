
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
  case TypeType::REF: return "REF";
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
  auto RecordOrSumBody = [](const Type *t) {
      std::string ret = "{";
      for (int i = 0; i < (int)t->str_children.size(); i++) {
        const auto &[lab, child] = t->str_children[i];
        if (i != 0)
          StringAppendF(&ret, ", ");
        StringAppendF(&ret, "%s: %s",
                      lab.c_str(),
                      TypeString(child).c_str());
      }
      ret.push_back('}');
      return ret;
    };

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
  case TypeType::RECORD:
    // Might want to special case tuple types; unit type?
    return StringPrintf("{%s}", RecordOrSumBody(t).c_str());
  case TypeType::SUM:
    // Might want to special case void?
    return StringPrintf("[%s]", RecordOrSumBody(t).c_str());

  case TypeType::MU: {
    CHECK(!t->str_children.empty());
    if (t->str_children.size() == 1) {
      return StringPrintf("(μ %s. %s)",
                          t->str_children[0].first.c_str(),
                          TypeString(t->str_children[0].second).c_str());
    } else {
      std::string ret = StringPrintf("#%d(μ", t->idx);
      for (int i = 0; i < (int)t->str_children.size(); i++) {
        if (i != 0) StringAppendF(&ret, ";");
        const auto &[v, c] = t->str_children[i];
        StringAppendF(&ret,
                      " %d=%s. %s", i, v.c_str(), TypeString(c).c_str());
      }
      ret += ")";
      return ret;
    }
  }

  case TypeType::REF:
    return StringPrintf("(%s ref)", TypeString(t->a).c_str());
  case TypeType::STRING:
    return "string";
  case TypeType::INT:
    return "int";

  default:
    return "unknown type type??";
  }
}

std::string DecString(const Dec *d) {
  switch (d->type) {
  case DecType::VAL:
    // TODO: Type vars
    return StringPrintf("val %s = %s",
                        d->str.c_str(),
                        ExpString(d->exp).c_str());
  case DecType::FUN:
    // TODO: Type vars
    return StringPrintf("fun %s (XXX) = %s",
                        d->str.c_str(),
                        ExpString(d->exp).c_str());
  default:
    return "TODO DECTYPE";
  }
}

// TODO: Some kind of pretty-printing
std::string ExpString(const Exp *e) {
  switch (e->type) {
  case ExpType::STRING:
    // XXX escaping
    return StringPrintf("\"%s\"", e->String().c_str());

  case ExpType::VAR: {
    const auto &[types, x] = e->Var();
    if (types.empty()) {
      return x;
    } else {
      std::string args;
      for (int i = 0; i < (int)types.size(); i++) {
        if (i != 0) StringAppendF(&args, ", ");
        StringAppendF(&args, "%s", TypeString(types[i]).c_str());
      }
      return StringPrintf("%s<%s>", x.c_str(), args.c_str());
    }
  }

  case ExpType::FN: {
    const auto &[self, x, body] = e->Fn();
    const std::string as =
      self.empty() ? "" : StringPrintf(" as %s", self.c_str());
    return StringPrintf("(fn%s %s => %s)",
                        as.c_str(), x.c_str(), ExpString(body).c_str());
  }

  case ExpType::INTEGER:
    return e->Integer().ToString();

  case ExpType::RECORD: {
    const auto &fields = e->Record();
    std::string ret = "{";
    for (int i = 0; i < (int)fields.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      const auto &[l, v] = fields[i];
      StringAppendF(&ret, "%s: %s", l.c_str(), ExpString(v).c_str());
    }
    ret += "}";
    return ret;
  }

  case ExpType::JOIN: {
    const auto &children = e->Join();
    std::string ret = "[";
    for (int i = 0; i < (int)children.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      ret += ExpString(children[i]);
    }
    ret += "]";
    return ret;
  }

  case ExpType::LET: {
    const auto &[decs, body] = e->Let();
    std::vector<std::string> dstrs;
    dstrs.reserve(decs.size());
    for (const Dec *d : decs) {
      dstrs.push_back(DecString(d));
    }
    return StringPrintf("let %s in %s end",
                        Util::Join(dstrs, " ").c_str(),
                        ExpString(body).c_str());
  }

  case ExpType::IF: {
    const auto &[cond, t, f] = e->If();
    return StringPrintf("(if %s then %s else %s)",
                        ExpString(cond).c_str(),
                        ExpString(t).c_str(),
                        ExpString(f).c_str());
  }

  case ExpType::APP: {
    const auto &[f, x] = e->App();
    return StringPrintf("(%s %s)",
                        ExpString(f).c_str(),
                        ExpString(x).c_str());
  }

  case ExpType::PRIMOP: {
    const auto &[po, children] = e->Primop();
    std::string args;
    for (int i = 0; i < (int)children.size(); i++) {
      if (i != 0)
        StringAppendF(&args, ", ");
      StringAppendF(&args, "%s", ExpString(children[i]).c_str());
    }
    return StringPrintf("%s(%s)", PrimopString(po), args.c_str());
  }

  default:
    return "ILLEGAL EXPRESSION";
  }
}

const Type *AstPool::SubstType(const Type *t, const std::string &v,
                               const Type *u) {
  auto RecordOrSum = [this](
      const Type *t, const std::string &v,
      const std::vector<std::pair<std::string, const Type *>> &sch) {
      std::vector<std::pair<std::string, const Type *>> ret;
      ret.reserve(sch.size());
      for (const auto &[l, u] : sch)
        ret.emplace_back(l, SubstType(t, v, u));
      return ret;
    };

  switch (u->type) {
  case TypeType::VAR:
    LOG(FATAL) << "Unimplemented: Substitution into type variable";
    return nullptr;
  case TypeType::SUM: {
    std::vector<std::pair<std::string, const Type *>> sch =
      RecordOrSum(t, v, u->str_children);
    return SumType(std::move(sch));
  }
  case TypeType::ARROW:
    return Arrow(SubstType(t, v, u->a), SubstType(t, v, u->b));
  case TypeType::MU:
    LOG(FATAL) << "Unimplemented: Substitution into mu type";
    return nullptr;
  case TypeType::RECORD: {
    std::vector<std::pair<std::string, const Type *>> sch =
      RecordOrSum(t, v, u->str_children);
    return RecordType(std::move(sch));
  }
  case TypeType::EVAR: {
    if (const Type *uu = u->evar.GetBound()) {
      return SubstType(t, v, uu);
    } else {
      // I think? Another point of view is that we should keep
      // a delayed substitution here.
      return u;
    }
  }
  case TypeType::REF:
    return RefType(SubstType(t, v, u->a));
  case TypeType::STRING:
    return u;
  case TypeType::INT:
    return u;
  default:
    LOG(FATAL) << "Unimplemented typetype in subst";
    return nullptr;
  }
}


}  // namespace il
