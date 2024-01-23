
#ifndef _REPHRASE_CONTEXT_H
#define _REPHRASE_CONTEXT_H

#include <string>

#include "functional-map.h"
#include "il.h"

// Polymorphic types only exist at the outermost level of the type
// language (and only for bound variables). tyvars may be empty for
// simple variables.
struct PolyType {
  std::vector<std::string> tyvars;
  const il::Type *type = nullptr;
};

// Elaboration context.
//
struct Context {

  // Empty context.
  Context() = default;
  // Initialize with a set of bindings.
  Context(const std::vector<std::pair<std::string, PolyType>> &exp,
          const std::vector<std::pair<std::string, int>> &typ);

  // When inserting, the returned context refers to the existing one,
  // so it must have a shorter lifespan!

  // Expression variables.
  Context Insert(const std::string &s, PolyType pt) const {
    return Context(fm.Insert(std::make_pair(s, V::EXP),
                             {.type = std::move(pt)}));
  }

  Context InsertType(const std::string &s, int arity) const {
    return Context(fm.Insert(std::make_pair(s, V::TYPE),
                             {.kind = arity}));
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
    // More here, e.g. constructor status
    PolyType type;
    // Arity for type constructors.
    int kind = 0;
  };

  using KeyType = std::pair<std::string, V>;
  using FM = FunctionalMap<KeyType, VarInfo>;

  explicit Context(FM &&fm) : fm(fm) {}

  // Otherwise, it's just a functional map.
  FM fm;
};

#endif
