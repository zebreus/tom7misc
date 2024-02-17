
// TODO: Don't allow constructors to be shadowed by regular variables.

#include "elaboration.h"

#include <utility>
#include <string>
#include <unordered_set>

#include "el.h"
#include "il.h"
#include "initial.h"
#include "pattern-compilation.h"
#include "il-util.h"

#include "base/stringprintf.h"

// This code has to mention both el and il stuff with the same
// name. But there are many things that are unambiguous.
using Context = il::Context;
using VarInfo = il::VarInfo;
using Unification = il::Unification;
using TypeVarInfo = il::TypeVarInfo;
using EVar = il::EVar;
using PatternCompilation = il::PatternCompilation;
using Program = il::Program;
using ILUtil = il::ILUtil;

using DatatypeDec = el::DatatypeDec;

Elaboration::Elaboration(el::AstPool *el_pool, il::AstPool *il_pool) :
  el_pool(el_pool), pool(il_pool), init(pool) {
  fail_match = pool->Fail(pool->String("match"));
  pattern_compilation.reset(new PatternCompilation(this));
}

Elaboration::~Elaboration() {}

std::pair<const il::Exp *, const il::Type *> Elaboration::FailMatch() {
  return std::make_pair(fail_match, NewEVar());
}

const il::Type *Elaboration::NewEVar() {
  return pool->EVar(EVar());
}

Program Elaboration::Elaborate(const el::Exp *el_exp) {
  Context G = init.InitialContext();

  const auto &[e, t] = Elab(G, el_exp);

  // Should check that the program has type layout?
  if (verbose > 0) {
    printf("Program type: %s\n", TypeString(t).c_str());
  }

  Program pgm;
  pgm.globals = std::move(globals);
  globals.clear();
  pgm.body = e;
  return pgm;
}

const il::Type *Elaboration::ElabType(const Context &G,
                                      const el::Type *el_type) {
  switch (el_type->type) {
  case el::TypeType::VAR: {
    const TypeVarInfo *k = G.FindType(el_type->var);
    CHECK(k != nullptr) << "Unbound (type) variable: " << el_type->var;

    const il::Type *t = k->type;
    CHECK(k->tyvars.size() == el_type->children.size()) <<
      "Type constructor '" << el_type->var << "' applied to the "
      "wrong number of arguments (want " << k->tyvars.size() <<
      "; got " << el_type->children.size() << ").\nIn:\n" <<
      el::TypeString(el_type);

    for (int i = 0; i < (int)k->tyvars.size(); i++) {
      const std::string &v = k->tyvars[i];
      const il::Type *u = ElabType(G, el_type->children[i]);
      t = pool->SubstType(u, v, t);
    }

    return t;
  }

  case el::TypeType::ARROW: {
    const il::Type *dom = ElabType(G, el_type->a);
    const il::Type *cod = ElabType(G, el_type->b);
    return pool->Arrow(dom, cod);
  }

  case el::TypeType::PRODUCT: {
    // Translate into the equivalent record.
    std::vector<std::pair<std::string, const il::Type *>> rec;
    rec.reserve(el_type->children.size());
    for (int i = 0; i < (int)el_type->children.size(); i++) {
      const el::Type *t = el_type->children[i];
      const il::Type *tt = ElabType(G, t);
      rec.emplace_back(StringPrintf("%d", i + 1), tt);
    }
    return pool->RecordType(std::move(rec));
  }

  case el::TypeType::RECORD: {
    std::vector<std::pair<std::string, const il::Type *>> rec;
    rec.reserve(el_type->str_children.size());
    for (const auto &[lab, t] : el_type->str_children) {
      const il::Type *tt = ElabType(G, t);
      rec.emplace_back(lab, tt);
    }
    return pool->RecordType(std::move(rec));
  }
  }
}

