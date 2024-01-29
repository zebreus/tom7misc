
#ifndef _REPHRASE_CONTEXT_H
#define _REPHRASE_CONTEXT_H

#include <string>

#include "functional-map.h"
#include "il.h"

namespace il {

struct VarInfo {
  // Polymorphic types only exist at the outermost level of the type
  // language (and only for bound variables). tyvars may be empty for
  // simple variables.
  std::vector<std::string> tyvars;
  const Type *type = nullptr;

  // The il variable that the el var refers to.
  // This will be blank if a primop (below).
  std::string var;

  // As an implementation detail, some identifiers have special status
  // because they reference builtins. This is only set by the
  // initial context.
  std::optional<Primop> primop;
};

struct TypeVarInfo {
  // Conveniently (or confusingly), a transparent type variable is also
  // given a (singleton) kind that has the same exact shape. There are
  // bound type variables and a body.
  std::vector<std::string> tyvars;
  const Type *type = nullptr;
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

  // When inserting, the returned context refers to the existing one,
  // so it must have a shorter lifespan!

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


private:
  enum class V {
    // e.g. 'int' or 'list'
    TYPE,
    // e.g. 'x' or '+'
    EXP,
    // e.g. 'nil' or '::'
    // XXX: These should probably just be vars, but be marked as
    // constructors in VarInfo.
    CTOR,
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
