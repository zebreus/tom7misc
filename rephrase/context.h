
#ifndef _REPHRASE_CONTEXT_H
#define _REPHRASE_CONTEXT_H

#include <string>

#include "functional-map.h"
#include "il.h"

namespace il {

struct VarInfo {
  // Polymorphic types only exist at the outermost level of the type
  // language (and only for bound variables and constructors). tyvars
  // may be empty for simple variables.
  std::vector<std::string> tyvars;
  const Type *type = nullptr;

  // The il variable that the el var refers to.
  // This will be blank if a primop (below).
  //
  // XXX: This makes sense as an elaboration context, but we
  // probably will want a "regular il context" so that we can do
  // stuff like type-checking and optimization? That should just
  // be indexed by il vars.
  std::string var;

  // As an implementation detail, some identifiers have special status
  // because they reference builtins. This is only set by the
  // initial context.
  std::optional<Primop> primop;

  // Some identifiers are datatype constructors. These do not have
  // il variables because they are not translated into variables;
  // they are an injection into a sum and then into a mu.
  // Components are: mu index (XXX needed?),
  //   full mu type (XXX needed? it's in the arrow), sum label
  std::optional<std::tuple<int, const Type *, std::string>> ctor;
};

struct TypeVarInfo {
  // Conveniently (or confusingly), a transparent bound type variable
  // is also given a (singleton) kind that has the same shape:
  //  Λ(α, β, ...).τ
  //
  // For a simple type variable (like those bound by the programmer
  // in a datatype declaration), we just have type = Var(α). These
  // always have kind 0.
  std::vector<std::string> tyvars;
  const Type *type = nullptr;

  #if 0
  // We also have explicit type variables, like in a datatype declaration.
  // These always have kind 0. This is the il type variable that the
  // identifier refers to. The type pointer above will be null.
  std::string var;
  #endif
};

// Elaboration context.
//
struct Context {

  // Empty context.
  Context() = default;
  ~Context() = default;
  // Initialize with a set of bindings.
  Context(const std::vector<std::pair<std::string, VarInfo>> &exp,
          const std::vector<std::pair<std::string, TypeVarInfo>> &typ);

  // Expression variables.
  Context Insert(const std::string &s, VarInfo vi) const {
    return Context(fm.Insert(std::make_pair(s, V::EXP),
                             {std::move(vi)}));
  }

  const VarInfo *Find(const std::string &s) const {
    if (const AnyVarInfo *avi = fm.FindPtr(std::make_pair(s, V::EXP))) {
      const VarInfo *vi = std::get_if<VarInfo>(avi);
      CHECK(vi != nullptr) << "Bug: Expression vars always hold VarInfo.";
      return vi;
    } else {
      return nullptr;
    }
  }

  Context InsertType(const std::string &s, TypeVarInfo tvi) const {
    return Context(fm.Insert(std::make_pair(s, V::TYPE),
                             {std::move(tvi)}));
  }

  const TypeVarInfo *FindType(const std::string &s) const {
    if (const AnyVarInfo *avi = fm.FindPtr(std::make_pair(s, V::TYPE))) {
      const TypeVarInfo *tvi = std::get_if<TypeVarInfo>(avi);
      CHECK(tvi != nullptr) << "Bug: Type vars always hold TypeVarInfo.";
      return tvi;
    } else {
      return nullptr;
    }
  }

  // True if the context has the (free) EVar inside the type of any
  // expression variable. This is used to determine what type
  // variables can be generalized in a val declaration.
  bool HasEVar(const EVar &e) const;

  // For debugging.
  std::string ToString() const;

private:
  enum class V {
    // e.g. 'int' or 'list'
    TYPE,
    // e.g. 'x' or '+' or 'SOME'
    EXP,
  };

  using AnyVarInfo = std::variant<VarInfo, TypeVarInfo>;

  using KeyType = std::pair<std::string, V>;
  using FM = FunctionalMap<KeyType, AnyVarInfo>;

  explicit Context(FM &&fm) : fm(fm) {}

  // Otherwise, it's just a functional map.
  FM fm;
};

}  // il

#endif
