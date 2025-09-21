#include "nullary.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "el-pass.h"
#include "el.h"
#include "functional-map.h"
#include "functional-set.h"

namespace el {

// Since a declaration can shadow a constructor, we need to be able to
// represent "remove from the nullary set" as the result of a decl,
// and with LOCAL this just has to be explicit. We might be able to
// simplify this by storing the nullary set itself as a map from
// string to status.
enum class NullaryAction {
  ADD,
  REMOVE,
};

static FunctionalSet<std::string> ApplyActions(
    FunctionalSet<std::string> s,
    FunctionalMap<std::string, NullaryAction> actions) {
  for (const auto &[k, v] : actions.Export()) {
    switch (v) {
    case NullaryAction::ADD:
      s = s.Insert(k);
      break;
    case NullaryAction::REMOVE:
      s = s.Remove(k);
      break;
    }
  }
  return s;
}

static std::string SetString(const FunctionalSet<std::string> &fs) {
  std::string ret = "{";
  for (const std::string &s : fs.Export()) {
    AppendFormat(&ret, " {}", s);
  }
  return ret + " }";
}

static std::string MapString(
    const FunctionalMap<std::string, NullaryAction> &fs) {
  std::string ret = "{";
  for (const auto &[s, v] : fs.Export()) {
    AppendFormat(&ret, " {}={}", s, v == NullaryAction::ADD ? "ADD" : "REM");
  }
  return ret + " }";
}


// Argument is the set of nullary constructors.
struct NullaryPass : public Pass<FunctionalSet<std::string>> {
  using Pass::Pass;

  std::pair<std::vector<const Dec *>,
            FunctionalMap<std::string, NullaryAction>>
  DoDecs(const std::vector<const Dec *> &ds,
         FunctionalSet<std::string> nullary_ctors) {
    // This complexity is for handling LOCAL. nullary_ctors is used
    // for rewriting these declarations (it's everything) and
    // outer_ctors is just the actions to take on the set for the body
    // of the LET that contains these declarations.
    FunctionalMap<std::string, NullaryAction> outer_ctors;

    std::vector<const Dec *> dd;
    for (const Dec *d : ds) {
      if (d->type == DecType::LOCAL) {
        const auto &[dd1, nn1] = DoDecs(d->decs1, nullary_ctors);
        const auto &[dd2, nn2] = DoDecs(d->decs2,
                                        ApplyActions(nullary_ctors, nn1));

        // *not* nn1. only the exported declarations.
        for (const auto &[k, v] : nn2.Export()) {
          outer_ctors = outer_ctors.Insert(k, v);
        }
        nullary_ctors = ApplyActions(nullary_ctors, nn2);

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
              outer_ctors = outer_ctors.Insert(lab, NullaryAction::ADD);
              nullary_ctors = nullary_ctors.Insert(lab);
            } else {
              ddd.arms.emplace_back(lab, DoType(t, nullary_ctors));
            }
          }
          dds.push_back(std::move(ddd));
        }
        dd.push_back(pool->DatatypeDec(d->tyvars, std::move(dds)));

      } else if (d->type == DecType::FUN) {
        // A function declaration can shadow a constructor,
        // so they aren't nullary any more (including in the function
        // body).

        if (verbose >= 3) {
          Print("Nullary on funs with {} / {}\n",
                SetString(nullary_ctors), MapString(outer_ctors));
        }

        std::vector<FunDec> ffs;
        ffs.reserve(d->funs.size());

        // We use the original constructor status for the function's
        // patterns. A function's patterns should not mention one of
        // the new bindings (so this should not matter), but I think
        // this is the most faithful.
        FunctionalSet<std::string> pat_nullary_ctors = nullary_ctors;

        // The function declarations can't be nullary constructors, since
        // they are not constructors. They might shadow constructors, though.
        for (const auto &fd : d->funs) {
          outer_ctors = outer_ctors.Insert(fd.name, NullaryAction::REMOVE);
          nullary_ctors = nullary_ctors.Remove(fd.name);

          if (verbose >= 3) {
            Print("nnc: Removed {}, now {} / {}\n", fd.name,
                  SetString(nullary_ctors),
                  MapString(outer_ctors));
          }
        }

        for (const auto &fd : d->funs) {
          FunDec ffd;
          ffd.name = fd.name;
          for (const auto &[ps, e] : fd.clauses) {
            std::vector<const Pat *> pps;
            pps.reserve(ps.size());
            for (const Pat *p : ps) pps.push_back(DoPat(p, pat_nullary_ctors));
            ffd.clauses.emplace_back(std::move(pps), DoExp(e, nullary_ctors));
          }
          ffs.push_back(std::move(ffd));
        }
        dd.push_back(pool->FunDec(std::move(ffs), d->pos));

      } else {
        CHECK(d->type != DecType::LOCAL);
        dd.push_back(Pass::DoDec(d, nullary_ctors));
      }
    }

    if (verbose >= 3) {
      Print("nullary: return from let with {} / {}\n",
            SetString(nullary_ctors),
            MapString(outer_ctors));
    }

    return std::make_pair(dd, outer_ctors);
  }

  const Exp *DoLet(const std::vector<const Dec *> &ds,
                   const Exp *e,
                   size_t pos,
                   FunctionalSet<std::string> nullary_ctors) override {
    const auto &[dd, nn] = DoDecs(ds, nullary_ctors);
    return pool->Let(dd, DoExp(e, ApplyActions(nullary_ctors, nn)), pos);
  }

  const Dec *DoFunDec(
      const std::vector<FunDec> &funs,
      size_t pos, FunctionalSet<std::string> nullary_ctors) override {
    LOG(FATAL) << "Function declarations are covered in the LET case.";
  }

  const Dec *DoDatatypeDec(
      const std::vector<std::string> &tyvars,
      const std::vector<DatatypeDec> &ds,
      size_t pos,
      FunctionalSet<std::string> nullary_ctors) override {
    LOG(FATAL) << "This case should not be reached; we cover datatype "
      "decls in LET.";
  }

  const Dec *DoLocalDec(const std::vector<const Dec *> &decs1,
                        const std::vector<const Dec *> &decs2,
                        size_t pos,
                        FunctionalSet<std::string> nullary_ctors) override {
    LOG(FATAL) << "This case should not be reached; we cover LOCAL "
      "decls in LET.";
  }

  const Pat *DoVarPat(const std::string &v,
                      size_t pos,
                      FunctionalSet<std::string> nullary_ctors) override {
    if (nullary_ctors.Contains(v)) {
      return pool->AppPat(v, pool->RecordPat({}, pos));
    } else {
      return pool->VarPat(v, pos);
    }
  }

  const Exp *DoVar(const std::string &v,
                   size_t pos,
                   FunctionalSet<std::string> nullary_ctors) override {
    if (nullary_ctors.Contains(v)) {
      return pool->App(pool->Var(v, pos), pool->Record({}, pos), pos);
    } else {
      return pool->Var(v, pos);
    }
  }

  int verbose = 0;
};

Nullary::Nullary(AstPool *pool) : pool(pool) {}

void Nullary::SetVerbose(int v) { verbose = v; }

const Exp *Nullary::Rewrite(const Exp *e) {
  NullaryPass pass(pool);
  pass.verbose = verbose;
  // Perhaps this should include some initial stuff like
  // true and false, if not introduced via some preamble.
  return pass.DoExp(e, FunctionalSet<std::string>());
}

}  // el
