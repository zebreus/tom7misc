
#include "il.h"

#include <cstdio>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "pp.h"
#include "primop.h"
#include "util.h"

namespace il {

[[maybe_unused]]
static constexpr bool VERBOSE = false;

const char *TypeTypeString(TypeType t) {
  switch (t) {
  case TypeType::VAR: return "VAR";
  case TypeType::SUM: return "SUM";
  case TypeType::ARROW: return "ARROW";
  case TypeType::MU: return "MU";
  case TypeType::EXISTS: return "EXISTS";
  case TypeType::FORALL: return "FORALL";
  case TypeType::RECORD: return "RECORD";
  case TypeType::EVAR: return "EVAR";
  case TypeType::REF: return "REF";
  case TypeType::VEC: return "VEC";
  case TypeType::STRING: return "STRING";
  case TypeType::INT: return "INT";
  case TypeType::FLOAT: return "FLOAT";
  case TypeType::WORD: return "WORD";
  case TypeType::BOOL: return "BOOL";
  case TypeType::OBJ: return "OBJ";
  case TypeType::LAYOUT: return "LAYOUT";
  }
  return "???MISSING???";
}

const char *ExpTypeString(ExpType t) {
  switch (t) {
  case ExpType::STRING: return "STRING";
  case ExpType::FLOAT: return "FLOAT";
  case ExpType::NODE: return "NODE";
  case ExpType::RECORD: return "RECORD";
  case ExpType::OBJECT: return "OBJECT";
  case ExpType::INT: return "INT";
  case ExpType::BOOL: return "BOOL";
  case ExpType::WORD: return "WORD";
  case ExpType::VAR: return "VAR";
  case ExpType::GLOBAL_SYM: return "GLOBAL_SYM";
  case ExpType::LAYOUT: return "LAYOUT";
  case ExpType::LET: return "LET";
  case ExpType::IF: return "IF";
  case ExpType::APP: return "APP";
  case ExpType::FN: return "FN";
  case ExpType::PROJECT: return "PROJECT";
  case ExpType::INJECT: return "INJECT";
  case ExpType::ROLL: return "ROLL";
  case ExpType::UNROLL: return "UNROLL";
  case ExpType::PRIMAPP: return "PRIMAPP";
  case ExpType::FAIL: return "FAIL";
  case ExpType::SEQ: return "SEQ";
  case ExpType::INTCASE: return "INTCASE";
  case ExpType::WORDCASE: return "WORDCASE";
  case ExpType::STRINGCASE: return "STRINGCASE";
  case ExpType::SUMCASE: return "SUMCASE";
  case ExpType::PACK: return "PACK";
  case ExpType::UNPACK: return "UNPACK";
  case ExpType::TYPEFN: return "TYPEFN";
  case ExpType::TYPEAPP: return "TYPEAPP";
  case ExpType::HAS: return "HAS";
  case ExpType::GET: return "GET";
  case ExpType::WITH: return "WITH";
  case ExpType::WITHOUT: return "WITHOUT";
  }
  return "???MISSING???";
}

const char *ObjFieldTypeString(ObjFieldType oft) {
  switch (oft) {
  case ObjFieldType::STRING: return "STRING";
  case ObjFieldType::FLOAT: return "FLOAT";
  case ObjFieldType::INT: return "INT";
  case ObjFieldType::BOOL: return "BOOL";
  case ObjFieldType::OBJ: return "OBJ";
  case ObjFieldType::LAYOUT: return "LAYOUT";
  }
  return "??MISSING??";
}

std::string AstPool::NewVar(const std::string &hint_in) {
  next_var++;
  std::string hint = BaseVar(hint_in);
  if (hint.empty()) {
    return std::format("x${}", next_var);
  } else {
    return std::format("{}${}", hint, next_var);
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
    record_type.emplace_back(std::format("{}", i + 1), v[i]);
  }
  return RecordType(record_type, guess);
}

std::string TypeString(const Type *t) {
  auto RecordOrSumBody = [](
      const std::vector<std::pair<std::string, const Type *>> &v) {
      std::string ret;
      for (int i = 0; i < (int)v.size(); i++) {
        const auto &[lab, child] = v[i];
        if (i != 0) ret.append(", ");
        AppendFormat(&ret, "{}: {}", lab, TypeString(child));
      }
      return ret;
    };

  auto GetTupleString =
    [](const std::vector<std::pair<std::string, const Type *>> &v) ->
      std::optional<std::string> {
    if (v.size() <= 1) return std::nullopt;
    // Note that this assumes labels are sorted in numeric order, i.e. not "10"
    // after "1".
    for (int i = 0; i < (int)v.size(); i++) {
      const std::string &lab = v[i].first;
      if (lab != std::format("{}", i + 1)) {
        return std::nullopt;
      }
    }

    // Then it is a tuple.
    std::vector<std::string> parts;
    parts.reserve(v.size());
    for (const auto &[k_, t] : v) {
      parts.push_back(TypeString(t));
    }

    return std::format("({})", Util::Join(parts, " * "));
  };

  switch (t->type) {
  case TypeType::VAR: {
    const auto &[var, args] = t->Var();
    switch (args.size()) {
    case 0:
      return var;
    case 1:
      return std::format("{} {}",
                         TypeString(args[0]),
                         var);
    default: {
      std::string sargs;
      for (int i = 0; i < (int)args.size(); i++) {
        if (i != 0) sargs.append(", ");
        sargs.append(TypeString(args[i]));
      }
      return std::format("({}) {}", sargs, var);
    }
    }
  }

  case TypeType::ARROW: {
    const auto &[dom, cod] = t->Arrow();
    return std::format("({} → {})",
                       TypeString(dom),
                       TypeString(cod));
  }

  case TypeType::RECORD: {
    // Since these are used in error message, special case unit and
    // tuple types.
    const std::vector<std::pair<std::string, const Type *>> &fields = t->Record();
    if (fields.empty()) return "unit";
    std::optional<std::string> to = GetTupleString(fields);
    if (to.has_value()) return to.value();
    return std::format("{{" "{}" "}}", RecordOrSumBody(fields));
  }

  case TypeType::SUM:
    // Might want to special case void?
    return std::format("[{}]", RecordOrSumBody(t->Sum()));

  case TypeType::MU: {
    const auto &[idx, bundle] = t->Mu();
    CHECK(!bundle.empty());
    if (bundle.size() == 1) {
      return std::format("(μ {}. {})",
                         bundle[0].first,
                         TypeString(bundle[0].second));
    } else {
      std::string ret = std::format("#{}(μ", idx);
      for (int i = 0; i < (int)bundle.size(); i++) {
        if (i != 0) ret.append(";");
        const auto &[v, c] = bundle[i];
        AppendFormat(&ret,
                     " {}={}. {}", i, v, TypeString(c));
      }
      ret.append(")");
      return ret;
    }
  }

  case TypeType::EXISTS: {
    const auto &[alpha, body] = t->Exists();
    return std::format("∃ {}.{}\n", alpha,
                       TypeString(body));
  }

  case TypeType::FORALL: {
    const auto &[alpha, body] = t->Forall();
    return std::format("∀ {}.{}\n", alpha,
                       TypeString(body));
  }

  case TypeType::EVAR: {
    const auto &evar = t->EVar();
    if (const Type *u = evar.GetBound()) {
      // We treat bound evars transparently, although for some
      // uses it might be good to be able to see it?
      return TypeString(u);
    } else {
      return evar.ToString();
    }
  }

  case TypeType::REF:
    return std::format("({} ref)", TypeString(t->Ref()));

  case TypeType::VEC:
    return std::format("(%s vec)", TypeString(t->Vec()));

  case TypeType::STRING:
    return "string";

  case TypeType::INT:
    return "int";

  case TypeType::FLOAT:
    return "float";

  case TypeType::WORD:
    return "word";

  case TypeType::BOOL:
    return "bool";

  case TypeType::OBJ:
    return "obj";

  case TypeType::LAYOUT:
    return "layout";

  default:
    return std::format("unknown typetype {}??",
                       TypeTypeString(t->type));
  }
}

// TODO: Some kind of pretty-printing
std::string ExpString(const Exp *e) {
  switch (e->type) {
  case ExpType::STRING:
    // XXX escaping
    return std::format("\"{}\"", e->String());

  case ExpType::VAR: {
    const auto &[types, x] = e->Var();
    if (types.empty()) {
      return x;
    } else {
      std::string args;
      for (int i = 0; i < (int)types.size(); i++) {
        if (i != 0) args.append(", ");
        args.append(TypeString(types[i]));
      }
      return std::format("{}<{}>", x, args);
    }
  }

  case ExpType::GLOBAL_SYM: {
    const auto &[types, sym] = e->GlobalSym();
    std::string x = std::format("global.{}", sym);
    if (types.empty()) {
      return x;
    } else {
      std::string args;
      for (int i = 0; i < (int)types.size(); i++) {
        if (i != 0) args.append(", ");
        args.append(TypeString(types[i]));
      }
      return std::format("{}<{}>", x, args);
    }
  }

  case ExpType::FN: {
    const auto &[self, x, t, body] = e->Fn();
    const std::string as =
      self.empty() ? "" : std::format(" as {}", self);
    return std::format("(fn{} {} : ({}) => {})",
                        as, x,
                        TypeString(t),
                        ExpString(body));
  }

  case ExpType::INT:
    return e->Int().ToString();

  case ExpType::WORD:
    return std::format("(word {})", e->Word());

  case ExpType::BOOL:
    return e->Bool() ? "true" : "false";

  case ExpType::FLOAT:
    return std::format("{:.17g}", e->Float());

  case ExpType::RECORD: {
    const auto &fields = e->Record();
    std::string ret = "{";
    for (int i = 0; i < (int)fields.size(); i++) {
      if (i != 0) ret.append(", ");
      const auto &[l, v] = fields[i];
      AppendFormat(&ret, "{} = {}", l, ExpString(v));
    }
    ret += "}";
    return ret;
  }

  case ExpType::OBJECT: {
    const auto &fields = e->Object();
    std::string ret = "{() ";
    for (int i = 0; i < (int)fields.size(); i++) {
      if (i != 0) ret.append(", ");
      const auto &[l, oft, v] = fields[i];
      AppendFormat(&ret, "{}[{}] = {}", l,
                   ObjFieldTypeString(oft),
                   ExpString(v));
    }
    ret += "}";
    return ret;
  }

  case ExpType::WITH: {
    const auto &[obj, field, oft, rhs] = e->With();
    return std::format("({} with {}:{} = {})",
                       ExpString(obj),
                       field,
                       ObjFieldTypeString(oft),
                       ExpString(rhs));
  }

  case ExpType::WITHOUT: {
    const auto &[obj, field, oft] = e->Without();
    return std::format("({} without {}:{})",
                       ExpString(obj),
                       field,
                       ObjFieldTypeString(oft));
  }

  case ExpType::PROJECT: {
    const auto &[lab, t, r] = e->Project();
    return std::format("#{}/{}({})",
                       lab, TypeString(t), ExpString(r));
  }

  case ExpType::INJECT: {
    const auto &[lab, t, r] = e->Inject();
    return std::format("([{} = {}] : {})",
                        lab, ExpString(r),
                        TypeString(t));
  }

  case ExpType::ROLL: {
    const auto &[t, child] = e->Roll();
    return std::format("roll<{}>({})",
                       TypeString(t),
                       ExpString(child));
  }

  case ExpType::UNROLL: {
    const auto &[child, t] = e->Unroll();
    return std::format("unroll<{}>({})",
                       TypeString(t),
                       ExpString(child));
  }

  case ExpType::NODE: {
    const auto &[attrs, children] = e->Node();
    std::string ret = std::format("[{}| ", ExpString(attrs));
    for (int i = 0; i < (int)children.size(); i++) {
      if (i != 0) ret.append(", ");
      ret.append(ExpString(children[i]));
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
    return std::format("let val {}{} = {}\n"
                       "in {}\n"
                       "end",
                       tyvars,
                       x,
                       ExpString(rhs),
                       ExpString(body));
  }

  case ExpType::IF: {
    const auto &[cond, t, f] = e->If();
    return std::format("(if {} then {} else {})",
                       ExpString(cond),
                       ExpString(t),
                       ExpString(f));
  }

  case ExpType::APP: {
    const auto &[f, x] = e->App();
    return std::format("({} {})",
                       ExpString(f),
                       ExpString(x));
  }

  case ExpType::PRIMAPP: {
    const auto &[po, types, children] = e->Primapp();

    std::string targs;
    for (int i = 0; i < (int)types.size(); i++) {
      if (i != 0) targs.append(", ");
      targs.append(TypeString(types[i]));
    }
    if (!types.empty()) targs = std::format("<{}>", targs);

    std::string args;
    for (int i = 0; i < (int)children.size(); i++) {
      if (i != 0) args.append(", ");
      args.append(ExpString(children[i]));
    }
    return std::format("{}{}({})", PrimopString(po), targs, args);
  }

  case ExpType::FAIL: {
    const auto &[msg, t] = e->Fail();
    return std::format("fail<{}> {}",
                       TypeString(t),
                       ExpString(msg));
  }

  case ExpType::SEQ: {
    const auto &[es, body] = e->Seq();
    std::vector<std::string> v;

    std::string ret = "seq(\n";
    for (const Exp *c : es)
      AppendFormat(&ret, "  {};\n", ExpString(c));
    AppendFormat(&ret, "  {})", ExpString(body));
    return ret;
  }

  case ExpType::INTCASE: {
    const auto &[obj, arms, def] = e->IntCase();
    std::vector<std::string> sarms;
    for (const auto &[bi, arm] : arms)
      sarms.push_back(std::format("{} => {}",
                                  bi.ToString(),
                                  ExpString(arm)));
    return std::format("intcase {} of\n"
                       "   {}\n"
                       " | _ => {}",
                       ExpString(obj),
                       Util::Join(sarms, "\n | "),
                       ExpString(def));
  }

  case ExpType::WORDCASE: {
    const auto &[obj, arms, def] = e->WordCase();
    std::vector<std::string> sarms;
    for (const auto &[w, arm] : arms)
      sarms.push_back(std::format("{} => {}",
                                  w,
                                  ExpString(arm)));
    return std::format("wordcase {} of\n"
                       "   {}\n"
                       " | _ => {}",
                       ExpString(obj),
                       Util::Join(sarms, "\n | "),
                       ExpString(def));
  }

  case ExpType::STRINGCASE: {
    const auto &[obj, arms, def] = e->StringCase();
    std::vector<std::string> sarms;
    for (const auto &[s, arm] : arms)
      sarms.push_back(std::format("\"{}\" => {}",
                                   EscapeString(s),
                                   ExpString(arm)));
    return std::format("stringcase {} of\n"
                       "   {}\n"
                       " | _ => {}",
                       ExpString(obj),
                       Util::Join(sarms, "\n | "),
                       ExpString(def));
  }

  case ExpType::SUMCASE: {
    const auto &[obj, arms, def] = e->SumCase();
    std::vector<std::string> sarms;
    for (const auto &[s, x, arm] : arms)
      sarms.push_back(std::format("{} ({}) => {}",
                                  s, x,
                                  ExpString(arm)));
    return std::format("sumcase {} of\n"
                       "   {}\n"
                       " | _ => {}",
                       ExpString(obj),
                       Util::Join(sarms, "\n | "),
                       ExpString(def));
  }

  case ExpType::PACK: {
    const auto &[t_hidden, alpha, t_packed, exp] = e->Pack();
    return std::format("pack {} as {}.{}\n"
                       "of {} end",
                       TypeString(t_hidden),
                       alpha,
                       TypeString(t_packed),
                       ExpString(exp));
  }

  case ExpType::UNPACK: {
    const auto &[alpha, x, rhs, body] = e->Unpack();
    // unpack α,x = rhs
    // in body
    return std::format("unpack {},{} = {}\n"
                       "in {} end",
                       alpha, x,
                       ExpString(rhs),
                       ExpString(body));
  }

  case ExpType::TYPEFN: {
    const auto &[alpha, exp] = e->TypeFn();
    return std::format("typefn {} => {}",
                       alpha, ExpString(exp));
  }

  case ExpType::TYPEAPP: {
    const auto &[exp, t] = e->TypeApp();
    return std::format("{}<{}>",
                       ExpString(exp), TypeString(t));
  }

  case ExpType::HAS: {
    const auto &[obj, field, oft] = e->Has();
    return std::format("(has {}.{} : {})",
                       ExpString(obj),
                       field,
                       ObjFieldTypeString(oft));
  }

  case ExpType::GET: {
    const auto &[obj, field, oft] = e->Get();
    return std::format("(get {}.{} : {})",
                       ExpString(obj),
                       field,
                       ObjFieldTypeString(oft));
  }

  default:
    return "ILLEGAL EXPRESSION";
  }
}

std::pair<std::vector<std::string>, std::vector<const Type *>>
AstPool::AlphaVaryMultipleTypes(
    const std::vector<std::string> &xv,
    const std::vector<const Type *> &tv) {
  std::vector<std::string> xxv;
  xxv.reserve(xv.size());
  std::vector<const Type *> ttv = tv;
  for (const std::string &x : xv) {
    const std::string xx = NewVar(x);
    xxv.push_back(xx);
    const Type *xxt = VarType(xx, {});
    for (int i = 0; i < (int)ttv.size(); i++) {

      // [x'/x]t, where we assert x' does not appear in t.
      ttv[i] = SubstTypeInternal(xxt, x, ttv[i], true);
    }
  }

  return std::make_pair(std::move(xxv), std::move(ttv));
}

const std::pair<std::string, const Type *>
AstPool::AlphaVaryType(const std::string &x, const Type *t) {
  std::string xx = NewVar(x);
  // [x'/x]t, where we assert x' does not appear in t.
  return std::make_pair(xx, SubstTypeInternal(VarType(xx, {}), x, t, true));
}

const Type *AstPool::UnrollType(const Type *mu) {
  if (VERBOSE) {
    Print(AORANGE("Unroll") " {}\n", TypeString(mu));
  }
  CHECK(mu->type == TypeType::MU);
  const auto &[idx, bundles] = mu->Mu();
  CHECK(idx >= 0 && idx < (int)bundles.size());
  const Type *ret = bundles[idx].second;
  // All type variables are bound recursively, so substitute them all.
  for (int i = 0; i < (int)bundles.size(); i++) {
    const auto &[a, t] = bundles[i];
    const Type *mu_i = Mu(i, bundles);
    ret = SubstType(mu_i, a, ret);
  }
  return ret;
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

    // All of the variables are bound in all of the arms. So if
    // any of them match the target variable, it is shadowed
    // and cannot occur.
    for (const auto &[a, body] : bundle) {
      if (a == v) return u;
    }

    std::vector<std::pair<std::string, const Type *>> new_bundle;
    new_bundle.reserve(bundle.size());

    if (is_simple) {
      for (const auto &[a, body] : bundle) {
        new_bundle.emplace_back(
            a, SubstTypeInternal(t, v, body, is_simple));
      }
      return Mu(idx, new_bundle, u);

    } else {

      // PERF: Only do this if a occurs in t and would cause capture.
      // Also, we could probably get rid of the complexity of is_simple
      // if we did an explicit check, since we would avoid the
      // infinite regress by construction (the fresh variable will not
      // appear).
      std::vector<std::string> aa;
      std::vector<const Type *> bb;
      for (const auto &[a, body] : bundle) {
        aa.push_back(a);
        bb.push_back(body);
      }

      const auto &[aaa, bbb] = AlphaVaryMultipleTypes(aa, bb);

      CHECK(aaa.size() == bbb.size());
      for (int i = 0; i < (int)aaa.size(); i++) {
        new_bundle.emplace_back(aaa[i],
                                SubstTypeInternal(t, v, bbb[i], is_simple));
      }

      return Mu(idx, new_bundle, u);
    }
  }

  case TypeType::EXISTS: {
    const auto &[alpha, body] = u->Exists();
    if (alpha == v) {
      // shadowed; can't occur.
      return u;
    } else {
      // PERF: As above, don't always rename.
      if (is_simple) {
        return Exists(alpha, SubstTypeInternal(t, v, body, is_simple));
      } else {
        const auto &[new_alpha, new_body] = AlphaVaryType(alpha, body);
        return Exists(new_alpha, SubstTypeInternal(t, v, new_body, is_simple));
      }
    }
  }

  case TypeType::FORALL: {
    const auto &[alpha, body] = u->Forall();
    if (alpha == v) {
      // shadowed; can't occur.
      return u;
    } else {
      // PERF: As above, don't always rename.
      if (is_simple) {
        return Forall(alpha, SubstTypeInternal(t, v, body, is_simple));
      } else {
        const auto &[new_alpha, new_body] = AlphaVaryType(alpha, body);
        return Forall(new_alpha, SubstTypeInternal(t, v, new_body, is_simple));
      }
    }
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

  case TypeType::VEC:
    return VecType(SubstTypeInternal(t, v, u->a, is_simple), u);

  case TypeType::STRING:
    return u;

  case TypeType::INT:
    return u;

  case TypeType::FLOAT:
    return u;

  case TypeType::WORD:
    return u;

  case TypeType::BOOL:
    return u;

  case TypeType::OBJ:
    return u;

  case TypeType::LAYOUT:
    return u;
  }
  LOG(FATAL) << "Unimplemented typetype in subst: "
             << TypeTypeString(u->type);
  return nullptr;
}

std::string ProgramString(const Program &pgm) {
  std::string ret = "globals\n";
  for (const Global &glob : pgm.globals) {
    std::string tyvars;
    if (!glob.tyvars.empty()) {
      tyvars = std::format("({}) ", Util::Join(glob.tyvars, ","));
    }
    AppendFormat(&ret, "  global {}{} : {} = {}\n",
                  tyvars,
                  glob.sym,
                  TypeString(glob.type),
                  ExpString(glob.exp));
  }
  AppendFormat(&ret,
               "in\n"
               "  {}\n"
               "end\n", ExpString(pgm.body));
  return ret;
}

template<class T, class F>
TypeCmp::Order static OrderVector(const std::vector<T> &av,
                                  const std::vector<T> &bv,
                                  const F &f) {
  if (av.size() == bv.size()) {
    for (size_t i = 0; i < av.size(); i++) {
      TypeCmp::Order ord = f(av[i], bv[i]);
      if (ord != TypeCmp::Order::EQ) return ord;
    }
    return TypeCmp::Order::EQ;

  } else if (av.size() < bv.size()) {
    return TypeCmp::Order::LESS;
  } else {
    return TypeCmp::Order::GREATER;
  }
}


TypeCmp::Order TypeCmp::Compare(const Type *a, const Type *b) {
  if (a == b) return Order::EQ;

  if (a->type == TypeType::EVAR) {
    a = a->EVar().GetBound();
    CHECK(a != nullptr) << "EVar (lhs) unset in TypeCmp.";
  }

  if (b->type == TypeType::EVAR) {
    b = b->EVar().GetBound();
    CHECK(b != nullptr) << "EVar (rhs) unset in TypeCmp.";
  }

  if (a->type != b->type) {
#   define ORDER(ctor) do {                                        \
      if (a->type == TypeType:: ctor ) return Order::LESS;         \
      if (b->type == TypeType:: ctor ) return Order::GREATER;      \
    } while (0)

    ORDER(VAR);
    ORDER(SUM);
    ORDER(ARROW);
    ORDER(MU);
    ORDER(EXISTS);
    ORDER(FORALL);
    ORDER(RECORD);
    ORDER(REF);
    ORDER(VEC);
    ORDER(STRING);
    ORDER(FLOAT);
    ORDER(INT);
    ORDER(BOOL);
    ORDER(WORD);
    ORDER(OBJ);
    ORDER(LAYOUT);
#   undef ORDER

    LOG(FATAL) << "Unhandled TypeType (or illegal evar?) in "
      "TypeCmp where a->type != b->type: " <<
      TypeString(a) << "\nvs\n" << TypeString(b);
  }

