#include "context.h"

#include <utility>
#include <vector>
#include <string>
#include <variant>

#include "functional-map.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"
#include "il.h"

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
  const auto m = fm.Export();
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

std::string Context::ToString() const {
  const auto m = fm.Export();
  std::string ret;
  for (const auto &[k, v] : m) {
    switch (k.second) {
    case V::EXP: {
      const VarInfo *vi = std::get_if<VarInfo>(&v);
      CHECK(vi != nullptr) << "Bug: Expression vars always hold VarInfo.";
      if (vi->primop.has_value()) {
        StringAppendF(&ret, "%s primop\n", k.first.c_str());
      } else {
        std::string tyvars;
        if (!vi->tyvars.empty()) {
          tyvars = "(" + Util::Join(vi->tyvars, ", ") + ") ";
        }
        StringAppendF(&ret, "%s => %s : %s%s",
                      k.first.c_str(),
                      vi->var.c_str(),
                      tyvars.c_str(),
                      TypeString(vi->type).c_str());

        if (vi->ctor.has_value()) {
          const auto &[idx, lab] = vi->ctor.value();
          StringAppendF(&ret, " ctor #%d, %s\n", idx, lab.c_str());
        }
        ret.push_back('\n');
      }
      break;
    }

    case V::TYPE: {
      const TypeVarInfo *tvi = std::get_if<TypeVarInfo>(&v);
      CHECK(tvi != nullptr) << "Bug: Type variables always hold TypeVarInfo.";
      std::string tyvars;
      /*
      if (!tvi->var.empty()) {
        CHECK(tvi->type == nullptr);

        StringAppendF(&ret, "type var %s => %s : 0\n",
                      k.first.c_str(),
                      tvi->var.c_str());

                      } else { */
        if (!tvi->tyvars.empty()) {
          if (tvi->tyvars.size() == 1) {
            tyvars = "Λ" + tvi->tyvars[0] + ".";
          } else {
            tyvars = "Λ(" + Util::Join(tvi->tyvars, ", ") + ").";
          }
        }

        StringAppendF(&ret, "type %s = %s%s\n",
                      k.first.c_str(),
                      tyvars.c_str(),
                      TypeString(tvi->type).c_str());
        // }
      break;
    }

    default:
      StringAppendF(&ret, "Unimplemented variable type!");
      break;
    }
  }
  return ret;
}

}  // il
