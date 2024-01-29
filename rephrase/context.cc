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

}  // il
