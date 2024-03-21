
#include "closure-conversion.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "context.h"
#include "il-typed-pass.h"
#include "il-util.h"
#include "il.h"
#include "simplification.h"
#include "util.h"

static constexpr bool VERBOSE = false;

// Closure conversion makes all functions in the program
// into globals. Globals are closed except for other globals.
//
// We do a similar thing to compile mutually-recursive fun
// declarations (see elaboration.cc) but it's easier here, because
// among other things, there is just one function.

// The type a -> b
// becomes
//  ∃α. {1: α, 2: {1: α, 2:a} -> b}
// α is the environment type. The function is represented as
// a pair of the environment and the closed function that now
// takes an additional environment argument.
//
// and (e1 e2)
// is therefore
// unpack α,f = e1
// in (#2 f){1 = #1 f, 2 = e2}
//
// That is, we unpack the environment and call the function
// with it and the original argument.
//
// A function expression (fn as self x => body) is
// translated as follows. Let x1:t1, ... xn:tn be the free
// variables in "body". Then the function expression is
// pack {1:t1, 2:t2, ...} as α.{1: α, 2: {1: α, 2:a} -> b} of g
//
// where g is a new global symbol
//
// global g = fn as self {1 = env, 2 = x} =>
//      let x1 = #1 env
//      let x2 = #2 env
//             ...
//      in body end


namespace il {

static const Type *PolyTypeToForall(AstPool *pool, const PolyType &pt) {
  const Type *t = pt.second;
  for (int i = pt.first.size() - 1; i >= 0; i --) {
    t = pool->Forall(pt.first[i], t);
  }
  return t;
}

namespace {
struct ConvertPass : public TypedPass<> {
  using TypedPass::TypedPass;

  static std::pair<const Type *, const Type *> GetPairType(const Type *t) {
    const std::vector<std::pair<std::string, const Type *>> &v =
      t->Record();
    CHECK(v.size() == 2 &&
          v[0].first == "1" &&
          v[1].first == "2") << "Bug: Expected pair type. The labels should "
      "be sorted.";
    return std::make_pair(v[0].second, v[1].second);
  }

  // The type a -> b
  // becomes
  //  ∃α. {1: α, 2: {1: α, 2:a} -> b}
  const Type *DoArrow(Context G,
                      const Type *dom, const Type *cod,
                      const Type *guess) override {
    const Type *dd = DoType(G, dom);
    const Type *cc = DoType(G, cod);
    std::string alpha = pool->NewVar("aenv");
    const Type *alpha_type = pool->VarType(alpha, {});
    const Type *arg = pool->RecordType(
        {
          // environment argument
          {"1", alpha_type},
          // actual argument
          {"2", dd},
        });
    const Type *body = pool->RecordType(
        {
          // the environment
          {"1", alpha_type},
          {"2", pool->Arrow(arg, cc)},
        });
    return pool->Exists(alpha, body);
  }

  // (e1 e2)
  // is
  //   unpack α,f = e1
  //   in (#2 f){1 = #1 f, 2 = e2}
  std::pair<const Exp *, const Type *>
  DoApp(Context G,
        const Exp *f, const Exp *arg,
        const Exp *guess) override {
    const auto &[ff, ft] = DoExp(G, f);
    const auto &[aa, at] = DoExp(G, arg);
    // ft is the translated function type,
    // so we expect this to be ∃α. {1: α, 2: {1: α, 2:a} -> b}
    const auto &[aenv, body] = ft->Exists();
    const auto &[tenv, tcc_fn] = GetPairType(body);

    CHECK(tcc_fn->type == TypeType::ARROW) << "Bug: Expected arrow type? "
      "But got: " << TypeString(tcc_fn) << "\nCompiling app of:\n" <<
      ExpString(f) << "\nWhich became:\n" <<
      ExpString(ff) << "\nWith type:\n" <<
      TypeString(ft) << "\nApplied to:\n" <<
      ExpString(arg) << "\nWhich became:\n" <<
      ExpString(aa) << "\nWith type:\n" <<
      TypeString(at);
    const auto &[cc_dom, cod] = tcc_fn->Arrow();

    // Now the expression. Use the same variable from the exists
    // so that we can use types without substitution.
    const std::string fnvar = pool->NewVar("f");
    const Exp *fnexp = pool->Var({}, fnvar);
    const Exp *extracted_env = pool->Project("1", fnexp);
    const Exp *extracted_fn  = pool->Project("2", fnexp);
    const Exp *ret =
      pool->Unpack(aenv, fnvar, ff,
                   pool->App(extracted_fn,
                             pool->Record({{"1", extracted_env},
                                           {"2", aa}})));

    return {ret, cod};
  }

