#include "context.h"

Context::Context(
    const std::vector<std::pair<std::string, PolyType>> &exp,
    const std::vector<std::pair<std::string, int>> &typ) {
  std::vector<std::pair<KeyType, VarInfo>> init;
  for (const auto &[s, pt] : exp) {
    init.push_back(
        std::make_pair(
            std::make_pair(s, V::EXP), VarInfo{.type = pt}));
  }
  for (const auto &[s, i] : typ) {
    init.push_back(
        std::make_pair(
            std::make_pair(s, V::TYPE), VarInfo{.kind = i}));
  }
}
