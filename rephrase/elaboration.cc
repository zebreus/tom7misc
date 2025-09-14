
// TODO: Don't allow constructors to be shadowed by regular variables.
// Nullary assumes this.

#include "elaboration.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>
#include <string>
#include <unordered_set>
#include <vector>

#include "base/print.h"
#include "inclusion.h"
#include "context.h"
#include "el.h"
#include "il.h"
#include "initial.h"
#include "pattern-compilation.h"
#include "il-util.h"
#include "primop.h"
#include "unification.h"
#include "util.h"

#include "ansi.h"

// This code has to mention both el and il stuff with the same
// name. But there are many things that are unambiguous.
using Context = il::ElabContext;
using VarInfo = il::VarInfo;
using Unification = il::Unification;
using TypeVarInfo = il::TypeVarInfo;
using EVar = il::EVar;
using PatternCompilation = il::PatternCompilation;
using Program = il::Program;
using ILUtil = il::ILUtil;
using Global = il::Global;

using DatatypeDec = el::DatatypeDec;

static constexpr bool VERBOSE = false;

Elaboration::Elaboration(const SourceMap &source_map,
                         el::AstPool *el_pool, il::AstPool *il_pool) :
  source_map(source_map), el_pool(el_pool), pool(il_pool), init(pool) {
  pattern_compilation.reset(new PatternCompilation(this));
}

Elaboration::~Elaboration() {}

std::pair<const il::Exp *, const il::Type *> Elaboration::FailMatch() {
  const il::Type *any = NewEVar();
  return std::make_pair(pool->Fail(pool->String("match"), any), any);
}

const il::Type *Elaboration::NewEVar() {
  return pool->EVar(EVar());
}

const il::Type *Elaboration::EVarize(const std::vector<std::string> &tyvars,
                                     const il::Type *type) {
  for (const std::string &alpha : tyvars) {
    type = pool->SubstType(NewEVar(), alpha, type);
  }
  return type;
}

Program Elaboration::Elaborate(const el::Exp *el_exp) {
  Context G = init.InitialContext();

  const auto &[e, t] = Elab(G, el_exp);

  // Should check that the program has type layout?
  if (verbose > 0) {
    Print("Program type: {}\n", TypeString(t));
  }

  Program pgm;
  pgm.globals = std::move(globals);
  globals.clear();
  pgm.body = e;
  return pgm;
}

static bool AllowedInObject(const il::Type *t) {
  return ILUtil::GetObjFieldType(t).has_value();
}