  std::pair<const Exp *, const Type *>
  DoFn(Context G,
       const std::string &self,
       const std::string &x,
       const Type *arrow_type_in,
       const Exp *body,
       const Exp *guess) override {

    // This translates function expressions, which can appear inside
    // expressions and also as globals.

    // First, translate everything recursively, like the base class.
    // We can't defer to TypedPass::DoFn for this because it would
    // create an ill-typed Fn (and actually fail because it wants to
    // extract the components of the arrow type annotation).
    //
    //  fn as self (x : int) -> bool =>
    //     if x = 0 then y
    //     else self (x - 1)
    //
    //  α is the type of the environment.
    //
    //  fn (arg : α * int) -> bool =>
    //     let env = #1 arg
    //     let self = pack α as α'.(α' * (α' * int) -> bool)
    //                of (env, g_self)
    //     let x = #2 arg
    //     let y = #1 env
    //     in if x = 0 then y
    //        else unpack α,ccf = self
    //             in (#2 ccf){1 = #1 ccf, 2 = (x - 1)}

    const Type *cc_arrow_type = DoType(G, arrow_type_in);
    // This will be a type like
    //   ∃α. {1: α, 2: {1: α, 2:a} -> b}
    // Extract the components.
    const auto &[cc_alpha_env, cc_fpair] = cc_arrow_type->Exists();
    const auto &[cc_env_type, cc_fn_type] = GetPairType(cc_fpair);
    CHECK(cc_fn_type->type == TypeType::ARROW);
    const auto &[cc_fn_dom, cc_fn_cod] = cc_fn_type->Arrow();
    const auto &[cc_env_type2, cc_actual_dom] = GetPairType(cc_fn_dom);

    // DCHECK(TypeEq(cc_env_type, cc_env_type2));

    // When recursively translating the body, we need to give types
    // to self and x, which are both in scope. The only good way to
    // do this is to make sure that they have the translated version
    // of their old types. For x this is just [[x]]
    // (which is cc_actual_dom here). For self, this is similarly
    // [[arrow_type]] (cc_arrow_type here), which is now an existential
    // type. That means the translation will need to bind an adapter
    // for the recursive variable.

    Context GG = G.
      // The type of the environment is bound within the arrow type, etc.
      InsertType(cc_alpha_env).
      Insert(self, {{}, cc_arrow_type}).
      Insert(x, {{}, cc_actual_dom});
    const auto &[cc_body, cc_body_type] = DoExp(GG, body);
    // DCHECK(TypeEq(cc_body_type, cc_fn_cod));


    // We will hoist out the function and make it a global. This
    // can be a polymorphic binding: If there are any free *type*
    // variables in the expression we're translating, we'll abstract
    // over those at the binding site, and apply the global to the
    // concrete types (free type variables) here. So, first get
    // the free type variables.
    std::unordered_set<std::string> free_tyvars_set =
      ILUtil::FreeTypeVars(cc_arrow_type);
    for (const std::string &alpha : ILUtil::FreeTypeVarsInExp(cc_body))
      free_tyvars_set.insert(alpha);
    // But we are binding the environment type variable.
    free_tyvars_set.erase(cc_alpha_env);
    std::vector<std::string> free_tyvars(free_tyvars_set.begin(),
                                         free_tyvars_set.end());

    // We also need the free expression variables and their types.
    // Since these might be polymorphic variables, we also get the
    // types they're applied to. The same variable might appear
    // multiple times, applied to different types! We grab all
    // the different ones since this used to try to put monomorphic
    // versions in the environment. But we'll actually put a
    // TypeFn-abstracted thing in there.

    using Uses = std::vector<std::vector<const Type *>>;
    std::unordered_map<std::string, Uses> free_expvars_map =
      ILUtil::FreeExpVarTally(cc_body);
    // But we will bind 'self' and 'x' as part of translation.
    free_expvars_map.erase(self);
    free_expvars_map.erase(x);

    if (VERBOSE) {
      printf(AWHITE("Free expvars") ":\n");
      for (const auto &[v, uses] : free_expvars_map) {
        printf("  %s:\n", v.c_str());
        for (const auto &types : uses) {
          std::vector<std::string> ts;
          for (const Type *t : types) ts.push_back(TypeString(t));
          printf("    (%s)\n", Util::Join(ts, ",").c_str());
        }
      }
    }

    struct EnvEntry {
      std::string il_var;
      // std::vector<const Type *> type_args;
      PolyType polytype;
      std::string env_label;
    };

    // Get their types from the context.
    // This flattens each variable with its polytype and uses.
    std::vector<EnvEntry> env;
    env.reserve(free_expvars_map.size());
    std::unordered_set<std::string> labels_used;
    for (const auto &[var, uses] : free_expvars_map) {
      // PERF! We should be deduplicating uses at the same types here,
      // especially when the tyvars are just empty!
      const PolyType *pt = G.Find(var);
      CHECK(pt != nullptr) << "Bug: In closure conversion, the free "
        "variable " << var << " was not bound in the context!";
      CHECK(!uses.empty());
      const size_t num_type_args = pt->first.size();
      for (const auto &types : uses) {
        CHECK(types.size() == num_type_args) << "Internal type error: "
          "In closue conversion, the variable " << var << " is used with " <<
          num_type_args << " type args, but its type has kind " <<
          num_type_args;
      }

      EnvEntry entry;
      entry.il_var = var;
      entry.polytype = *pt;
      // just need a unique label, but try to use the variable's
      // name for clarity.
      std::string label = pool->BaseVar(var);
      int suffix = 0;
      while (labels_used.contains(label)) {
        suffix++;
        label = StringPrintf("%s_%d", pool->BaseVar(var).c_str(), suffix);
      }
      labels_used.insert(label);
      entry.env_label = std::move(label);
      env.push_back(std::move(entry));
    }

    // Keep the environment in a stable order.
    std::sort(env.begin(),
              env.end(),
              [](const auto &a, const auto &b) {
                return a.env_label < b.env_label;
              });

    if (VERBOSE) {
      for (const EnvEntry &entry : env) {
        std::vector<std::string> ts;
        // for (const Type *t : entry.type_args) ts.push_back(TypeString(t));

        printf(AORANGE("#%s") " = " ABLUE("%s") " (used as <%s>)\n"
               "   polytype: %s\n" ,
               entry.env_label.c_str(),
               entry.il_var.c_str(),
               Util::Join(ts, ",").c_str(),
               PolyTypeString(entry.polytype).c_str());
      }
    }

    // Now, the environment expression itself, along with its actual type.
    std::vector<std::pair<std::string, const Exp *>> env_components;
    std::vector<std::pair<std::string, const Type *>> env_type_components;
    env_components.reserve(env.size());
    env_type_components.reserve(env.size());
    for (const EnvEntry &entry : env) {
      const PolyType &pt = entry.polytype;
      // CHECK(entry.polytype.first.size() == entry.type_args.size());
      // for (int i = 0; i < (int)entry.polytype.first.size(); i++) {
      // const std::string &alpha = entry.polytype.first[i];
      // const Type *arg = entry.type_args[i];
      // t = pool->SubstType(arg, alpha, t);
      // }

      // eta expand the IL variable (using the TypeFn and Forall constructs,
      // which is the only place we use these).
      const Type *forall_type = PolyTypeToForall(pool, pt);
      std::vector<const Type *> eta_type_args;
      eta_type_args.reserve(pt.first.size());
      for (int i = 0; i < (int)pt.first.size(); i++)
        eta_type_args.push_back(pool->VarType(pt.first[i]));
      const Exp *eta_exp = pool->Var(std::move(eta_type_args), entry.il_var);
      for (int i = (int)pt.first.size() - 1; i >= 0; i--) {
        eta_exp = pool->TypeFn(pt.first[i], eta_exp);
      }

      env_components.emplace_back(entry.env_label, eta_exp);
      env_type_components.emplace_back(entry.env_label, forall_type);
    }

    const Exp *env_exp = pool->Record(env_components);
    const Type *env_type = pool->RecordType(env_type_components);

    // A variable for the environment.
    const std::string env_var = pool->NewVar("env");
    const Exp *env_var_exp = pool->Var({}, env_var);

    // The argument to the closure-converted function, which pairs
    // the environment with the original argument.
    std::string arg_var = pool->NewVar("cc_arg");
    const Exp *arg_var_exp = pool->Var({}, arg_var);

    // Wrap the body with projections of each variable from the
    // environment.
    const Exp *fn = cc_body;
    for (const EnvEntry &entry : env) {
      // The binding is polymorphic (regular prenex polymorphism)
      // but the value in the environment is explicit first-order
      // polymorphism. Convert.

      const PolyType &pt = entry.polytype;
      const Exp *rhs = pool->Project(entry.env_label, env_var_exp);
      for (int i = 0; i < (int)pt.first.size(); i++) {
        rhs = pool->TypeApp(rhs, pool->VarType(pt.first[i]));
      }

      fn = pool->Let(entry.polytype.first, entry.il_var, rhs,
                     fn);
    }

    const std::string global_sym =
      pool->NewVar(
          self.empty() ? "g_fn" : StringPrintf("g_%s", self.c_str()));

    // Since we call the function recursively through the global symbol,
    // we need to instantiate it again at the same type variables.
    std::vector<const Type *> free_type_args;
    for (const std::string &a : free_tyvars)
      free_type_args.push_back(pool->VarType(a));

    // Bind "self" if the function is recursive.
    if (!self.empty()) {
      CHECK(!global_sym.empty());
      fn =
        pool->Let(
            {}, self,
            pool->Pack(
                // We could use α here, but it seems a little better (?) to
                // use the actual type, which we do know.
                env_type,
                //   α.{1: α, 2: {1: α, 2:a} -> b}
                cc_alpha_env, cc_fpair,
                pool->Record(
                    {{"1", env_var_exp},
                     {"2", pool->GlobalSym(free_type_args, global_sym)}})),
            fn);
    }

    // Project out the environment and original argument from the
    // arg pair.
    fn = pool->Let({}, x,
                    pool->Project("2", arg_var_exp),
                    fn);
    fn = pool->Let({},
                    env_var, pool->Project("1", arg_var_exp),
                    fn);

    const Type *closed_fn_type =
      pool->SubstType(env_type, cc_alpha_env, cc_fn_type);

    // Now wrap with the function. This function is now closed,
    // and non-recursive.
    fn = pool->Fn("", arg_var, closed_fn_type, fn);

    // Emit the closure-converted function as a top-level symbol.
    Global global;
    global.tyvars = free_tyvars;
    global.sym = global_sym;
    global.type = closed_fn_type;
    global.exp = fn;
    globals_added.push_back(std::move(global));

    // And then the fn itself is translated as a packed pair
    // of the environment and the closed function.

    // pack α as α'.(α' * (α' * int) -> bool)
    // of ({y = , g_self)

    CHECK(!global_sym.empty());
    const Exp *ret =
      pool->Pack(
          // {x1 : t1, x2 = t2, ...}
          env_type,
          // α.(α * (α * dom) -> cod)
          cc_alpha_env, cc_fpair,
          // ({x1=x1, x2=x2, ...}, g<types...>)
          pool->Record({{"1", env_exp},
                        {"2", pool->GlobalSym(free_type_args, global_sym)}}));

    return {ret, cc_arrow_type};
  }

  std::vector<Global> globals_added;
};
}  // namespace

ClosureConversion::ClosureConversion(AstPool *pool) : pool(pool) {

}

void ClosureConversion::SetVerbose(int v) {
  verbose = v;
}

Program ClosureConversion::Convert(const Program &pgm) {
  ConvertPass pass(pool);
  Context G;
  Program cc_pgm = pass.DoProgram(G, pgm);
  for (Global &global : pass.globals_added)
    cc_pgm.globals.push_back(std::move(global));
  return cc_pgm;
}

uint64_t ClosureConversion::SimplificationOpts() {
  return Simplification::O_DEAD_VARS |
    Simplification::O_REDUCE |
    Simplification::O_MAKE_NONRECURSIVE |
    Simplification::O_ETA_CONTRACT |
    Simplification::O_INLINE_EXP |
    Simplification::O_DEAD_CODE |
    Simplification::O_FLATTEN |

    // We want all functions to be global, so prevent
    // inlining them back in. We could still allow
    // some simpler value inlining, though.
    // Simplification::O_GLOBAL_INLINING |
    Simplification::O_GLOBAL_DEAD;
}

}  // namespace il
