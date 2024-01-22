
#ifndef _REPHRASE_CONTEXT_H
#define _REPHRASE_CONTEXT_H

#include <string>

#include "functional-map.h"
#include "il.h"

// Elaboration context.
//
struct Context {

  // Empty context.
  Context() = default;
  // Initialize with a set of bindings.
  Context(const std::vector<std::pair<std::string, const il::Type *>> &exp,
          const std::vector<std::pair<std::string, int>> &typ);

  // When inserting, the returned context refers to the existing one,
  // so it must have a shorter lifespan!

  // Expression variables.
  Context Insert(const std::string &s, const il::Type *type) const {
    return Context(fm.Insert(std::make_pair(s, V::EXP),
                             {.type = type}));
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
    // XXX this should probably be "poly type"
    const il::Type *type = nullptr;
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
