#include "nullary.h"

#include "el.h"
#include "el-pass.h"
#include "base/logging.h"
#include "functional-map.h"

namespace { struct Unit { }; }
using FunctionalSet = FunctionalMap<std::string, Unit>;

namespace el {

// Argument is the set of nullary constructors.
struct NullaryPass : public Pass<FunctionalSet> {
  using Pass::Pass;

  const Exp *DoLet(const std::vector<const Dec *> &ds,
                   const Exp *e,
                   FunctionalSet nullary_ctors) override {
    std::vector<const Dec *> dd;
    for (const Dec *d : ds) {
      if (d->type == DecType::DATATYPE) {
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
              nullary_ctors = nullary_ctors.Insert(lab, {});
            } else {
              ddd.arms.emplace_back(lab, DoType(t, nullary_ctors));
            }
          }
          dds.push_back(std::move(ddd));
        }
        dd.push_back(pool->DatatypeDec(d->tyvars, std::move(dds)));

      } else {
        dd.push_back(Pass::DoDec(d, nullary_ctors));
      }
    }
    return pool->Let(dd, DoExp(e, nullary_ctors));
  }

  const Dec *DoDatatypeDec(const std::vector<std::string> &tyvars,
                           const std::vector<DatatypeDec> &ds,
                           FunctionalSet nullary_ctors) override {
    LOG(FATAL) << "This case should not be reached; we cover datatype "
      "decls in LET.";
  }

  const Pat *DoVarPat(const std::string &v,
                      FunctionalSet nullary_ctors) override {
    if (nullary_ctors.Contains(v)) {
      return pool->AppPat(v, pool->RecordPat({}));
    } else {
      return pool->VarPat(v);
    }
  }

  const Exp *DoVar(const std::string &v,
                   FunctionalSet nullary_ctors) override {
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
  return pass.DoExp(e, FunctionalSet());
}

}  // el
