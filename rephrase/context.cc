#include "context.h"

#include <utility>
#include <vector>

namespace il {

Context::Context(
    const std::vector<std::pair<std::string, PolyType>> &exp,
    const std::vector<std::pair<std::string, SingletonKind>> &typ) {
  std::vector<std::pair<KeyType, VarInfo>> init;
  for (const auto &[s, pt] : exp) {
    init.push_back(
        std::make_pair(
            std::make_pair(s, V::EXP), VarInfo{.data = pt}));
  }
  for (const auto &[s, k] : typ) {
    init.push_back(
        std::make_pair(
            std::make_pair(s, V::TYPE), VarInfo{.data = k}));
  }

  fm = FunctionalMap(init);
}

}  // il
