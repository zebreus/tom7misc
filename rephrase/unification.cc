
#include "unification.h"

#include <set>
#include <vector>
#include <memory>
#include <string>
#include <utility>
#include <mutex>
#include <cstdint>
#include <cinttypes>
#include <functional>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "il.h"

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
  return StringPrintf("_EVAR_" PRIi64 "_", GetCell()->id);
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

  case TypeType::RECORD:
    for (const auto &[l_, c] : t->Record()) {
      if (Occurs(e, c)) return true;
    }
    return false;

  case TypeType::EVAR:
    return SameEVar(e, t->EVar());

  case TypeType::REF:
    return Occurs(e, t->Ref());

  case TypeType::STRING:
    return false;

  case TypeType::INT:
    return false;

  case TypeType::FLOAT:
    return false;
  }
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

    case TypeType::MU:
      LOG(FATAL) << "Unimplemented: Mu in FreeEVarsInType";
      return;

    case TypeType::RECORD:
      for (const auto &[l_, c] : t->Record())
        Rec(c);
      return;

    case TypeType::EVAR:
      if (t->EVar().GetBound() == nullptr) {
        s.insert(t->EVar());
      }
      return;

    case TypeType::REF:
      Rec(t->Ref());
      return;

    case TypeType::STRING:
      return;

    case TypeType::INT:
      return;

    case TypeType::FLOAT:
      return;
    }
  };

  Rec(t);
  return std::vector<EVar>(s.begin(), s.end());
}

static void UnifyEx(std::string_view what,
                    const VMap &vmap,
                    const Type *t1, const Type *t2) {
  // First, handle existential variables.
  if (t1->type == TypeType::EVAR || t2->type == TypeType::EVAR) {
    // For bound existential variables we can just
    // recurse into them.
    auto IsBoundEvar = [](const Type *t) -> const Type * {
        if (t->type != TypeType::EVAR) return nullptr;
        return t->EVar().GetBound();
      };

    if (const Type *at = IsBoundEvar(t1)) {
      UnifyEx(what, vmap, at, t2);
      return;
    }

    if (const Type *bt = IsBoundEvar(t2)) {
      UnifyEx(what, vmap, t1, bt);
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
        LOG(FATAL) <<
          "(" << what << ") "
          "Type mismatch (circularity):\n"
          "During unification, an existential variable occurred "
          "in the type that it needed to be unified with. The "
          "type was:\n" <<
          TypeString(t);
      } else {
        e.Set(t);
      }
    }
    return;
  }

  // Otherwise, the types need to have the same constructor.
  CHECK(t1->type == t2->type) <<
    "(" << what << ") "
    "Tycon mismatch:\n"
    "During unification, the types did not have the same "
    "outer constructor. One was " << TypeTypeString(t1->type) <<
    " and the other was " << TypeTypeString(t2->type) << ". Full "
    "types:\n" << TypeString(t1) << "\n(vs)\n" << TypeString(t2);

  typedef
    const std::vector<std::pair<std::string, const Type *>> &
    (il::Type::*RecordOrSumField)() const;

  auto RecordOrSum = [what, &vmap](
      const char *record_what,
      // XXX pass member
      RecordOrSumField Field,
      const Type *t1, const Type *t2) {
      const auto &field1 = std::invoke(Field, t1);
      const auto &field2 = std::invoke(Field, t2);
      CHECK(field1.size() == field2.size()) <<
        "(" << what << ") Labels in " << record_what <<
        " type do not match during unification.\n"
        "There are a different number:\n" <<
        TypeString(t1) << "\nvs\n" << TypeString(t2);

      for (int i = 0; i < (int)field1.size(); i++) {
        const auto &[l1, c1] = field1[i];
        const auto &[l2, c2] = field2[i];
        CHECK(l1 == l2) <<
          "(" << what << ") "
          "Labels in " << record_what <<
          " type do not match during unification.\n"
          "The label " << l1 << " did not match " << l2 << " in:\n" <<
          TypeString(t1) << "\nvs\n" << TypeString(t2);
        UnifyEx(what, vmap, c1, c2);
      }
    };

  switch (t1->type) {
  case TypeType::EVAR:
    LOG(FATAL) << "Bug: This is checked above.\n";
    break;

  case TypeType::VAR:
    LOG(FATAL) << "Unimplemented";
    break;

  case TypeType::SUM:
    RecordOrSum("sum", &Type::Sum, t1, t2);
    return;

  case TypeType::ARROW: {
    const auto &[dom1, cod1] = t1->Arrow();
    const auto &[dom2, cod2] = t2->Arrow();
    UnifyEx(what, vmap, dom1, dom2);
    UnifyEx(what, vmap, cod1, cod2);
    break;
  }

  case TypeType::MU:
    LOG(FATAL) << "Unimplemented: Unify mu";
    break;

  case TypeType::RECORD:
    RecordOrSum("record", &Type::Record, t1, t2);
    return;

  case TypeType::REF:
    UnifyEx(what, vmap, t1->Ref(), t2->Ref());
    return;

  case TypeType::STRING:
    return;

  case TypeType::INT:
    return;

  case TypeType::FLOAT:
    return;
  }
}

void Unification::Unify(std::string_view what,
                        const Type *t1, const Type *t2) {
  UnifyEx(what, {}, t1, t2);
}

}  // namespace il
