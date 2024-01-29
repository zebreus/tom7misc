
#include "unification.h"

#include <vector>
#include <memory>
#include <string>
#include <utility>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "il.h"

namespace il {

EVar::EVar() : cell(std::make_shared<EVarCell>()) {
  // Hemlock/Aphasia frontend keeps an integer counter here, which
  // could be useful for identity tests or debugging output. But
  // we actually just need the pointer.
}

// TODO: Improve, probably by keeping some counter?
std::string EVar::ToString() const {
  return StringPrintf("_EVAR_%p_", GetCell().get());
}


// When we descend under a quantifier, we need to keep track of
// the correspondence of the bound variable on each side.
using VMap = std::vector<std::pair<std::string, std::string>>;

std::shared_ptr<EVar::EVarCell> EVar::GetCell() const {
  if (const Type *next = cell->GetBound()) {
    if (next->type == TypeType::EVAR) {
      std::shared_ptr<EVar::EVarCell> ptd = next->evar.GetCell();
      cell = ptd;
    }
  }

  // Here, either we found the deepest cell or it's free, so
  // we are done.
  return cell;
}

bool EVar::SameEVar(const EVar &a,
                    const EVar &b) {
  return a.GetCell().get() == b.GetCell().get();
}

bool EVar::Occurs(const EVar &e, const Type *t) {
  switch (t->type) {
  case TypeType::VAR:
    return false;
  case TypeType::SUM:
    for (const auto &[l_, c] : t->str_children) {
      if (Occurs(e, c)) return true;
    }
    return false;
  case TypeType::ARROW:
    return Occurs(e, t->a) || Occurs(e, t->b);
  case TypeType::MU:
    LOG(FATAL) << "Unimplemented: Mu in Occurs";
    return false;
  case TypeType::RECORD:
    for (const auto &[l_, c] : t->str_children) {
      if (Occurs(e, c)) return true;
    }
    return false;
  case TypeType::EVAR:
    return SameEVar(e, t->evar);
  case TypeType::REF:
    return Occurs(e, t->a);
  case TypeType::STRING:
    return false;
  case TypeType::INT:
    return false;
  }
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
        return t->evar.GetBound();
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
      const EVar &a = t1->evar;
      const EVar &b = t2->evar;
      // Nothing to do if it's the same variable.
      if (EVar::SameEVar(a, b))
        return;

      // Otherwise, it cannot fail the occurs check, so we just
      // set one of them to the other.
      a.Set(t2);
    } else {
      const bool evar_left = t1->type == TypeType::EVAR;
      if (!evar_left) { CHECK(t2->type == TypeType::EVAR); }
      const EVar &e = evar_left ? t1->evar : t2->evar;
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

  auto RecordOrSum = [what, &vmap](const char *record_what,
                                   const Type *t1, const Type *t2) {
      CHECK(t1->str_children.size() == t2->str_children.size()) <<
        "(" << what << ") Labels in " << record_what <<
        " type do not match during unification.\n"
        "There are a different number:\n" <<
        TypeString(t1) << "\nvs\n" << TypeString(t2);

      for (int i = 0; i < (int)t1->str_children.size(); i++) {
        const auto &[l1, c1] = t1->str_children[i];
        const auto &[l2, c2] = t2->str_children[i];
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
    RecordOrSum("sum", t1, t2);
    return;

  case TypeType::ARROW:
    UnifyEx(what, vmap, t1->a, t2->a);
    UnifyEx(what, vmap, t1->b, t2->b);
    break;

  case TypeType::MU:
    LOG(FATAL) << "Unimplemented";
    break;

  case TypeType::RECORD:
    RecordOrSum("record", t1, t2);
    return;

  case TypeType::REF:
    UnifyEx(what, vmap, t1->a, t2->a);
    return;

  case TypeType::STRING:
    return;

  case TypeType::INT:
    return;

  }
}

void Unification::Unify(std::string_view what,
                        const Type *t1, const Type *t2) {
  UnifyEx(what, {}, t1, t2);
}

}  // namespace il
