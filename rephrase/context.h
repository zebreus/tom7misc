
#ifndef _REPHRASE_CONTEXT_H
#define _REPHRASE_CONTEXT_H

#include <string>

#include "functional-map.h"
#include "il.h"

namespace il {

// Polymorphic types only exist at the outermost level of the type
// language (and only for bound variables). tyvars may be empty for
// simple variables.
struct PolyType {
  std::vector<std::string> tyvars;
  const Type *type = nullptr;
};

// Conveniently (or confusingly), a transparent type variable is also
// given a (singleton) kind that has the same exact shape. There are
// bound type variables and a body.
using SingletonKind = PolyType;

// Elaboration context.
//
struct Context {

  // Empty context.
  Context() = default;
  ~Context() = default;
  // Initialize with a set of bindings.
  Context(const std::vector<std::pair<std::string, PolyType>> &exp,
          const std::vector<std::pair<std::string, SingletonKind>> &typ);

  // When inserting, the returned context refers to the existing one,
  // so it must have a shorter lifespan!

  // Expression variables.
  Context Insert(const std::string &s, PolyType pt) const {
    return Context(fm.Insert(std::make_pair(s, V::EXP),
                             {.data = std::move(pt)}));
  }

  const PolyType *Find(const std::string &s) const {
    if (const VarInfo *vi = fm.FindPtr(std::make_pair(s, V::EXP))) {
      return &vi->data;
    } else {
      return nullptr;
    }
  }

  Context InsertType(const std::string &s, SingletonKind kind) const {
    return Context(fm.Insert(std::make_pair(s, V::TYPE),
                             {.data = std::move(kind)}));
  }

  const SingletonKind *FindType(const std::string &s) const {
    if (const VarInfo *vi = fm.FindPtr(std::make_pair(s, V::TYPE))) {
      return &vi->data;
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
    CTOR,
  };

  struct VarInfo {
    // Since they have the same structure, this does dual
    // duty as the type (of an expression variable) or kind
    // (of a type).
    // TODO: More here, e.g. constructor status
    PolyType data;
  };

  using KeyType = std::pair<std::string, V>;
  using FM = FunctionalMap<KeyType, VarInfo>;

  explicit Context(FM &&fm) : fm(fm) {}

  // Otherwise, it's just a functional map.
  FM fm;
};

}  // il

#endif