  DCHECK(a->type == b->type);
  switch (a->type) {
  case TypeType::VAR: {
    const auto &[ax, av] = a->Var();
    const auto &[bx, bv] = a->Var();
    if (ax == bx) {
      // const std::vector<const Type *>
      return OrderVector(av, bv,
                         [](const auto &aa, const auto &bb) {
                           return Compare(aa, bb);
                         });
    } else if (ax < bx) {
      return Order::LESS;
    } else {
      return Order::GREATER;
    }

    break;
  }

  case TypeType::SUM:
    // Sum fields are sorted by label.
    return OrderVector(a->Sum(), b->Sum(),
                       [](const auto &aa, const auto &bb) {
                         if (aa.first == bb.first) {
                           return Compare(aa.second, bb.second);
                         } else if (aa.first < bb.first) {
                           return Order::LESS;
                         } else {
                           return Order::GREATER;
                         }
                       });
    break;

  case TypeType::ARROW: {
    const auto &[a1, a2] = a->Arrow();
    const auto &[b1, b2] = b->Arrow();
    const auto ord = Compare(a1, b1);
    if (ord == Order::EQ) return Compare(a2, b2);
    else return ord;
  }

  case TypeType::MU: {
    const auto &[an, av] = a->Mu();
    const auto &[bn, bv] = b->Mu();
    if (an == bn) {
      return OrderVector(av, bv,
                         [](const auto &aa, const auto &bb) {
                           const auto &[ax, at] = aa;
                           const auto &[bx, bt] = bb;
                           if (ax == bx) {
                             return Compare(at, bt);
                           } else if (ax < bx) {
                             return Order::LESS;
                           } else {
                             return Order::GREATER;
                           }
                         });
    } else if (an < bn) {
      return Order::LESS;
    } else {
      return Order::GREATER;
    }
    break;
  }

  case TypeType::EXISTS: {
    const auto &[ax, at] = a->Exists();
    const auto &[bx, bt] = b->Exists();
    if (ax == bx) {
      return Compare(at, bt);
    } else if (ax < bx) {
      return Order::LESS;
    } else {
      return Order::GREATER;
    }

    break;
  }

  case TypeType::FORALL: {
    const auto &[ax, at] = a->Forall();
    const auto &[bx, bt] = b->Forall();
    if (ax == bx) {
      return Compare(at, bt);
    } else if (ax < bx) {
      return Order::LESS;
    } else {
      return Order::GREATER;
    }

    break;
  }

  case TypeType::RECORD:
    // Record fields are sorted by label.
    return OrderVector(a->Record(), b->Record(),
                       [](const auto &aa, const auto &bb) {
                         if (aa.first == bb.first) {
                           return Compare(aa.second, bb.second);
                         } else if (aa.first < bb.first) {
                           return Order::LESS;
                         } else {
                           return Order::GREATER;
                         }
                       });
    break;

  case TypeType::EVAR:
    LOG(FATAL) << "Bug: EVars handled above.";
  case TypeType::REF: return Compare(a->Ref(), b->Ref());
  case TypeType::VEC: return Compare(a->Ref(), b->Ref());
  case TypeType::STRING: return Order::EQ;
  case TypeType::FLOAT: return Order::EQ;
  case TypeType::INT: return Order::EQ;
  case TypeType::BOOL: return Order::EQ;
  case TypeType::WORD: return Order::EQ;
  case TypeType::OBJ: return Order::EQ;
  case TypeType::LAYOUT: return Order::EQ;
  }

  LOG(FATAL) << "Unhandled TypeType in TypeCmp where "
    "a->type == b->type: " <<
    TypeString(a) << "\nvs\n" << TypeString(b);
  return Order::LESS;
}

bool TypeVecCmp::operator()(const std::vector<const Type *> &av,
                            const std::vector<const Type *> &bv) const {
  return OrderVector(av, bv, [](const Type *a, const Type *b) {
      return TypeCmp::Compare(a, b);
    }) == TypeCmp::Order::LESS;
}


}  // namespace il
