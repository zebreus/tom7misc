
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
  case TypeType::FLOAT: return "FLOAT";
  default: return "???MISSING???";
  }
}

std::string AstPool::NewVar(const std::string &hint_in) {
  next_var++;
  std::string hint = BaseVar(hint_in);
  if (hint.empty()) {
    return StringPrintf("x$%d", next_var);
  } else {
    return StringPrintf("%s$%d", hint.c_str(), next_var);
  }
}

std::string AstPool::BaseVar(const std::string &hint) {
  return hint.substr(0, hint.find('$'));
}

const Type *AstPool::Product(const std::vector<const Type *> &v,
                             const Type *guess) {
  std::vector<std::pair<std::string, const Type *>> record_type;
  record_type.reserve(v.size());
  for (int i = 0; i < (int)v.size(); i++) {
    record_type.emplace_back(StringPrintf("%d", i + 1), v[i]);
  }
  return RecordType(record_type, guess);
}

std::string TypeString(const Type *t) {
  auto RecordOrSumBody = [](
      const std::vector<std::pair<std::string, const Type *>> &v) {
      std::string ret;
      for (int i = 0; i < (int)v.size(); i++) {
        const auto &[lab, child] = v[i];
        if (i != 0)
          StringAppendF(&ret, ", ");
        StringAppendF(&ret, "%s: %s",
                      lab.c_str(),
                      TypeString(child).c_str());
      }
      return ret;
    };

  switch (t->type) {
  case TypeType::VAR: {
    const auto &[var, args] = t->Var();
    switch (args.size()) {
    case 0:
      return var;
    case 1:
      return StringPrintf("%s %s",
                          TypeString(args[0]).c_str(),
                          var.c_str());
    default: {
      std::string sargs;
      for (int i = 0; i < (int)args.size(); i++) {
        if (i != 0) StringAppendF(&sargs, ", ");
        StringAppendF(&sargs, "%s", TypeString(args[i]).c_str());
      }
      return StringPrintf("(%s) %s", sargs.c_str(), var.c_str());
    }
  }
  }

  case TypeType::ARROW: {
    const auto &[dom, cod] = t->Arrow();
    return StringPrintf("(%s → %s)",
                        TypeString(dom).c_str(),
                        TypeString(cod).c_str());
  }

  case TypeType::RECORD:
    // Might want to special case tuple types; unit type?
    return StringPrintf("{%s}", RecordOrSumBody(t->Record()).c_str());

  case TypeType::SUM:
    // Might want to special case void?
    return StringPrintf("[%s]", RecordOrSumBody(t->Sum()).c_str());

  case TypeType::MU: {
    const auto &[idx, bundle] = t->Mu();
    CHECK(!bundle.empty());
    if (bundle.size() == 1) {
      return StringPrintf("(μ %s. %s)",
                          bundle[0].first.c_str(),
                          TypeString(bundle[0].second).c_str());
    } else {
      std::string ret = StringPrintf("#%d(μ", idx);
      for (int i = 0; i < (int)bundle.size(); i++) {
        if (i != 0) StringAppendF(&ret, ";");
        const auto &[v, c] = bundle[i];
        StringAppendF(&ret,
                      " %d=%s. %s", i, v.c_str(), TypeString(c).c_str());
      }
      ret += ")";
      return ret;
    }
  }

  case TypeType::EVAR: {
    const auto &evar = t->EVar();
    if (const Type *u = evar.GetBound()) {
      // We treat bound evars transparently, although for some
      // uses it might be good to be able to see it?
      return TypeString(u).c_str();
    } else {
      return evar.ToString();
    }
  }

  case TypeType::REF:
    return StringPrintf("(%s ref)", TypeString(t->Ref()).c_str());

  case TypeType::STRING:
    return "string";

  case TypeType::INT:
    return "int";

  case TypeType::FLOAT:
    return "float";

  default:
    return "unknown type type??";
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

  case ExpType::FLOAT:
    return StringPrintf("%.17g", e->Float());

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

  case ExpType::PROJECT: {
    const auto &[lab, r] = e->Project();
    return StringPrintf("#%s(%s)", lab.c_str(), ExpString(r).c_str());
  }

  case ExpType::INJECT: {
    const auto &[lab, r] = e->Inject();
    return StringPrintf("[%s = %s]", lab.c_str(), ExpString(r).c_str());
  }

  case ExpType::ROLL: {
    const auto &[t, child] = e->Roll();
    return StringPrintf("roll<%s>(%s)",
                        TypeString(t).c_str(),
                        ExpString(child).c_str());
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
    const auto &[tvs, x, rhs, body] = e->Let();
    std::string tyvars;
    if (!tvs.empty()) {
      tyvars = "(" + Util::Join(tvs, ",") + ") ";
    }
    return StringPrintf("let val %s%s = %s\n"
                        "in %s\n"
                        "end",
                        tyvars.c_str(),
                        x.c_str(),
                        ExpString(rhs).c_str(),
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
    const auto &[po, types, children] = e->Primop();

    std::string targs;
    for (int i = 0; i < (int)types.size(); i++) {
      if (i != 0)
        StringAppendF(&targs, ", ");
      StringAppendF(&targs, "%s", TypeString(types[i]).c_str());
    }
    if (!types.empty()) targs = StringPrintf("<%s>", targs.c_str());

    std::string args;
    for (int i = 0; i < (int)children.size(); i++) {
      if (i != 0)
        StringAppendF(&args, ", ");
      StringAppendF(&args, "%s", ExpString(children[i]).c_str());
    }
    return StringPrintf("%s%s(%s)",
                        PrimopString(po),
                        targs.c_str(),
                        args.c_str());
  }

  case ExpType::FAIL: {
    const std::string &msg = e->Fail();
    return StringPrintf("fail \"%s\"", msg.c_str());
  }

  case ExpType::SEQ: {
    const auto &[es, body] = e->Seq();
    std::vector<std::string> v;

    std::string ret = "seq(\n";
    for (const Exp *c : es)
      StringAppendF(&ret, "  %s;\n", ExpString(c).c_str());
    StringAppendF(&ret, "  %s)", ExpString(body).c_str());
    return ret;
  }

  case ExpType::INTCASE: {
    const auto &[obj, arms, def] = e->IntCase();
    std::vector<std::string> sarms;
    for (const auto &[bi, arm] : arms)
      sarms.push_back(StringPrintf("%s => %s",
                                   bi.ToString().c_str(),
                                   ExpString(arm).c_str()));
    return StringPrintf("intcase %s of\n"
                        "   %s\n"
                        " | _ => %s",
                        ExpString(obj).c_str(),
                        Util::Join(sarms, "\n | ").c_str(),
                        ExpString(def).c_str());
  }

  default:
    return "ILLEGAL EXPRESSION";
  }
}

const std::pair<std::string, const Type *>
AstPool::AlphaVaryType(const std::string &x, const Type *t) {
  std::string xx = NewVar(x);
  // [x'/x]t, where we assert x' does not appear in t.
  return std::make_pair(xx, SubstTypeInternal(VarType(xx, {}), x, t, true));
}

const Type *AstPool::SubstType(const Type *t, const std::string &v,
                               const Type *u) {
  return SubstTypeInternal(t, v, u, false);
}

// If is_simple, then t is assumed to have no free variables that occur
// in u (or v). This is typically because it is a fresh variable.
const Type *AstPool::SubstTypeInternal(const Type *t, const std::string &v,
                                       const Type *u, bool is_simple) {
  auto RecordOrSum = [this, is_simple](
      const Type *t, const std::string &v,
      const std::vector<std::pair<std::string, const Type *>> &sch) {
      std::vector<std::pair<std::string, const Type *>> ret;
      ret.reserve(sch.size());
      for (const auto &[l, u] : sch)
        ret.emplace_back(l, SubstTypeInternal(t, v, u, is_simple));
      return ret;
    };

  switch (u->type) {
  case TypeType::VAR:
    if (u->var == v) {
      // A variable can be v<t1, t2, ...>.
      // The substituted type t has kind 0, so if this variable is
      // applied to type args, something is wrong.
      // XXX We probably do need to support substitution of something
      // like Λα.(1 + α) for "option", though.
      CHECK(u->children.empty()) << "Unimplemented: Substitution at "
        "kind > 0. (Or there's a bug!)";

      return t;
    } else {
      return u;
    }

  case TypeType::SUM: {
    std::vector<std::pair<std::string, const Type *>> sch =
      RecordOrSum(t, v, u->str_children);
    return SumType(std::move(sch), u);
  }

  case TypeType::ARROW:
    return Arrow(SubstTypeInternal(t, v, u->a, is_simple),
                 SubstTypeInternal(t, v, u->b, is_simple), u);

  case TypeType::MU: {
    const auto &[idx, bundle] = u->Mu();

    std::vector<std::pair<std::string, const Type *>> new_bundle;
    for (const auto &[a, body] : bundle) {
      // Target variable is shadowed so it cannot occur.
      if (a == v) {
        new_bundle.emplace_back(a, body);
      } else {
        // PERF: Only do this if a occurs in t and would cause capture.
        // Also, we could probably get rid of the complexity of is_simple
        // if we did an explicit check, since we would avoid the
        // infinite regress by construction (the fresh variable will not
        // appear).
        if (is_simple) {
          new_bundle.emplace_back(
              a, SubstTypeInternal(t, v, body, is_simple));
        } else {
          const auto &[new_a, new_body] = AlphaVaryType(a, body);
          new_bundle.emplace_back(
              new_a, SubstTypeInternal(t, v, body, is_simple));
        }
      }
    }

    return Mu(idx, new_bundle, u);
  }

  case TypeType::RECORD: {
    std::vector<std::pair<std::string, const Type *>> sch =
      RecordOrSum(t, v, u->str_children);
    return RecordType(std::move(sch), u);
  }

  case TypeType::EVAR: {
    if (const Type *uu = u->evar.GetBound()) {
      return SubstTypeInternal(t, v, uu, is_simple);
    } else {
      // I think? Another point of view is that we should keep
      // a delayed substitution here.
      return u;
    }
  }

  case TypeType::REF:
    return RefType(SubstTypeInternal(t, v, u->a, is_simple), u);

  case TypeType::STRING:
    return u;

  case TypeType::INT:
    return u;

  case TypeType::FLOAT:
    return u;

  default:
    LOG(FATAL) << "Unimplemented typetype in subst: "
               << TypeTypeString(u->type);
    return nullptr;
  }
}


}  // namespace il