// XXX return context instead of requiring let?
const std::pair<const il::Exp *, const il::Type *> Elaboration::ElabDecs(
    const il::Context &G,
    const std::vector<const el::Dec *> &decs,
    const el::Exp *el_body) {
  if (decs.empty()) {
    return Elab(G, el_body);
  } else {
    const el::Dec *dec = decs[0];
    std::vector<const el::Dec *> rest_decs;
    rest_decs.reserve(decs.size() - 1);
    for (int i = 1; i < (int)decs.size(); i++) {
      rest_decs.push_back(decs[i]);
    }

    const el::Exp *rest = rest_decs.empty() ? el_body :
      el_pool->Let(std::move(rest_decs), el_body);

    switch (dec->type) {
    case el::DecType::VAL:
      // Only irrefutable patterns are allowed in a val decl.

      // Need to generalize if free evars.
      // All this is handled in the pattern compiler.
      return pattern_compilation->CompileIrrefutable(
          G, dec->pat, dec->exp, rest);

    case el::DecType::FUN:
      // TODO: Support curried notation.
      CHECK(!dec->funs.empty()) << "Bug: Should not parse empty funs.";
      if (dec->funs.size() == 1) {
        const el::FunDec &fun = dec->funs[0];
        // If not mutually recursive, do this the easy way.
        return pattern_compilation->CompileIrrefutable(
            G,
            el_pool->VarPat(fun.name),
            el_pool->Fn(fun.name, fun.clauses),
            rest);
      } else {
        // For mutually recursive functions, we hoist them out to
        // globals so that they can reference each other. First,
        // type-check and translate the bodies.
        std::vector<std::string> il_vars;
        std::vector<std::pair<const il::Type *, const il::Type *>> dom_cods;
        Context GG = G;
        for (const el::FunDec &fun : dec->funs) {
          std::string il_var = pool->NewVar(fun.name);
          il_vars.push_back(il_var);
          const il::Type *dom = NewEVar();
          const il::Type *cod = NewEVar();
          dom_cods.emplace_back(dom, cod);
          GG = GG.Insert(fun.name, VarInfo{
              // We only support uniform recursion, so each function
              // will have just one type here; if there are free evars
              // at the end, those are the only ones we abstract over.
              .tyvars = {},
              .type = pool->Arrow(dom, cod),
              .var = il_var,
            });
        }

        std::vector<std::pair<const il::Exp *, const il::Type *>> fns;
        for (int i = 0; i < (int)dec->funs.size(); i++) {
          const el::FunDec &fun = dec->funs[i];
          // In the generated code we could perform the recursion
          // through the fn's recursive name or through the global.
          // The fn name is a little better because its type variables
          // are already instantiated, but if there ends up being a
          // reason to prefer the global, it would be easy to switch
          // it right here.
          const auto &[e, t] =
            Elab(GG, el_pool->Fn(il_vars[i], fun.clauses));
          const auto &[dom, cod] = dom_cods[i];
          Unification::Unify("fun..and decl", pool->Arrow(dom, cod), t);
          fns.emplace_back(e, t);
        }

        // The declared functions may mention local variables that prevent
        // them from being hoisted to top-level. These present as
        // free variables that are not in the mutually-recursive bundle
        // and are not globals.
        for (int i = 0; i < (int)dec->funs.size(); i++) {
          const auto &[oexp, otype] = fns[i];
          std::unordered_set<std::string> fvs =
            ILUtil::FreeExpVars(oexp);

          // FIXME: We actually need to use one environment for the
          // whole bundle. So do this same thing, but for all fns at once.

          // The other functions in the bundle will look syntactically
          // free, but they are being bound as part of this declaration.
          // Remove them from the set.
          for (const std::string &ilv : il_vars) {
            fvs.erase(ilv);
          }

          // If the function is f : d->c = (fn x => e), with free variables
          // x1 : t1, x2 : t2, ... then we generate the global
          // global_f : (t1*t2*...) -> d -> c =
          //    (fn (x1=x1, x2=x2, ...) => fn x => e)

          // The free variables must be bound in the context, so get
          // those types.
          std::vector<std::pair<std::string, const il::Type *>> fvts;
          for (const std::string &s : fvs) {
            const VarInfo *vi = G.Find(s);
            CHECK(vi != nullptr) << "Bug: When compiling a "
              "mutually-recursive bundle of functions (fun...and), "
              "found a free variable " << s << " that's not bound in "
              "the context.\n";
            CHECK(vi->type != nullptr) << "Bug: Maybe this happens "
              "for primops or ctors?";
            fvts.emplace_back(s, vi->type);
          }

          std::sort(fvts.begin(), fvts.end(),
                    [](const std::pair<std::string, const il::Type *> &a,
                       const std::pair<std::string, const il::Type *> &b) {
                      return a.first < b.first;
                    });

          // Now the curried function. Unpack the argument record; the
          // labels have the same names as the variables for convenience.
          // This means that the record type is just the fvts set we just
          // created.
          const il::Type *env_type = pool->RecordType(fvts);
          const il::Exp *gbody = oexp;
          const std::string gx = pool->NewVar("env");
          const il::Exp *gxv = pool->Var({}, gx);
          for (const auto &[fv, t] : fvts) {
            gbody = pool->Let({}, fv, pool->Project(fv, gxv), gbody);
          }


          const il::Exp *glob_fn = pool->Fn("", gx, gbody);
          const il::Type *glob_typ = pool->Arrow(env_type, otype);

          LOG(FATAL) << "Oops this is not right. See remark above.";
        }

        LOG(FATAL) << "Unimplemented: Mutually-recursive FUN";
      }
      break;

    case el::DecType::DATATYPE: {
      {
        std::unordered_set<std::string> unique_tyvars;
        for (const std::string &eltv : dec->tyvars) {
          CHECK(!unique_tyvars.contains(eltv)) << "Duplicate type "
            "variable " << eltv << " in datatype declaration.";
          unique_tyvars.insert(eltv);
        }

        std::unordered_set<std::string> unique_names;
        std::unordered_set<std::string> unique_ctors;
        for (const DatatypeDec &dd : dec->datatypes) {
          CHECK(!unique_names.contains(dd.name)) << "Duplicate datatype " <<
            dd.name << " in mutually recurisve datatype declaration.";
          unique_names.insert(dd.name);
          for (const auto &[ctor, t_] : dd.arms) {
            CHECK(!unique_ctors.contains(ctor)) << "Duplicate constructor " <<
              ctor << " in mutually recursive datatype declaration. The "
              "constructors must be distinct across all datatypes.";
            unique_ctors.insert(ctor);
          }
        }
      }

      // Generate
      std::vector<std::pair<std::string, std::string>> tyvars;
      std::vector<std::string> il_tyvars;
      for (const std::string &eltv : dec->tyvars) {
        std::string iltv = pool->NewVar(eltv);
        tyvars.emplace_back(eltv, iltv);
        il_tyvars.emplace_back(iltv);
      }

      // Bind tyvars: The explicit type variables written by the
      // programmer.
      Context GG = G;
      for (const auto &[eltv, iltv] : tyvars) {
        GG = GG.InsertType(eltv, TypeVarInfo{.type = pool->VarType(iltv, {})});
      }

      // Bind the muvars: One recursive variable for each datatype
      // in the bundle.
      std::vector<std::pair<std::string, std::string>> recvars;
      for (const DatatypeDec &dd : dec->datatypes) {
        recvars.emplace_back(dd.name, pool->NewVar(dd.name));
      }

      for (const auto &[eltv, iltv] : recvars) {
        GG = GG.InsertType(eltv, TypeVarInfo{.type = pool->VarType(iltv, {})});
      }

      // Each arm of the mu is the bound recursive type variable (il)
      // and the type, which is a sum.
      std::vector<std::pair<std::string, const il::Type *>> sum_types;
      // For each datatype declaration, for each arm, its arg type ("of ...").
      // This is used below to bind the constructors.
      std::vector<std::vector<const il::Type *>> oftypes;
      for (int i = 0; i < (int)dec->datatypes.size(); i++) {
        const DatatypeDec &dd = dec->datatypes[i];
        // Construct the sum over all arms of this datatype.
        std::vector<std::pair<std::string, const il::Type *>> sum_arms;
        oftypes.push_back({});
        for (const auto &[ctor, el_type] : dd.arms) {
          const il::Type *cod = ElabType(GG, el_type);
          sum_arms.emplace_back(ctor, cod);
          oftypes.back().push_back(cod);
        }
        sum_types.emplace_back(recvars[i].second, pool->SumType(sum_arms));
      }

      //      π_n (μ   v_0 . [ctor11: t11, ctor12: t12, ...]
      //           and v_1 . [ctor21: t21, ctor22: t22, ...]
      //           and ...)

      // Bind the datatypes themselves, e.g. 'list' and 'option'.
      // Each is a function over the same IL type variables,
      // Λ(tyvars). pi_n (... same mu body ...)
      std::vector<const il::Type *> mu_types;
      for (int i = 0; i < (int)dec->datatypes.size(); i++) {
        const DatatypeDec &dd = dec->datatypes[i];
        const il::Type *mu = pool->Mu(i, sum_types);
        mu_types.push_back(mu);
        GG = GG.InsertType(dd.name, TypeVarInfo{
            .tyvars = il_tyvars,
            .type = mu,
          });
      }

      // Now, bind the expression-level constructors.

      CHECK(oftypes.size() == dec->datatypes.size());
      CHECK(mu_types.size() == dec->datatypes.size());
      for (int y = 0; y < (int)dec->datatypes.size(); y++) {
        const DatatypeDec &dd = dec->datatypes[y];
        CHECK(oftypes[y].size() == dd.arms.size());
        const il::Type *mu_type = mu_types[y];
        for (int x = 0; x < (int)dd.arms.size(); x++) {
          const std::string &ctor = dd.arms[x].first;
          const il::Type *dom = oftypes[y][x];
          const il::Type *cod = mu_type;

          GG = GG.Insert(ctor, VarInfo{
              .tyvars = il_tyvars,
              .type = pool->Arrow(dom, cod),
              .ctor = std::make_optional(std::make_tuple(y, mu_type, ctor)),
            });
        }
      }

      // There are no actual declarations generated; all the bindings
      // are transparent. So just elaborate the body in the new context.
      return Elab(GG, rest);
    }

    default:;
    }

    LOG(FATAL) << "Unimplemented in ElabDecs";
    return std::make_pair(nullptr, nullptr);
  }
}


