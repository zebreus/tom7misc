#include "context.h"

#include <utility>
#include <vector>

namespace il {

Context::Context(
    const std::vector<std::pair<std::string, VarInfo>> &exp,
    const std::vector<std::pair<std::string, TypeVarInfo>> &typ) {
  std::vector<std::pair<KeyType, AnyVarInfo>> init;
  for (const auto &[s, pt] : exp) {
    init.push_back(
        std::make_pair(
            std::make_pair(s, V::EXP), AnyVarInfo{pt}));
  }
  for (const auto &[s, k] : typ) {
    init.push_back(
        std::make_pair(
            std::make_pair(s, V::TYPE), AnyVarInfo{k}));
  }

  fm = FunctionalMap(init);
}

bool Context::HasEVar(const EVar &e) const {
  // PERF: This is linear+ in the size of the context.
  // We could at least do it without copying. We will
  // also check multiple free EVars in the same term,
  // so we might want to only do the export once.
  const auto &m = fm.Export();
  for (const auto &[k, v] : m) {
    if (k.second == V::EXP) {
      // Only expression variables.
      const VarInfo *vi = std::get_if<VarInfo>(&v);
      CHECK(vi != nullptr) << "Bug: Expression vars always hold VarInfo.";
      if (EVar::Occurs(e, vi->type)) {
        return true;
      }
    }
  }

  return false;
}

}  // il
