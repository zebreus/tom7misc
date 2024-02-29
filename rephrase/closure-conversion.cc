
#include "closure-conversion.h"

#include <string>

#include "il.h"
#include "il-typed-pass.h"

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
    const auto &[cc_dom, cod] = tcc_fn->Arrow();

    // Now the expression. Use the same variable from the exists
    // so that we can use types without substitution.
    const std::string fnvar = pool->NewVar("f");
    const Exp *fnexp = pool->Var({}, fnvar);
    const Exp *extracted_fn  = pool->Project("1", fnexp);
    const Exp *extracted_env = pool->Project("2", fnexp);
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
       const Type *arrow_type,
       const Exp *body,
       const Exp *guess) override {

    // TODO: How do we deal with existing globals that already have
    // fn type?
    // The function g itself will appear as a global,
    //   glob (α, β) g = fn (env, x) => ...
    // So we just end up translating the body of g like we would
    // translate a function in a val decl. The function is closed over
    // expression variables so the environment will be empty! We
    // can do optimization after this to remove empty args in many cases,
    // but we need to translate every arrow-type expression uniformly
    // so that the types match up.
    //
    // How to deal with polymorphism though? I guess we can abstract
    // over any type variables in the context, like we'll be abstracting
    // over the expression variables to form the environment.
    //
    // What about free variables with polymorphic type?

#if 0
    const Type *at = DoType(G, arrow_type, args...);
    const auto &[dom, cod] = arrow_type->Arrow();
    Context GG = G.Insert(self, {{}, at}).Insert(x, {{}, dom});
    const auto &[be, bt] = DoExp(GG, body, args...);
    return {pool->Fn(self, x, at, be, guess), at};
#endif
  }

  std::vector<Global> globals_added;

};

Program ClosureConversion::Convert(const Program &pgm) {
  LOG(FATAL) << "Unimplemented!";
  return pgm;
}


}  // namespace il