const std::pair<const il::Exp *, const il::Type *> Elaboration::Elab(
    const Context &G,
    const el::Exp *el_exp) {

  CHECK(el_exp != nullptr) << "Bug: null el_exp";

  switch (el_exp->type) {
  case el::ExpType::STRING:
    return std::make_pair(pool->String(el_exp->str),
                          pool->StringType());

  case el::ExpType::INTEGER:
    return std::make_pair(pool->Int(el_exp->integer),
                          pool->IntType());

  case el::ExpType::FLOAT:
    return std::make_pair(pool->Float(el_exp->d),
                          pool->FloatType());

  case el::ExpType::ANN: {
    // Type annotations are erased during elaboration, after
    // ensuring they hold through unification.
    const auto &[e, t] = Elab(G, el_exp->a);
    const il::Type *tann = ElabType(G, el_exp->t);
    Unification::Unify("type annotation", t, tann);
    return std::make_pair(e, t);
  }

  case el::ExpType::JOIN:
    LOG(FATAL) << "Unimplemented in elaboration: JOIN";
    break;

  case el::ExpType::TUPLE: {
    // This means a record expression with fields {1: e1, 2: e2, ...}.
    std::vector<std::pair<std::string, const il::Type *>> lct;
    std::vector<std::pair<std::string, const il::Exp *>> lce;
    lct.reserve(el_exp->children.size());
    lce.reserve(el_exp->children.size());

    for (int i = 0; i < (int)el_exp->children.size(); i++) {
      std::string lab = StringPrintf("%d", i + 1);
      const auto &[e, t] = Elab(G, el_exp->children[i]);
      lce.emplace_back(lab, e);
      lct.emplace_back(lab, t);
    }

    return std::make_pair(pool->Record(std::move(lce)),
                          pool->RecordType(std::move(lct)));
  }

  case el::ExpType::RECORD: {
    std::vector<std::pair<std::string, const il::Type *>> lct;
    std::vector<std::pair<std::string, const il::Exp *>> lce;
    lct.reserve(el_exp->str_children.size());
    lce.reserve(el_exp->str_children.size());

    for (const auto &[lab, child] : el_exp->str_children) {
      const auto &[e, t] = Elab(G, child);
      lce.emplace_back(lab, e);
      lct.emplace_back(lab, t);
    }

    return std::make_pair(pool->Record(std::move(lce)),
                          pool->RecordType(std::move(lct)));
  }

  case el::ExpType::VAR: {
    const il::VarInfo *vi = G.Find(el_exp->str);
    CHECK(vi != nullptr) << "Unbound variable: " << el_exp->str;

    // If the variable is polymorphic, then instantiate it with
    // evars.
    const il::Type *t = vi->type;
    std::vector<const il::Type *> tvs;
    tvs.reserve(vi->tyvars.size());
    for (int i = 0; i < (int)vi->tyvars.size(); i++) {
      const il::Type *w = NewEVar();
      tvs.push_back(w);
      t = pool->SubstType(w, vi->tyvars[i], t);
    }

    if (vi->primop.has_value()) {
      Primop po = vi->primop.value();
      const auto &[type_arity, val_arity] = PrimopArity(po);
      CHECK((int)tvs.size() == type_arity) << "Bug: Wrong number of type "
        "arguments to primop. This is probably a mistake in "
        "PrimopArity or Initial.";

      // In the case of a primop, we need to eta expand it with
      // a lambda. Since the primop takes a list of arguments,
      // we also need to project the elements from the tuple.
      std::string x = pool->NewVar();
      const il::Exp *vx = pool->Var({}, x);

      std::vector<const il::Exp *> args;
      args.reserve(val_arity);
      CHECK(val_arity < 10) << "This can be supported, but I need to "
        "be careful about the order of two-digit numbers.";
      if (val_arity == 1) {
        // Don't make a tuple of length 1.
        args.push_back(vx);
      } else {
        for (int i = 0; i < val_arity; i++) {
          args.push_back(pool->Project(StringPrintf("%d", i + 1), vx));
        }
      }

      // λx.primop<t1, t2, ...>(x)                 (when val_arity = 1)
      // λx.primop<t1, t2, ...>(#1 x, #2 x, ...)   (otherwise)
      const il::Exp *lambda =
        pool->Fn("", x, pool->Primop(po, std::move(tvs), std::move(args)));
      return std::make_pair(lambda, t);

    } else if (vi->ctor.has_value()) {
      const auto &[mu_idx_, mu_type_, sum_lab] = vi->ctor.value();

      // As with a primop, we eta-expand. This is a bit simpler because
      // the constructor just takes a single argument.
      // λx.roll(μ type..., inj[lab](x))

      const il::Type *dom = NewEVar(), *cod = NewEVar();
      Unification::Unify("ctor application", pool->Arrow(dom, cod), t);

      // A function argument is never polymorphic, even if the constructor
      // is!
      std::string x = pool->NewVar();
      const il::Exp *vx = pool->Var({}, x);
      const il::Exp *lambda =
        pool->Fn("", x,
                 pool->Roll(cod,
                            pool->Inject(sum_lab, vx)));

      return std::make_pair(lambda, t);
    } else {
      // Otherwise, a simple variable.
      return std::make_pair(pool->Var(tvs, vi->var), t);
    }
  }

  case el::ExpType::LET:
    return ElabDecs(G, el_exp->decs, el_exp->a);

  case el::ExpType::IF:
    // TODO: Defer to case on a built-in datatype decl?
    LOG(FATAL) << "Unimplemented IF";
    break;

  case el::ExpType::FN: {
    // Function type is an arrow. We may need this for the recursive
    // variable, so create it up front.
    const il::Type *dom = NewEVar();
    const il::Type *cod = NewEVar();
    const il::Type *fntype = pool->Arrow(dom, cod);

    const std::string self =
      el_exp->str.empty() ? pool->NewVar("self") : el_exp->str;
    const std::string iself = pool->NewVar(self);
    Context GG = G.Insert(self,
                          VarInfo{
                            .tyvars = {},
                            .type = fntype,
                            .var = iself
                              });

    // Bind an argument variable.
    const std::string arg = pool->NewVar("x");
    const std::string iarg = pool->NewVar(arg);
    GG = GG.Insert(arg, VarInfo{.tyvars = {}, .type = dom, .var = iarg});


    std::vector<std::pair<const el::Pat *, const el::Exp *>> rows;
    for (const auto &[p, e] : el_exp->clauses)
      rows.emplace_back(p, e);
    rows.emplace_back(
        el_pool->WildPat(),
        // Could include location info here.
        el_pool->Fail(el_pool->String("unhandled match")));

    // Now translate the pattern.
    const auto [body, body_type] =
      pattern_compilation->Compile(GG, arg, dom, rows);

    Unification::Unify("fn body", body_type, cod);

    return std::make_pair(pool->Fn(iself, iarg, body), fntype);
  }

  case el::ExpType::CASE: {

    const auto &[oe, ot] = Elab(G, el_exp->a);
    std::string obj_var = pool->NewVar("obj");
    std::string obj_ilvar = pool->NewVar("obj");

    Context GG = G.Insert(obj_var,
                          VarInfo{
                              .tyvars = {},
                              .type = ot,
                              .var = obj_ilvar});

    std::vector<std::pair<const el::Pat *, const el::Exp *>> rows =
      el_exp->clauses;
    rows.emplace_back(el_pool->WildPat(),
                      el_pool->Fail(el_pool->String("unhandled match")));

    const auto &[cexp, ctype] =
      pattern_compilation->Compile(GG, obj_var, ot, rows);
    return std::make_pair(
        pool->Let({}, obj_ilvar, oe,
                  cexp),
        ctype);
  }

  case el::ExpType::APP: {
    const auto &[fe, ft] = Elab(G, el_exp->a);
    const auto &[xe, xt] = Elab(G, el_exp->b);

    const il::Type *dom = NewEVar();
    const il::Type *cod = NewEVar();
    Unification::Unify("application-fn", ft, pool->Arrow(dom, cod));
    Unification::Unify("application-arg", xt, dom);

    return std::make_pair(pool->App(fe, xe), cod);
  }

  case el::ExpType::FAIL: {
    const auto &[e, t] = Elab(G, el_exp->a);
    Unification::Unify("fail", t, pool->StringType());
    // Can have any return type, as it does not return.
    const il::Type *ret = NewEVar();
    return std::make_pair(pool->Fail(e), ret);
  }

  default:
    break;
  }

  LOG(FATAL) << "Unimplemented exp type: " << el::ExpString(el_exp);
  return std::make_pair(nullptr, nullptr);
}
