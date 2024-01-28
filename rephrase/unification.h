#ifndef _REPHRASE_UNIFICATION_H
#define _REPHRASE_UNIFICATION_H

#include <mutex>
#include <memory>
#include <string_view>

#include "base/logging.h"

namespace il {
struct Type;

// Existential type (meta)variable. This is a type that participates
// in unification; it may be unconstrained or it may have been
// determined by unification with a (partially) concrete type.
//
// This is a reference cell, which can be passed around by value.
// Accesses are thread-safe.
struct EVar {

  EVar();

  const Type *GetBound() const { return GetCell()->GetBound(); }
  void Set(const Type *t) const { return GetCell()->Set(t); }

  static bool SameEVar(const EVar &a, const EVar &b);
  static bool Occurs(const EVar &a, const Type *t);

 private:
  struct EVarCell {
    const Type *GetBound() const {
      std::unique_lock ml(m);
      return type;
    }

    void Set(const Type *t) const {
      std::unique_lock ml(m);
      CHECK(type == nullptr) << "Bug: EVar already set!";
      CHECK(t != nullptr);
      type = t;
    }

    mutable std::mutex m;
    // If null, then this is Free; otherwise it
    // is Bound to the type.
    mutable const Type *type = nullptr;
  };

  // Get the deepest EVar that is free or bound to
  // something other than an Evar. Performs path
  // compression.
  std::shared_ptr<EVarCell> GetCell() const;

  mutable std::shared_ptr<EVarCell> cell;
};

struct Unification {

  static void Unify(std::string_view what,
                    const Type *t1, const Type *t2);
};

}  // unification

#endif
