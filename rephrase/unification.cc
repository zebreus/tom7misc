
#include "unification.h"

#include <format>
#include <set>
#include <vector>
#include <memory>
#include <string>
#include <utility>
#include <mutex>
#include <cstdint>
#include <functional>

#include "base/logging.h"
#include "il.h"
#include "ansi.h"

namespace il {

static int64_t NextEVarCounter() {
  static std::mutex m;
  // 0 is an invalid id.
  static int64_t counter = 0;
  std::unique_lock<std::mutex> ml(m);
  counter++;
  return counter;
}

EVar::EVar() : cell(std::make_shared<EVarCell>(NextEVarCounter())) {
}

// TODO: Should probably distinguish bound/free?
std::string EVar::ToString() const {
  return std::format("_EVAR_{}_", GetCell()->id);
}


// When we descend under a quantifier, we need to keep track of
// the correspondence of the bound variable on each side.
using VMap = std::vector<std::pair<std::string, std::string>>;

std::shared_ptr<EVar::EVarCell> EVar::GetCell() const {
  if (const Type *next = cell->GetBound()) {
    if (next->type == TypeType::EVAR) {
      std::shared_ptr<EVar::EVarCell> ptd = next->EVar().GetCell();
      cell = ptd;
    }
  }

  // Here, either we found the deepest cell or it's free, so
  // we are done.
  return cell;
}

bool EVar::SameEVar(const EVar &a, const EVar &b) {
  return a.GetCell().get() == b.GetCell().get();
}

bool EVar::LessEVar(const EVar &a, const EVar &b) {
  return a.GetCell()->id < b.GetCell()->id;
}

bool EVar::Occurs(const EVar &e, const Type *t) {

  switch (t->type) {

  case TypeType::VAR: {
    const auto &[var, args] = t->Var();
    for (const Type *child : args) {
      if (Occurs(e, child)) return true;
    }
    return false;
  }

  case TypeType::SUM:
    for (const auto &[l_, c] : t->Sum()) {
      if (Occurs(e, c)) return true;
    }
    return false;

  case TypeType::ARROW: {
    const auto &[dom, cod] = t->Arrow();
    return Occurs(e, dom) || Occurs(e, cod);
  }

  case TypeType::MU: {
    const auto &[idx_, v] = t->Mu();
    for (const auto &[a, sum_type] : v) {
      if (Occurs(e, sum_type)) return true;
    }
    return false;
  }

  case TypeType::EXISTS: {
    const auto &[a, tt] = t->Exists();
    return Occurs(e, tt);
  }

  case TypeType::FORALL: {
    const auto &[a, tt] = t->Forall();
    return Occurs(e, tt);
  }

  case TypeType::RECORD:
    for (const auto &[l_, c] : t->Record()) {
      if (Occurs(e, c)) return true;
    }
    return false;

  case TypeType::EVAR:
    if (SameEVar(e, t->EVar()))
      return true;
    if (const Type *tt = t->EVar().GetBound()) {
      return Occurs(e, tt);
      // return SameEVar(e, t->EVar());
    }
    return false;

  case TypeType::REF:
    return Occurs(e, t->Ref());

  case TypeType::VEC:
    return Occurs(e, t->Vec());

  case TypeType::STRING:
    return false;

  case TypeType::INT:
    return false;

  case TypeType::WORD:
    return false;

  case TypeType::FLOAT:
    return false;

  case TypeType::BOOL:
    return false;

  case TypeType::OBJ:
    return false;

  case TypeType::LAYOUT:
    return false;
  }

  LOG(FATAL) << "Unhandled case in occurs check.";
  return true;
}

// PERF: We can actually use hash set on the ids, especially
// since we're keeping this implementation-private. But it
// would be unusual for a type to have lots of free evars
// during generalization.
namespace {
struct LessEVar {
  bool operator() (const EVar &a, const EVar &b) const {
    return EVar::LessEVar(a, b);
  }
};
}

using EVarSet = std::set<EVar, LessEVar>;

std::vector<EVar> EVar::FreeEVarsInType(const Type *t) {
  return FreeEVarsInTypes({t});
}

std::vector<EVar> EVar::FreeEVarsInTypes(
    const std::vector<const Type *> &tv) {
  EVarSet s;

  std::function<void(const Type *)> Rec =
    [&](const Type *t) -> void {
    switch (t->type) {

    case TypeType::VAR: {
      const auto &[var, args] = t->Var();
      for (const Type *child : args) {
        Rec(child);
      }
      return;
    }

    case TypeType::SUM:
      for (const auto &[l_, c] : t->Sum())
        Rec(c);
      return;

    case TypeType::ARROW: {
      const auto &[dom, cod] = t->Arrow();
      Rec(dom);
      Rec(cod);
      return;
    }

    case TypeType::MU: {
      const auto &[idx_, arms] = t->Mu();
      for (const auto &[a_, typ] : arms) {
        Rec(typ);
      }
      return;
    }

    case TypeType::EXISTS: {
      const auto &[alpha, tt] = t->Exists();
      Rec(tt);
      return;
    }

    case TypeType::FORALL: {
      const auto &[alpha, tt] = t->Forall();
      Rec(tt);
      return;
    }

    case TypeType::RECORD:
      for (const auto &[l_, c] : t->Record())
        Rec(c);
      return;

    case TypeType::EVAR: {
      const Type *b = t->EVar().GetBound();
      if (b == nullptr) {
        // Then this is a free evar.
        s.insert(t->EVar());
      } else {
        // but if not, it could have free evars within it!
        Rec(b);
      }
      return;
    }

    case TypeType::REF:
      Rec(t->Ref());
      return;

    case TypeType::VEC:
      Rec(t->Vec());
      return;

    case TypeType::STRING:
      return;

    case TypeType::INT:
      return;

    case TypeType::WORD:
      return;

    case TypeType::FLOAT:
      return;

    case TypeType::BOOL:
      return;

    case TypeType::OBJ:
      return;

    case TypeType::LAYOUT:
      return;
    }
  };

  for (const Type *t : tv) Rec(t);
  return std::vector<EVar>(s.begin(), s.end());
}

static void UnifyEx(const std::function<std::string()> &error_context,
                    const VMap &vmap,
                    const Type *t1, const Type *t2) {
  auto Error = [&error_context]() {
      return std::format(
          ARED("Unification error") ":\n"
          "In: {}\n\n",
          error_context());
    };

  // First, handle existential variables.
  if (t1->type == TypeType::EVAR || t2->type == TypeType::EVAR) {
    // For bound existential variables we can just
    // recurse into them.
    auto IsBoundEvar = [](const Type *t) -> const Type * {
        if (t->type != TypeType::EVAR) return nullptr;
        return t->EVar().GetBound();
      };

    if (const Type *at = IsBoundEvar(t1)) {
      UnifyEx(error_context, vmap, at, t2);
      return;
    }

    if (const Type *bt = IsBoundEvar(t2)) {
      UnifyEx(error_context, vmap, t1, bt);
      return;
    }

    // Now we know that at least one of them is a free evar.
    if (t1->type == TypeType::EVAR && t2->type == TypeType::EVAR) {
      // Both evars.
      const EVar &a = t1->EVar();
      const EVar &b = t2->EVar();
      // Nothing to do if it's the same variable.
      if (EVar::SameEVar(a, b))
        return;

      // Otherwise, it cannot fail the occurs check, so we just
      // set one of them to the other.
      a.Set(t2);
    } else {
      const bool evar_left = t1->type == TypeType::EVAR;
      if (!evar_left) { CHECK(t2->type == TypeType::EVAR); }
      const EVar &e = evar_left ? t1->EVar() : t2->EVar();
      const Type *t = evar_left ? t2 : t1;
      CHECK(t->type != TypeType::EVAR);

      if (EVar::Occurs(e, t)) {
        LOG(FATAL) << Error() <<
          "Type mismatch (circularity):\n"
          "During unification, an existential variable occurred "
          "in the type that it needed to be unified with. The "
          "type was:\n" <<
          TypeString(t) <<
          "\nAnd the evar was: " << e.ToString();
      } else {
        e.Set(t);
      }
    }
    return;
  }

  // Otherwise, the types need to have the same constructor.
  CHECK(t1->type == t2->type) << Error() <<
    "Tycon mismatch:\n"
    "During unification, the types did not have the same "
    "outer constructor. One was " << TypeTypeString(t1->type) <<
    " and the other was " << TypeTypeString(t2->type) << ". Full "
    "types:\n" << TypeString(t1) << "\n(vs)\n" << TypeString(t2);

  typedef
    const std::vector<std::pair<std::string, const Type *>> &
    (il::Type::*RecordOrSumField)() const;

  auto RecordOrSum = [&error_context, &Error, &vmap](
      const char *record_what,
      // Member function to extract the field.
      RecordOrSumField Field,
      const Type *t1, const Type *t2) {
      const auto &field1 = std::invoke(Field, t1);
      const auto &field2 = std::invoke(Field, t2);
      CHECK(field1.size() == field2.size()) << Error() <<
        "Labels in " << record_what <<
        " type do not match during unification.\n"
        "There are a different number:\n" <<
        TypeString(t1) << "\nvs\n" << TypeString(t2);

      for (int i = 0; i < (int)field1.size(); i++) {
        const auto &[l1, c1] = field1[i];
        const auto &[l2, c2] = field2[i];
        CHECK(l1 == l2) << Error() <<
          "Labels in " << record_what <<
          " type do not match during unification.\n"
          "The label " << l1 << " did not match " << l2 << " in:\n" <<
          TypeString(t1) << "\nvs\n" << TypeString(t2);
        UnifyEx(error_context, vmap, c1, c2);
      }
    };

  switch (t1->type) {
  case TypeType::EVAR:
    LOG(FATAL) << "Bug: This is checked above.\n";
    break;

  case TypeType::VAR: {
    const auto &[v1, ts1] = t1->Var();
    const auto &[v2, ts2] = t2->Var();

    // When would we have kind > 0?
    CHECK(ts1.empty() && ts2.empty()) << "Unimplemented";

    // If the variable appears in the map, it must have the
    // corresponding value.
    for (const auto &[a1, a2] : vmap) {
      if (a1 == v1) {
        CHECK(a2 == v2) << Error() << "Bound type variables "
          "are not equal. On one side, the type was " << a1 << " and "
          "we expected to see " << a2 << " on the other, but we got " <<
          v2 << ".";
        return;
      } else {
        CHECK(a2 != v2) << Error() << "Bound type variables "
          "are not equal. On one side, the type was " << a2 << " and "
          "we expected to see " << a1 << " on the other, but we got " <<
          v1 << ".";
        // (But keep searching.)
      }
    }

    // If we get here, then the type variable must be free on both
    // sides. Then it has to be exactly equal.

    // XXX actually in this case we could easily support higher kinds?
    CHECK(v1 == v2) << Error() << "Free type variables "
      "are not equal. Got " << v1 << " and " << v2 << ".";

    return;
  }

  case TypeType::SUM:
    RecordOrSum("sum", &Type::Sum, t1, t2);
    return;

  case TypeType::ARROW: {
    const auto &[dom1, cod1] = t1->Arrow();
    const auto &[dom2, cod2] = t2->Arrow();
    UnifyEx(error_context, vmap, dom1, dom2);
    UnifyEx(error_context, vmap, cod1, cod2);
    break;
  }

  case TypeType::MU: {
    const auto &[idx1, arms1] = t1->Mu();
    const auto &[idx2, arms2] = t2->Mu();
    CHECK(idx1 == idx2) << Error() << "Indices in two mu types do "
      "not match. They may be different types from a mutually-recursive "
      "bundle or just unrelated.\nIn:\n" <<
      TypeString(t1) << "\nvs\n" << TypeString(t2);

    CHECK(arms1.size() == arms2.size()) <<
      Error() << "The lengths of two mu types do not agree; this "
      "means they must be from different mutually-recursive bundles:"
      "\nIn:\n" <<
      TypeString(t1) << "\nvs\n" << TypeString(t2);

    // Order here is important (that's what the index denotes), so we just
    // do a pairwise comparison of arms.

    VMap vmap_rec = vmap;
    for (int i = 0; i < (int)arms1.size(); i++) {
      const auto &[a1, arm1_in] = arms1[i];
      const auto &[a2, arm2_in] = arms2[i];

      vmap_rec.emplace_back(a1, a2);
      UnifyEx(error_context, vmap_rec, arm1_in, arm2_in);
      vmap_rec.pop_back();

      /*
      // Rename both type variables to the same thing.
      const auto &[a, arm1] = elab->pool->AlphaVaryType(a1, arm1_in);
      const Type *arm2 = elab->pool->SubstType(
          elab->pool->VarType(a, {}), a2, arm2_in);
      UnifyEx(what, vmap, arm1, arm2);
      */
    }

    break;
  }

  case TypeType::EXISTS:
    LOG(FATAL) << Error() << "Bug: Not expecting Exists type before closure "
      "conversion!";
    return;

  case TypeType::FORALL:
    LOG(FATAL) << Error() << "Bug: Not expecting Forall type before closure "
      "conversion!";
    return;

  case TypeType::RECORD:
    RecordOrSum("record", &Type::Record, t1, t2);
    return;

  case TypeType::REF:
    UnifyEx(error_context, vmap, t1->Ref(), t2->Ref());
    return;

  case TypeType::VEC:
    UnifyEx(error_context, vmap, t1->Vec(), t2->Vec());
    return;

  case TypeType::STRING:
    return;

  case TypeType::INT:
    return;

  case TypeType::WORD:
    return;

  case TypeType::FLOAT:
    return;

  case TypeType::BOOL:
    return;

  case TypeType::OBJ:
    return;

  case TypeType::LAYOUT:
    return;
  }
}

void Unification::Unify(const std::function<std::string()> &error_context,
                        const Type *t1, const Type *t2) {
  UnifyEx(error_context, {}, t1, t2);
}

}  // namespace il
