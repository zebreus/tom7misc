#ifndef _REPHRASE_UNIFICATION_H
#define _REPHRASE_UNIFICATION_H

#include <cstdint>
#include <mutex>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <span>

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

  // Returns nullptr if not (yet) bound.
  const Type *GetBound() const { return GetCell()->GetBound(); }
  void Set(const Type *t) const { return GetCell()->Set(t); }

  static bool Occurs(const EVar &a, const Type *t);

  // Get the set of distinct free EVars in the type t.
  static std::vector<EVar> FreeEVarsInType(const Type *t);
  static std::vector<EVar> FreeEVarsInTypes(
      std::span<const Type *const> tv);

  // It typically only makes sense to compare free EVars.
  static bool SameEVar(const EVar &a, const EVar &b);
  static bool LessEVar(const EVar &a, const EVar &b);

  // For debugging output.
  std::string ToString() const;

 private:
  struct EVarCell {
    EVarCell() = delete;
    EVarCell(int64_t id) : id(id) {}
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
    // If free, then this nonzero counter can be used to uniquely
    // identify the EVar (will be 1:1 with the pointer, but
    // deterministic). If bound, it is generally ignored.
    const int64_t id = 1;
  };

  // Get the deepest EVar that is free or bound to
  // something other than an Evar. Performs path
  // compression.
  std::shared_ptr<EVarCell> GetCell() const;

  mutable std::shared_ptr<EVarCell> cell;
};

struct Unification {

  static void Unify(const std::function<std::string()> &error_context,
                    const Type *t1, const Type *t2);

};

}  // unification

#endif
