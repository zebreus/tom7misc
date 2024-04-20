#include "uncurry.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "el.h"
#include "el-pass.h"
#include "base/logging.h"

namespace el {

// Argument is the set of nullary constructors.
struct UncurryPass : public Pass<> {
  using Pass::Pass;

  // Fun decls can't appear in patterns or types, so avoid copying them.
  const Pat *DoPat(const Pat *p) override { return p; }
  const Type *DoType(const Type *t) override { return t; }

  const Dec *DoFunDec(const std::vector<FunDec> &funs, size_t pos) override {
    std::vector<FunDec> ffs;
    ffs.reserve(funs.size());
    for (const auto &fd : funs) {
      FunDec ffd;
      ffd.name = fd.name;
      CHECK(!fd.clauses.empty()) << "Function " << fd.name << " has no "
        "clauses!";

      // Check that it is rectangular.
      const size_t width = (int)fd.clauses[0].first.size();
      CHECK(width > 0) << "Function " << fd.name << " has no arguments?";
      for (const auto &[ps, e] : fd.clauses) {
        CHECK(ps.size() == width) << "Function " << fd.name <<
          " does not have the same number of patterns in each clause "
          "(curry notation). First row had " << width << " and then "
          "later saw " << ps.size() << ".";
      }

      // Don't rewrite, just recurse.
      if (width == 1) {
        for (const auto &[ps, e] : fd.clauses) {
          ffd.clauses.emplace_back(ps, DoExp(e));
        }
      } else {
        //   fun f p1 p2 p3 = e1
        //     | f q1 q2 q3 = e2
        //     ...
        // to
        //   fun f v1 =
        //      fn v2 =>
        //      fn v3 =>
        //        case (v1, v2, v3) of
        //             (p1, p2, p3) => e1
        //           | (p1, p2, p3) => e2

        // Generate n variables.
        std::vector<std::string> v;
        std::vector<const Exp *> vrec;
        for (int i = 0; i < (int)width; i++) {
          v.push_back(pool->NewInternalVar("cur"));
          vrec.push_back(pool->Var(v.back(), pos));
        }

        // Clauses for the internal case. Need to recurse on the
        // expressions, but the patterns can't have any rewrites in
        // them.
        std::vector<std::pair<const Pat *, const Exp *>> case_clauses;
        case_clauses.reserve(fd.clauses.size());
        for (const auto &[ps, e] : fd.clauses) {
          case_clauses.emplace_back(pool->TuplePat(ps), DoExp(e));
        }

        const Exp *body = pool->Case(pool->Tuple(vrec),
                                     std::move(case_clauses), pos);

        // Wrap in fn expressions.
        // Note that we do NOT include v1 here.
        for (int i = (int)width - 1; i > 0; i--) {
          body = pool->Fn("", {std::make_pair(pool->VarPat(v[i]), body)}, pos);
        }

        // Because we use it as the argument to the generated fun decl.
        CHECK(ffd.clauses.empty());
        ffd.clauses.emplace_back(std::vector<const Pat *>{pool->VarPat(v[0])},
                                 body);
      }

      ffs.push_back(std::move(ffd));
    }
    return pool->FunDec(std::move(ffs), pos);
  }

};

Uncurry::Uncurry(AstPool *pool) : pool(pool) {}

const Exp *Uncurry::Rewrite(const Exp *e) {
  UncurryPass pass(pool);
  return pass.DoExp(e);
}

}  // el