const il::Type *Elaboration::ElabType(const Context &G,
                                      const el::Type *el_type) {

  const size_t pos = el_type->pos;

  auto Error = [&](const std::string &construct) ->
    std::function<std::string()> {
    return std::function<std::string()>([this, construct, pos, el_type]() {
        std::string loc = ErrorAtPos(pos);
        return std::format("{}"
                           "ElabType: {}\n"
                           "Type: {}\n",
                           loc,
                           construct,
                           TypeString(el_type));
      });
    };

  switch (el_type->type) {
  case el::TypeType::VAR: {

    const TypeVarInfo *k = G.FindType(el_type->var);
    CHECK(k != nullptr) << "Unbound (type) variable: "
                        << el_type->var
                        << "\nIn:\n" << Error("var")();

    const il::Type *t = k->type;
    if (VERBOSE) {
      Print(AORANGE("Look up") " type var " APURPLE("{}") ": {}\n",
             el_type->var,
             TypeString(t));
    }

    CHECK(k->tyvars.size() == el_type->children.size()) <<
      "Type constructor '" << el_type->var << "' applied to the "
      "wrong number of arguments (want " << k->tyvars.size() <<
      "; got " << el_type->children.size() << ").\nIn:\n" <<
      Error("var")();

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
      rec.emplace_back(std::format("{}", i + 1), tt);
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

  LOG(FATAL) << "Unknown type";
  return nullptr;
}

std::tuple<std::vector<Elaboration::ILDec>,
           std::vector<il::ElabContext::Binding>,
           il::ElabContext>
Elaboration::ElabDec(
    const il::ElabContext &G,
    const el::Dec *dec) {

  const size_t pos = dec->pos;

  // PERF this copies the string, but it seems safer than worrying about
  // its lifetime. Most of these will be smaller than the SSO.
  auto Error = [&](const std::string &construct) ->
    std::function<std::string()> {
    return std::function<std::string()>([this, construct, pos, dec]() {
        std::string loc = ErrorAtPos(pos);
        return std::format("{}"
                           "ElabDec: {}\n"
                           "Declaration: {}\n",
                           loc,
                           construct,
                           ShortColorDecString(dec));
      });
    };

  switch (dec->type) {
  case el::DecType::VAL:
    // Only irrefutable patterns are allowed in a val decl.

    // Need to generalize if free evars.
    // All this is handled in the pattern compiler.
    return pattern_compilation->CompileIrrefutable(
        G, dec->pat, dec->exp);

  case el::DecType::FUN: {
    CHECK(!dec->funs.empty()) << "Bug: Should not parse empty funs.";
    const size_t pos = dec->pos;

    auto GetSimpleClauses = [](const el::FunDec &fun) {
        std::vector<std::pair<const el::Pat *, const el::Exp *>>
          simple_clauses;
        simple_clauses.reserve(fun.clauses.size());
        for (const auto &[ps, e] : fun.clauses) {
          CHECK(ps.size() == 1) << "Function " << fun.name <<
            " should have been rewritten already by the Uncurry "
            "phase.";
          simple_clauses.emplace_back(ps[0], e);
        }
        return simple_clauses;
      };

    if (dec->funs.size() == 1) {
      const el::FunDec &fun = dec->funs[0];

      // If not mutually recursive, do this the easy way.
      return pattern_compilation->CompileIrrefutable(
          G,
          el_pool->VarPat(fun.name, pos),
          el_pool->Fn(fun.name, GetSimpleClauses(fun), pos));
    } else {
      // For mutually recursive functions, we hoist them out to
      // globals so that they can reference each other. First,
      // type-check and translate the bodies.
      std::vector<std::string> el_vars;
      std::vector<std::string> il_vars;
      std::vector<std::pair<const il::Type *, const il::Type *>> dom_cods;
      Context GG = G;
      for (const el::FunDec &fun : dec->funs) {
        const std::string &el_var = fun.name;
        const std::string il_var = pool->NewVar(el_var);
        el_vars.push_back(el_var);
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
      fns.reserve(dec->funs.size());
      for (int i = 0; i < (int)dec->funs.size(); i++) {
        const el::FunDec &fun = dec->funs[i];
        // Might be better to use a pos for the specific function
        // (or clause)? Right now we just use the first FUN token.

        // In the generated code we could perform the recursion
        // through the fn's recursive name or through the global.
        // The fn name is a little better because its type variables
        // are already instantiated, but if there ends up being a
        // reason to prefer the global, it would be easy to switch
        // it right here.
        const auto &[e, t] =
          Elab(GG, el_pool->Fn(el_vars[i], GetSimpleClauses(fun), pos));
        const auto &[dom, cod] = dom_cods[i];
        Unification::Unify(Error("fun..and decl"), pool->Arrow(dom, cod), t);
        fns.emplace_back(e, t);
      }

      // Once the function bodies are elaborated, we've finished with
      // any constraints on their types, so we can generalize. We only
      // support uniform recursion, so we get the same set of type
      // variables for all of them. Get that set.
      std::vector<EVar> free_evars = [&]() {
          std::vector<const il::Type *> all_types;
          for (const auto &[dom, cod] : dom_cods) {
            all_types.push_back(dom);
            all_types.push_back(cod);
          }

          std::vector<EVar> gen_evars;
          for (const EVar &v : EVar::FreeEVarsInTypes(all_types)) {
            // Only generalize evars that cannot still be accessed
            // through the context. (The context to consider here
            // is G, the one for the outer scope.)
            if (!G.HasEVar(v)) {
              gen_evars.push_back(v);
            }
          }
          return gen_evars;
        }();

      if (VERBOSE) {
        Print("Free type variables:\n");
        for (const EVar &ev : free_evars) {
          Print("  {}\n", ev.ToString());
        }
      }

      // Generalize, setting the evars to new type variables.
      std::vector<std::string> tyvars;
      std::vector<const il::Type *> tyvar_args;
      tyvars.reserve(free_evars.size());
      tyvar_args.reserve(free_evars.size());
      for (const EVar &ev : free_evars) {
        std::string alpha_var = pool->NewVar("gen");
        const il::Type *alpha = pool->VarType(alpha_var, {});
        tyvars.push_back(alpha_var);
        tyvar_args.push_back(alpha);
        ev.Set(alpha);
      }

      // The declared functions may mention local variables that prevent
      // them from being hoisted to top-level. These present as
      // free variables that are not in the mutually-recursive bundle
      // and are not globals. We use the same environment for the
      // entire bundle.
      //
      // Note that the bundle may use a polymorphic value (e.g. 'map')
      // at multiple different types in its body. Two options here:
      // We can replicate it in the environment for each different
      // type instantiation, or we can use IL-level type quantification.
      // Trying the second option here.
      std::unordered_set<std::string> fvs_all;
      for (int i = 0; i < (int)dec->funs.size(); i++) {
        const auto &[oexp, otype] = fns[i];
        for (const std::string &fv : ILUtil::FreeExpVars(oexp)) {
          fvs_all.insert(fv);
        }
      }

      // The other functions in the bundle will look syntactically
      // free, but they are being bound as part of this declaration.
      // Remove them from the set.
      for (const std::string &ilv : il_vars) {
        fvs_all.erase(ilv);
      }

      // The free variables must be bound in the context, so get
      // those types. We have the (IL) var, its tyvars if any, and
      // a forall-quantified monotype. For example, 'ignore' would
      // be
      //   {"ignore", {"α"}, ∀ α. α → unit}
      //
      std::vector<std::tuple<std::string,
                             std::vector<std::string>,
                             const il::Type *>> fvts;
      for (const std::string &s : fvs_all) {
        // Since we've already elaborated the expression, its
        // free variables are IL variables. Look them up in the
        // context to get their types.
        const VarInfo *vi = G.FindByILVar(s);
        CHECK(vi != nullptr) << "Bug: When compiling a "
          "mutually-recursive bundle of functions (fun...and), "
          "found a free variable " << s << " that's not bound in "
          "the context.\n";
        CHECK(vi->type != nullptr) << "Bug: Maybe this happens "
          "for primops or ctors?";

        const il::Type *typ = vi->type;
        for (int i = (int)vi->tyvars.size() - 1; i >= 0; i--) {
          typ = pool->Forall(vi->tyvars[i], typ);
        }

        fvts.emplace_back(s, vi->tyvars, typ);
      }

      std::sort(fvts.begin(), fvts.end(),
                [](const auto &a, const auto &b) {
                  return std::get<0>(a) < std::get<0>(b);
                });

      if (VERBOSE) {
        Print("Environment for mutually recursive fun dec:\n");
        for (const auto &[x, tv, t] : fvts) {
          Print("  {{" "{}" "}} {} : {}\n",
                Util::Join(tv, ","),
                x.c_str(), TypeString(t));
        }
      }

      // The labels have the same names as the variables for our
      // convenience. So the record type is directly derived from
      // the free variable set we just created.
      std::vector<std::pair<std::string, const il::Type *>> record_type;
      record_type.reserve(fvts.size());
      for (const auto &[x, tv_, t] : fvts) record_type.emplace_back(x, t);
      const il::Type *env_type = pool->RecordType(record_type);
      const std::string gx = pool->NewVar("env");
      const il::Exp *gxv = pool->Var({}, gx);

      // Wrap the body with an expression that unpacks every element of the
      // environment. We rely on simplification to clean up ones that we're
      // not using. These bindings are polymorphic in the same way that the
      // original variables were; we bind them by abstracting over the same
      // type variables. The stored value is ∀-quantified at the IL level,
      // so we apply it to those type variables in order.
      auto UnpackEnv = [&](const il::Exp *body) {
          for (const auto &[fv, tvs, t_] : fvts) {
            const il::Exp *rhs = pool->Project(fv, env_type, gxv);
            for (const std::string &alpha : tvs) {
              rhs = pool->TypeApp(rhs, pool->VarType(alpha, {}));
            }
            body = pool->Let(tvs, fv, rhs, body);
          }
          return body;
        };

      // Names for the globals.
      std::vector<std::string> global_syms;
      global_syms.reserve(dec->funs.size());
      for (const el::FunDec &fun : dec->funs) {
        global_syms.push_back(
            pool->NewVar(std::format("g_{}", fun.name)));
      }

      // Now we can generate the global for each function.
      CHECK(dec->funs.size() == fns.size());
      for (int i = 0; i < (int)dec->funs.size(); i++) {
        const el::FunDec &fun = dec->funs[i];
        const auto &[obody, otype] = fns[i];

        // If the function is f : d->c = (fn x => e), with free variables
        // x1 : t1, x2 : t2, ... then we generate the global
        // global_f : (t1*t2*...) -> d -> c =
        //    (fn (x1=x1, x2=x2, ...) => fn x => e)
        //
        // With appropriate substitutions for calls to friends in the
        // bundle.
        const il::Exp *gbody = obody;

        // To call self, we're just using the recursive variable. It's
        // already bound so there's nothing to do for it.
        // To call a friend f, we just call (glob_f env) to get the
        // function of the correct type.
        for (int j = 0; j < (int)dec->funs.size(); j++) {
          const el::FunDec &friend_fun = dec->funs[j];
          if (friend_fun.name == fun.name) {
            // Substitution would do nothing, but skip it to do fewer
            // allocations.
          } else {
            // The symbol is applied to the same type variables that
            // were passed into it, since each function in the bundle
            // is abstracted over the same set and recursion is uniform.
            const il::Exp *friend_exp =
              pool->App(pool->GlobalSym(tyvar_args, global_syms[j]), gxv);
            gbody = ILUtil::SubstExp(pool, friend_exp, il_vars[j], gbody);
          }
        }

        // Now wrap the body with the projections from the environment.
        // We could have done this before the substitutions above, since
        // the symbols should be disjoint, but that just makes more work
        // copying these.
        gbody = UnpackEnv(gbody);

        const il::Type *glob_type = pool->Arrow(env_type, otype);
        const il::Exp *glob_fn = pool->Fn("", gx, glob_type, gbody);

        // Now add it to the table.
        Global global;
        // Same tyvars for each.
        global.tyvars = tyvars;
        global.sym = global_syms[i];
        global.type = glob_type;
        global.exp = glob_fn;
        globals.push_back(std::move(global));
      }

      // Now the globals are emitted and can access one another. Next
      // we need to bind callable functions for the body of the let.
      // These can just be
      //   val env = {x1=x1, x2=x2, ...}
      //   val (α1, α2) f1 = (g_f1 env)
      //   val (α1, α2) f2 = (g_f2 env)

      // The context used for elaboration of the let's body is almost
      // the same as what we used to evaluate the function bodies, but
      // now we've generalized. So build a new context.

      std::vector<il::ElabContext::Binding> binds;
      Context GGG = G;
      for (int i = 0; i < (int)dec->funs.size(); i++) {
        const el::FunDec &fun = dec->funs[i];
        std::string il_var = il_vars[i];
        const auto &[dom, cod] = dom_cods[i];
        VarInfo funinfo = VarInfo{
          .tyvars = tyvars,
          .type = pool->Arrow(dom, cod),
          .var = il_var,
        };
        GGG = GGG.Insert(fun.name, funinfo);
        binds.push_back(il::ElabContext::Binding{
            .v = fun.name, .info = {funinfo}});
      }

      // Get a name (cosmetic) for the environment that involves
      // the functions declared.
      std::string aenv_var = [this, dec]() {
          std::vector<std::string> fnames;
          for (const el::FunDec &fun : dec->funs)
            fnames.push_back(fun.name);
          return pool->NewVar(
              std::format("{}_env", Util::Join(fnames, "_")));
        }();

      std::vector<ILDec> ret;
      // One for each function, and the environment itself.
      ret.reserve(dec->funs.size() + 1);

      // First, the environment.
      {
        std::vector<std::pair<std::string, const il::Exp *>> env_fields;
        env_fields.reserve(fvts.size());
        for (const auto &[lab_var, tvs, t_] : fvts) {
          // Label and variable are the same. But we abstract over
          // any bound type variables for polymorphic bindings.
          std::vector<const il::Type *> tv_args;
          tv_args.reserve(tvs.size());
          for (const std::string &alpha : tvs)
            tv_args.push_back(pool->VarType(alpha, {}));
          const il::Exp *elt = pool->Var(tv_args, lab_var);
          for (int i = (int)tvs.size() - 1; i >= 0; i--) {
            elt = pool->TypeFn(tvs[i], elt);
          }
          env_fields.emplace_back(lab_var, elt);
        }
        ret.push_back(ILDec{
              .tyvars = {},
              .x = aenv_var,
              .rhs = pool->Record(env_fields)
              });
      }

      // const il::Exp *ret_exp = let_exp;
      // Now each function binding as described above.
      for (int i = 0; i < (int)dec->funs.size(); i++) {
        // Generalize using the same type variable bindings and
        // occurrences. Note that since the body is an application,
        // this violates the value restriction, but that is an EL
        // concept, not an IL one.
        //
        // (If we need to fix this to do monomorphization, we can
        // just eta-expand it here. It has arrow type.)
        ret.push_back(ILDec{
            .tyvars = tyvars,
            .x = il_vars[i],
            .rhs = pool->App(pool->GlobalSym(tyvar_args, global_syms[i]),
                             // Environment is not polymorphic.
                             pool->Var({}, aenv_var))
          });
      }

      return std::make_tuple(ret, binds, GGG);
    }
    break;
  }

  case el::DecType::LOCAL: {
    std::vector<ILDec> ildecs;

    il::ElabContext GG = G;
    // The hidden decls.
    for (const el::Dec *el_dec : dec->decs1) {
      const auto &[ds, binds, GGG] = ElabDec(GG, el_dec);
      GG = GGG;
      for (const ILDec &d : ds)
        ildecs.push_back(d);
    }

    il::ElabContext GHIDE = GG;
    // The exposed decls.
    std::vector<il::ElabContext::Binding> exposed_binds;
    for (const el::Dec *el_dec : dec->decs2) {
      const auto &[ds, binds, GGG] = ElabDec(GG, el_dec);
      GG = GGG;
      for (const ILDec &d : ds)
        ildecs.push_back(d);
      for (const il::ElabContext::Binding &b : binds)
        exposed_binds.push_back(b);
    }

    // All the IL decls are part of the elaborated program.
    // But we do not want to expose the bindings from the
    // hidden part.

    il::ElabContext GL = G;
    for (const auto &b : exposed_binds) {
      GL = GL.InsertBinding(b);
    }

    return std::make_tuple(ildecs, exposed_binds, GL);
  }

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

    // Now produce the output bindings and context.
    il::ElabContext GGG = G;
    std::vector<il::ElabContext::Binding> binds;

    // Bind the datatypes themselves, e.g. 'list' and 'option'.
    // Each is a function over the same IL type variables,
    // Λ(tyvars). pi_n (... same mu body ...)
    std::vector<const il::Type *> mu_types;
    for (int i = 0; i < (int)dec->datatypes.size(); i++) {
      const DatatypeDec &dd = dec->datatypes[i];
      const il::Type *mu = pool->Mu(i, sum_types);
      mu_types.push_back(mu);
      TypeVarInfo datatype_info = TypeVarInfo{
        .tyvars = il_tyvars,
        .type = mu,
      };
      GGG = GGG.InsertType(dd.name, datatype_info);
      binds.push_back(il::ElabContext::Binding{
          .v = dd.name,
          .info = datatype_info,
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

        // The mu constructor binds the recursive type variables, so
        // they will not be free in the codomain. But in the domain,
        // they are outside that binding. Substitute the actual mu
        // type for all datatypes being declared.
        // (e.g. for :: we have α * list -> (μ list. ...), but list
        // is an unbound il variable).
        for (int yother = 0; yother < (int)dec->datatypes.size(); yother++) {
          const il::Type *mu_other = mu_types[yother];
          // e.g. "list$1"
          const std::string il_tyvar = recvars[yother].second;
          if (VERBOSE) {
            Print("[{}/{}]{}\n",
                  TypeString(mu_other),
                  il_tyvar,
                  TypeString(dom));
          }
          dom = pool->SubstType(mu_other, il_tyvar, dom);
        }

        if (VERBOSE) {
          std::string tyvars;
          if (!il_tyvars.empty()) {
            tyvars = std::format("(" AYELLOW("{}") ") ",
                                 Util::Join(il_tyvars, ","));
          }
          Print("Binding constructor " ABLUE("{}") " : "
                "{}{} -> {}\n"
                "   with .ctor = {}  {}  {}\n",
                ctor,
                tyvars,
                TypeString(dom),
                TypeString(cod),
                y,
                TypeString(mu_type),
                ctor);
        }
        VarInfo ctor_info = VarInfo{
          .tyvars = il_tyvars,
          .type = pool->Arrow(dom, cod),
          .ctor = std::make_optional(std::make_tuple(y, mu_type, ctor)),
        };
        GGG = GGG.Insert(ctor, ctor_info);
        binds.push_back(
            il::ElabContext::Binding({.v = ctor, .info = ctor_info}));
      }
    }

    // There are no actual declarations generated; all the bindings
    // are transparent. So just elaborate the body in the new context.
    return std::make_tuple(std::vector<ILDec>{}, binds, GGG);
  }

  case el::DecType::OBJECT: {
    const el::ObjectDec &object = dec->object;

    // All we need to for an object declaration is record the name
    // in the context.
    il::ObjVarInfo ovi;
    for (const auto &[lab, el_type] : object.fields) {
      CHECK(!ovi.fields.contains(lab)) << "Duplicate labels in "
        "declaration of object " << object.name;
      const il::Type *t = ElabType(G, el_type);
      CHECK(AllowedInObject(t)) << "Only base types are allowed "
        "in object declarations.\n"
        "Declaring: " ANSI_PURPLE << object.name << ANSI_RESET "\n"
        "Saw:       " ANSI_BLUE << lab << ANSI_RESET " : " << TypeString(t);
      ovi.fields[lab] = t;
    }

    // As with a datatype decl, nothing is actually generated by
    // the decl. (In the future we could be generating a tag, though?)
    std::vector<il::ElabContext::Binding> binds = {
      il::ElabContext::Binding{
        .v = object.name,
        .info = ovi,
      }
    };
    auto GG = G.InsertObj(object.name, std::move(ovi));
    return std::make_tuple(std::vector<ILDec>{}, binds, GG);
  }

  case el::DecType::TYPE: {
    // This is not hard, but I am rushing for the SIGBOVIK deadline!
    CHECK(dec->tyvars.empty()) << "unimplemented: tyvars in type decl";
    const il::Type *t = ElabType(G, dec->t);
    TypeVarInfo tvi = TypeVarInfo{.tyvars = {}, .type = t};
    il::ElabContext GG = G.InsertType(dec->str, tvi);
    std::vector<il::ElabContext::Binding> binds = {
      il::ElabContext::Binding{
        .v = dec->str,
        .info = tvi,
      }
    };
    return std::make_tuple(std::vector<ILDec>{}, binds, GG);
  }

  case el::DecType::OPEN: {
    const auto &[e, t] = Elab(G, dec->exp);

    std::optional<const il::Type *> ortype = ILUtil::GetTypeIfKnown(t);
    if (!ortype.has_value()) {
      LOG(FATAL) << "In open declaration, the type of the expression "
        "must be synthesizable (and must be a record). You can add a "
        "type annotation to fix this. Object exp: " << ExpString(dec->exp);
    }

    const il::Type *rtype = ortype.value();

    CHECK(rtype->type == il::TypeType::RECORD) << "The type in an open "
      "declaration must be a record. Got: " << TypeString(rtype);
    // generate { f1 = f1, f2 = f2, ... }
    std::vector<std::pair<std::string, const el::Pat *>> pats;
    for (const auto &[f, t] : rtype->Record()) {
      pats.emplace_back(f, el_pool->VarPat(f, pos));
    }
    const el::Pat *rpat = el_pool->RecordPat(pats, dec->pos);

    // XXX PERF: This elaborates the RHS twice. Should not be hard to fix,
    // but the SIGBOVIK Deadline approaches!
    return pattern_compilation->CompileIrrefutable(
        G, rpat, dec->exp);
  }

  }  // switch

  LOG(FATAL) << "Unimplemented in ElabDec";
  return std::make_tuple(
      std::vector<ILDec>{}, std::vector<il::ElabContext::Binding>{}, G);
}



// XXX Maybe now this should just be inlined into the LET case?
std::pair<const il::Exp *, const il::Type *> Elaboration::ElabLet(
    const il::ElabContext &G,
    const std::vector<const el::Dec *> &decs,
    const el::Exp *el_body) {

  std::vector<ILDec> ildecs;
  il::ElabContext GG = G;
  for (const el::Dec *dec : decs) {
    const auto &[ds, binds_, GGG] = ElabDec(GG, dec);
    GG = GGG;
    for (const ILDec &d : ds)
      ildecs.push_back(d);
  }

  const auto &[ee, tt] = Elab(GG, el_body);

  return std::make_pair(LetDecs(ildecs, ee), tt);
}

il::ObjFieldType Elaboration::ResolveObjFieldType(
    // Context for error messages.
    const char *what,
    const el::Exp *error_exp,
    // Can be empty.
    const std::string &objname,
    // Can be null, for cases where the objvarinfo wasn't supplied.
    const il::ObjVarInfo *ovi,
    const std::string &lab,
    // Can be null, e.g. for WITHOUT.
    const il::Type *rhs_type) {

  size_t pos = error_exp->pos;

  if (ovi != nullptr) {
    const auto it = ovi->fields.find(lab);
    auto FieldError = [this, pos, &lab, &objname]() {
        return std::format("{}"
                           "For the field " ABLUE("{}") " in the object "
                           "named " AORANGE("{}") ".",
                           ErrorAtPos(pos),
                           lab, objname);
      };

    CHECK(it != ovi->fields.end()) <<
      FieldError() <<
      "\nField not present. Object expression:\n" <<
      ExpString(error_exp);

      if (rhs_type != nullptr) {
      Unification::Unify([what, &FieldError]() {
          return std::format("{}\n"
                             "Object field type: {}\n",
                             FieldError(), what);
        }, rhs_type, it->second);
    } else {
      // e.g. for WITHOUT, where we don't even have an expression.
      rhs_type = it->second;
    }
  }

  // Now, the type must be known.
  CHECK(rhs_type != nullptr) << "(" << what << "): Bug? There is no "
    "ObjVarInfo AND no rhs_type, which is unexpected.";
  std::optional<const il::Type *> oftype = ILUtil::GetTypeIfKnown(rhs_type);
  if (!oftype.has_value()) {
    CHECK(ovi == nullptr) << "Bug: The field types should all be "
      "known if an object name is provided. Object exp: " <<
      ExpString(error_exp);

    LOG(FATAL) << "(" << what << "): The expression "
      "populating the field " << lab << " needs to have a known type, "
      "but it does not. The type may be determined from the expression "
      "itself (e.g. for literals), or from the object name, but none "
      "was provided. Object exp: " << ExpString(error_exp);
  }

  const std::optional ftype = ILUtil::GetObjFieldType(oftype.value());
  CHECK(ftype.has_value()) << "(" << what << "): The "
    "expression populating the field " << lab << " must have a base "
    "type, but got: " << TypeString(oftype.value()) <<
    "\nObject exp: " << ExpString(error_exp);
  return ftype.value();
}

const std::pair<const il::Exp *, const il::Type *> Elaboration::Elab(
    const Context &G,
    const el::Exp *el_exp) {

  auto Error = [this, el_exp](const std::string &construct) {
      return std::function<std::string()>(
          [this, construct, el_exp]() -> std::string {
            size_t pos = ExpNearbyPos(el_exp);
            std::string loc = ErrorAtPos(pos);
            return std::format("{}"
                               "Elab: {}\n"
                               "Expression: {}\n",
                               loc,
                               construct,
                               ShortColorExpString(el_exp));
      });
    };

  CHECK(el_exp != nullptr) << "Bug: null el_exp";

  switch (el_exp->type) {
  case el::ExpType::STRING:
    return std::make_pair(pool->String(el_exp->str),
                          pool->StringType());

  case el::ExpType::INT:
    return std::make_pair(pool->Int(el_exp->integer),
                          pool->IntType());

  case el::ExpType::BOOL:
    return std::make_pair(pool->Bool(el_exp->boolean),
                          pool->BoolType());

  case el::ExpType::FLOAT:
    return std::make_pair(pool->Float(el_exp->d),
                          pool->FloatType());

  case el::ExpType::ANN: {
    // Type annotations are erased during elaboration, after
    // ensuring they hold through unification.
    const auto &[e, t] = Elab(G, el_exp->a);
    const il::Type *tann = ElabType(G, el_exp->t);
    Unification::Unify(Error("type annotation"), t, tann);
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
      std::string lab = std::format("{}", i + 1);
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
    CHECK(vi != nullptr) << Error("Variable")()
                         << "Unbound variable: " << el_exp->str;

    if (VERBOSE) {
      Print("Look up " ABLUE("{}") " : {}\n",
            el_exp->str,
            Context::VarInfoString(*vi));
    }

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

    if (vi->inlined != nullptr) {
      CHECK(vi->tyvars.empty()) << "Not expected, but we could support this.";
      return std::make_pair(vi->inlined, vi->type);

    } else if (vi->primop.has_value()) {
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

        CHECK(t->type == il::TypeType::ARROW);
        const auto &[dom, cod] = t->Arrow();
        CHECK(dom->type == il::TypeType::RECORD);
        CHECK((int)dom->Record().size() == val_arity) << "Bug: "
          "Mismatch between PrimopArity and PrimopType?";

        for (int i = 0; i < val_arity; i++) {
          args.push_back(pool->Project(std::format("{}", i + 1), dom, vx));
        }
      }

      // λx.primop<t1, t2, ...>(x)                 (when val_arity = 1)
      // λx.primop<t1, t2, ...>(#1 x, #2 x, ...)   (otherwise)
      const il::Exp *lambda =
        pool->Fn("", x, t, pool->Primapp(po, std::move(tvs), std::move(args)));
      return std::make_pair(lambda, t);

    } else if (vi->ctor.has_value()) {
      const auto &[mu_idx_, mu_type, sum_lab] = vi->ctor.value();

      const auto &[mu_idx_2_, arms] = mu_type->Mu();
      CHECK(mu_idx_ == mu_idx_2_);
      // const std::vector<std::pair<std::string, const Type *>> &arms;
      CHECK(mu_idx_ >= 0 && mu_idx_ < (int)arms.size());
      const auto &[alpha, unrolled_sum_type] = arms[mu_idx_];
      CHECK(unrolled_sum_type->type == il::TypeType::SUM);

      // sum type needs to be closed.
      const il::Type *sum_type =
        pool->SubstType(mu_type, alpha, unrolled_sum_type);

      // As with a primop, we eta-expand. This is a bit simpler because
      // the constructor just takes a single argument.
      // λx.roll(μ type..., inj[lab](x))

      const il::Type *dom = NewEVar(), *cod = NewEVar();
      Unification::Unify(Error("ctor application"), pool->Arrow(dom, cod), t);

      // A function argument is never polymorphic, even if the constructor
      // is!
      std::string x = pool->NewVar();
      const il::Exp *vx = pool->Var({}, x);
      const il::Exp *lambda =
        pool->Fn("", x, t,
                 pool->Roll(cod,
                            pool->Inject(sum_lab, sum_type, vx)));

      return std::make_pair(lambda, t);
    } else {
      // Otherwise, a simple variable.
      return std::make_pair(pool->Var(tvs, vi->var), t);
    }
  }

  case el::ExpType::LET:
    return ElabLet(G, el_exp->decs, el_exp->a);

  case el::ExpType::ANDALSO: {
    const auto &[ae, at] = Elab(G, el_exp->a);
    const auto &[be, bt] = Elab(G, el_exp->b);
    Unification::Unify(Error("andalso lhs"), at, pool->BoolType());
    Unification::Unify(Error("andalso rhs"), bt, pool->BoolType());

    return std::make_pair(pool->If(ae, be, pool->Bool(false)),
                          pool->BoolType());
  }

  case el::ExpType::ORELSE: {
    const auto &[ae, at] = Elab(G, el_exp->a);
    const auto &[be, bt] = Elab(G, el_exp->b);
    Unification::Unify(Error("orelse lhs"), at, pool->BoolType());
    Unification::Unify(Error("orelse rhs"), bt, pool->BoolType());

    return std::make_pair(pool->If(ae, pool->Bool(true), be),
                          pool->BoolType());
  }

  case el::ExpType::IF: {
    const auto &[cond_exp, cond_type] = Elab(G, el_exp->a);
    const auto &[true_exp, true_type] = Elab(G, el_exp->b);
    const auto &[false_exp, false_type] = Elab(G, el_exp->c);

    Unification::Unify(Error("if cond"), cond_type, pool->BoolType());
    Unification::Unify(Error("if branches"), true_type, false_type);

    return std::make_pair(pool->If(cond_exp, true_exp, false_exp),
                          true_type);
  }

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

    // XXX Get proper location info here.
    // Also, can include this in the error string.
    rows.emplace_back(
        el_pool->WildPat(),
        el_pool->Fail(el_pool->String("unhandled match", el_exp->pos)));

    // Now translate the pattern.
    const auto [body, body_type] =
      pattern_compilation->Compile(GG, arg, dom, rows, el_exp->pos);

    Unification::Unify(Error("fn body"), body_type, cod);

    return std::make_pair(pool->Fn(iself, iarg, fntype, body), fntype);
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

    const size_t pos = el_exp->a->pos;
    rows.emplace_back(
        el_pool->WildPat(),
        el_pool->Fail(
            el_pool->String(
                std::format("unhandled match at {}",
                            SimplePos(pos)), pos)));

    const auto &[cexp, ctype] =
      pattern_compilation->Compile(GG, obj_var, ot, rows, pos);
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

    if (VERBOSE) {
      Print("App F: {} : {}\n", ExpString(fe), TypeString(ft));
      Print("App X: {} : {}\n", ExpString(xe), TypeString(xt));
    }

    Unification::Unify(Error("application-fn"), ft, pool->Arrow(dom, cod));
    Unification::Unify(Error("application-arg"), xt, dom);

    return std::make_pair(pool->App(fe, xe), cod);
  }

  case el::ExpType::FAIL: {
    const auto &[e, t] = Elab(G, el_exp->a);
    Unification::Unify(Error("fail"), t, pool->StringType());
    // Can have any return type, as it does not return.
    // We annotate the fail with that type, since we need to be able
    // to synthesize the types of IL expressions.
    const il::Type *ret = NewEVar();
    return std::make_pair(pool->Fail(e, ret), ret);
  }

  case el::ExpType::LAYOUT: {
    const il::Exp *e = ElabLayout(G, el_exp->layout);
    return std::make_pair(e, pool->LayoutType());
  }

  case el::ExpType::WITH: {
    const auto &[oe, ot] = Elab(G, el_exp->a);
    const std::string &objtype = el_exp->str;
    const std::string &field = el_exp->str2;
    const auto &[re, rt] = Elab(G, el_exp->b);

    Unification::Unify(Error("with"), ot, pool->ObjType());

    const il::ObjVarInfo *ovi = nullptr;
    if (!objtype.empty()) {
      ovi = G.FindObj(objtype);
      CHECK(ovi != nullptr) << "Unbound object name " << objtype <<
        " in with expression: " << ExpString(el_exp);
    }

    const il::ObjFieldType ftype =
      ResolveObjFieldType("with", el_exp, objtype, ovi, field, rt);

    return {pool->With(oe, field, ftype, re), pool->ObjType()};
  }

  case el::ExpType::WITHOUT: {
    const auto &[oe, ot] = Elab(G, el_exp->a);
    const std::string &objtype = el_exp->str;
    const std::string &field = el_exp->str2;

    Unification::Unify(Error("without"), ot, pool->ObjType());

    const il::ObjVarInfo *ovi = nullptr;
    if (!objtype.empty()) {
      ovi = G.FindObj(objtype);
      CHECK(ovi != nullptr) << "Unbound object name " << objtype <<
        " in without expression: " << ExpString(el_exp);
    }

    const il::ObjFieldType ftype =
      ResolveObjFieldType("without", el_exp, objtype, ovi, field, nullptr);

    return {pool->Without(oe, field, ftype), pool->ObjType()};
  }

  case el::ExpType::OBJECT: {

    // We may not have an object name. But if one is given, it has
    // to be valid!
    const il::ObjVarInfo *ovi = nullptr;
    if (!el_exp->str.empty()) {
      ovi = G.FindObj(el_exp->str);
      CHECK(ovi != nullptr) << Error("object literal")() <<
        std::format("Unbound object name " AORANGE("{}") ".\n",
                    el_exp->str);
    }

    std::vector<
      std::tuple<std::string, il::ObjFieldType, const il::Exp *>> fields;
    fields.reserve(el_exp->str_children.size());
    for (const auto &[lab, e] : el_exp->str_children) {
      const auto &[ee, tt] = Elab(G, e);

      const il::ObjFieldType ftype = ResolveObjFieldType(
          "object literal", el_exp, el_exp->str, ovi, lab, tt);

      fields.emplace_back(lab, ftype, ee);
    }

    return std::make_pair(pool->Object(fields), pool->ObjType());
  }
  }

  LOG(FATAL) << "Unimplemented exp type: " << el::ExpString(el_exp);
  return std::make_pair(nullptr, nullptr);
}

// Producing an IL expression of type LayoutType. Unlike EL, we make
// no syntactic distinction between "layout expressions" and "other
// expressions".
const il::Exp *Elaboration::ElabLayout(
    const il::ElabContext &G,
    const el::Layout *lay) {

  switch (lay->type) {
  case el::LayoutType::TEXT: {
    // This is a literal string.
    // We should consider calling some user-provided parsing hook, though?
    return pool->Primapp(Primop::STRING_TO_LAYOUT, {},
                         {pool->String(lay->str)});
  }

  case el::LayoutType::JOIN: {
    // An EL join is just a node with no attributes.
    std::vector<const il::Exp *> v;
    for (const el::Layout *child : lay->children) {
      const il::Exp *ee = ElabLayout(G, child);
      v.push_back(ee);
    }

    return pool->Node(pool->Object({}), std::move(v));
  }

  case el::LayoutType::EXP: {
    const auto &[ee, tt] = Elab(G, lay->exp);
    Unification::Unify(
        [this, lay]() -> std::string {
          size_t pos = ExpNearbyPos(lay->exp);
          std::string loc = ErrorAtPos(pos);
          return std::format("{}\n"
                             "Elaborating exp embedded in layout.\n"
                             "Expression: {}\n",
                             loc,
                             ShortColorExpString(lay->exp));
        },
        tt, pool->LayoutType());
    return ee;
  }
  }

  LOG(FATAL) << "Unimplemented layout type.";
  return nullptr;
}

const il::Exp *Elaboration::LetDecs(
    const std::vector<ILDec> &decs,
    const il::Exp *body) {
  const il::Exp *ret = body;
  for (int i = decs.size() - 1; i >= 0; i--) {
    const ILDec &dec = decs[i];
    ret = pool->Let(dec.tyvars, dec.x, dec.rhs, ret);
  }
  return ret;
}


std::string Elaboration::ErrorAtPos(size_t byte_pos) {
  if (byte_pos >= SourceMap::BOGUS_POS) {
    // This can probably go once we have sufficient coverage of
    // position information in the parser. But for now it lets me
    // figure out which of the many missing positions got propagated.
    return std::format(AORANGE("BOGUS: ") "%{}\n", uint64_t(byte_pos));
  } else {
    const std::string file = source_map.filecover[byte_pos];
    const int line = source_map.linecover[byte_pos];
    return std::format(
        "\nAt byte {} which is "
        AWHITE("{}") ":" AYELLOW("{}") ".\n",
        byte_pos, file, line);
  }
}

std::string Elaboration::SimplePos(size_t byte_pos) {
  const std::string file = source_map.filecover[byte_pos];
  const int line = source_map.linecover[byte_pos];
  return std::format("{}:{}", file, line);
}
