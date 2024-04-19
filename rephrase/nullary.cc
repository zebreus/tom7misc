#include "nullary.h"

#include "el.h"
#include "el-pass.h"
#include "base/logging.h"
#include "functional-set.h"
#include <string>
#include <utility>
#include <vector>

namespace el {

// Argument is the set of nullary constructors.
struct NullaryPass : public Pass<FunctionalSet<std::string>> {
  using Pass::Pass;

  // Returns only the NEW nullary constructors.
  std::pair<std::vector<const Dec *>,
            FunctionalSet<std::string>> DoDecs(
                const std::vector<const Dec *> &ds,
                FunctionalSet<std::string> nullary_ctors) {
    // nullary_ctors is used for rewriting these declarations (it's
    // everything) and new_nullary_ctors is just the new ones produced
    // by the declarations. They are usually updated in tandem.
    FunctionalSet<std::string> new_nullary_ctors;

    std::vector<const Dec *> dd;
    for (const Dec *d : ds) {
      if (d->type == DecType::LOCAL) {
        const auto &[dd1, nn1] = DoDecs(d->decs1, nullary_ctors);
        const auto &[dd2, nn2] = DoDecs(d->decs2, nullary_ctors.Insert(nn1));

        // But *not* nn1.
        new_nullary_ctors = nullary_ctors.Insert(nn2);
        nullary_ctors.Insert(nn2);

        dd.push_back(pool->LocalDec(dd1, dd2));

      } else if (d->type == DecType::DATATYPE) {
        // Handle this here, since we need to add to the set (including
        // for later delcarations in this let).

        std::vector<DatatypeDec> dds;
        dds.reserve(ds.size());
        for (const auto &dd : d->datatypes) {
          DatatypeDec ddd;
          ddd.name = dd.name;
          for (const auto &[lab, t] : dd.arms) {
            if (t == nullptr) {
              ddd.arms.emplace_back(lab, pool->RecordType({}));
              new_nullary_ctors = nullary_ctors.Insert(lab);
              nullary_ctors = nullary_ctors.Insert(lab);
            } else {
              ddd.arms.emplace_back(lab, DoType(t, nullary_ctors));
            }
          }
          dds.push_back(std::move(ddd));
        }
        dd.push_back(pool->DatatypeDec(d->tyvars, std::move(dds)));

      } else {
        CHECK(d->type != DecType::LOCAL);
        dd.push_back(Pass::DoDec(d, nullary_ctors));
      }
    }
    return std::make_pair(dd, new_nullary_ctors);
  }

  const Exp *DoLet(const std::vector<const Dec *> &ds,
                   const Exp *e,
                   FunctionalSet<std::string> nullary_ctors) override {
    const auto &[dd, nn] = DoDecs(ds, nullary_ctors);
    return pool->Let(dd, DoExp(e, nullary_ctors.Insert(nn)));
  }

  const Dec *DoLocal(const std::vector<const Dec *> &decs1,
                     const std::vector<const Dec *> &decs2,
                     FunctionalSet<std::string> nullary_ctors) override {
    LOG(FATAL) << "This case should not be reached; we cover LOCAL "
      "decls in LET.";
  }

  const Dec *DoDatatypeDec(
      const std::vector<std::string> &tyvars,
      const std::vector<DatatypeDec> &ds,
      FunctionalSet<std::string> nullary_ctors) override {
    LOG(FATAL) << "This case should not be reached; we cover datatype "
      "decls in LET.";
  }

  const Pat *DoVarPat(const std::string &v,
                      FunctionalSet<std::string> nullary_ctors) override {
    if (nullary_ctors.Contains(v)) {
      return pool->AppPat(v, pool->RecordPat({}));
    } else {
      return pool->VarPat(v);
    }
  }

  const Exp *DoVar(const std::string &v,
                   FunctionalSet<std::string> nullary_ctors) override {
    if (nullary_ctors.Contains(v)) {
      return pool->App(pool->Var(v), pool->Record({}));
    } else {
      return pool->Var(v);
    }
  }
};

Nullary::Nullary(AstPool *pool) : pool(pool) {}

const Exp *Nullary::Rewrite(const Exp *e) {
  NullaryPass pass(pool);
  // Perhaps this should include some initial stuff like
  // true and false, if not introduced via some preamble.
  return pass.DoExp(e, FunctionalSet<std::string>());
}

}  // el
