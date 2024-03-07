
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
};

struct ObjVarInfo {
  // An object declaration just tells us the types of fields that
  // we might find in the object.
  std::unordered_map<std::string, const Type *> fields;
};

// Elaboration context.
//
struct ElabContext {

  // Empty context.
  ElabContext() = default;
  ~ElabContext() = default;
  // Initialize with a set of bindings.
  ElabContext(const std::vector<std::pair<std::string, VarInfo>> &exp,
              const std::vector<std::pair<std::string, TypeVarInfo>> &typ,
              const std::vector<std::pair<std::string, ObjVarInfo>> &obj);

  // Expression variables.
  ElabContext Insert(const std::string &s, VarInfo vi) const {
    return ElabContext(fm.Insert(std::make_pair(s, V::EXP),
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

  ElabContext InsertType(const std::string &s, TypeVarInfo tvi) const {
    return ElabContext(fm.Insert(std::make_pair(s, V::TYPE),
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

  ElabContext InsertObj(const std::string &s, ObjVarInfo ovi) const {
    return ElabContext(fm.Insert(std::make_pair(s, V::OBJ),
                             {std::move(ovi)}));
  }

  const ObjVarInfo *FindObj(const std::string &s) const {
    if (const AnyVarInfo *avi = fm.FindPtr(std::make_pair(s, V::OBJ))) {
      const ObjVarInfo *ovi = std::get_if<ObjVarInfo>(avi);
      CHECK(ovi != nullptr) << "Bug: Object vars always hold ObjVarInfo.";
      return ovi;
    } else {
      return nullptr;
    }
  }


  // True if the context has the (free) EVar inside the type of any
  // expression variable. This is used to determine what type
  // variables can be generalized in a val declaration.
  // Linear time.
  bool HasEVar(const EVar &e) const;

  // Find the VarInfo for an il variable. Returns nullptr if not
  // bound. Linear time.
  const VarInfo *FindByILVar(const std::string &s) const;

  // For debugging.
  std::string ToString() const;

  static std::string VarInfoString(const VarInfo &vi);

private:
  enum class V {
    // e.g. 'int' or 'list'
    TYPE,
    // e.g. 'x' or '+' or 'SOME'
    EXP,
    // Name declared in an "object" declaration.
    OBJ,
  };

  using AnyVarInfo = std::variant<VarInfo, TypeVarInfo, ObjVarInfo>;

  using KeyType = std::pair<std::string, V>;
  using FM = FunctionalMap<KeyType, AnyVarInfo>;

  explicit ElabContext(FM &&fm) : fm(fm) {}

  // Otherwise, it's just a functional map.
  FM fm;
};

using PolyType = std::pair<std::vector<std::string>, const Type *>;

// Regular IL context.
// In the IL type language, we just know what set of type variables
// are bound (they all have kind 0).
struct Context {

  // Empty context.
  Context() = default;
  ~Context() = default;
  // Initialize with a set of bindings.
  Context(const std::vector<std::pair<std::string, PolyType>> &exps);

  // Expression variables.
  Context Insert(const std::string &s, PolyType pt) const {
    return Context(expmap.Insert(s, std::move(pt)), symmap, typeset);
  }

  const PolyType *Find(const std::string &s) const {
    return expmap.FindPtr(s);
  }

  Context InsertSym(const std::string &s, PolyType pt) const {
    return Context(expmap, symmap.Insert(s, std::move(pt)), typeset);
  }

  const PolyType *FindSym(const std::string &s) const {
    return symmap.FindPtr(s);
  }

  Context InsertType(const std::string &s) const {
    return Context(expmap, symmap, typeset.Insert(s, {}));
  }

  bool FindType(const std::string &s) const {
    return typeset.FindPtr(s) != nullptr;
  }

  // For debugging.
  std::string ToString() const;

private:

  struct Unit {};
  template<class T>
  using FunctionalSet = FunctionalMap<T, Unit>;

  Context(FunctionalMap<std::string, PolyType> e,
          FunctionalMap<std::string, PolyType> s,
          FunctionalSet<std::string> t) : expmap(std::move(e)),
                                          symmap(std::move(s)),
                                          typeset(std::move(t)) {}

  FunctionalMap<std::string, PolyType> expmap, symmap;
  FunctionalSet<std::string> typeset;
};


}  // il

#endif
