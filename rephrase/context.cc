#include "context.h"

#include <format>
#include <utility>
#include <vector>
#include <string>
#include <variant>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "functional-map.h"
#include "il.h"
#include "unification.h"
#include "util.h"

namespace il {

ElabContext::ElabContext(
    const std::vector<std::pair<std::string, VarInfo>> &exp,
    const std::vector<std::pair<std::string, TypeVarInfo>> &typ,
    const std::vector<std::pair<std::string, ObjVarInfo>> &obj) {
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

  for (const auto &[s, o] : obj) {
    init.push_back(
        std::make_pair(
            std::make_pair(s, V::OBJ), AnyVarInfo{o}));
  }

  fm = FunctionalMap(init);
}

bool ElabContext::HasEVar(const EVar &e) const {
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

const VarInfo *ElabContext::FindByILVar(const std::string &s) const {
  // PERF: As in HasEVar. In this case, we might want to keep a
  // parallel map indexed by il vars? In any case, just do the
  // export once.
  const auto m = fm.Export();
  for (const auto &[k, v] : m) {
    if (k.second == V::EXP) {
      const VarInfo *vi = std::get_if<VarInfo>(&v);
      CHECK(vi != nullptr) << "Bug: Expression vars always hold VarInfo.";
      // Look up the VarInfo in the original context, since Export
      // makes a copy.
      if (vi->var == s) {
        return Find(k.first);
      }
    }
  }
  return nullptr;
}

std::string ElabContext::VarInfoString(const VarInfo &vi) {
  std::string ret;
  if (vi.primop.has_value()) {
    AppendFormat(&ret, "primop\n");
  } else {
    std::string tyvars;
    if (!vi.tyvars.empty()) {
      tyvars = "(" + Util::Join(vi.tyvars, ", ") + ") ";
    }
    AppendFormat(&ret, "{} : {}{}",
                 vi.var,
                 tyvars,
                 TypeString(vi.type));

    if (vi.ctor.has_value()) {
      const auto &[idx, mu_type, lab] = vi.ctor.value();
      AppendFormat(&ret, " ctor #{}({}) {}\n",
                   idx, TypeString(mu_type), lab);
    }
    ret.push_back('\n');
  }
  return ret;
}

std::string ElabContext::ToString() const {
  const auto m = fm.Export();
  std::string ret;
  for (const auto &[k, v] : m) {
    switch (k.second) {
    case V::EXP: {
      const VarInfo *vi = std::get_if<VarInfo>(&v);
      CHECK(vi != nullptr) << "Bug: Expression vars always hold VarInfo.";
      AppendFormat(&ret, "{} => {} ",
                   k.first,
                   VarInfoString(*vi));
      break;
    }

    case V::TYPE: {
      const TypeVarInfo *tvi = std::get_if<TypeVarInfo>(&v);
      CHECK(tvi != nullptr) << "Bug: Type variables always hold TypeVarInfo.";
      std::string tyvars;
      if (!tvi->tyvars.empty()) {
        if (tvi->tyvars.size() == 1) {
          tyvars = "Λ" + tvi->tyvars[0] + ".";
        } else {
          tyvars = "Λ(" + Util::Join(tvi->tyvars, ", ") + ").";
        }
      }

      AppendFormat(&ret, "type {} = {}{}\n", k.first, tyvars,
                   TypeString(tvi->type));
      break;
    }

    default:
      AppendFormat(&ret, "Unimplemented variable type!");
      break;
    }
  }
  return ret;
}

std::string PolyTypeString(const PolyType &pt) {
  return std::format("({}) {}", Util::Join(pt.first, ","),
                      TypeString(pt.second));
}

std::string Context::ToString() const {

  std::string ret;
  AppendFormat(&ret, "Exp vars:\n");
  for (const auto &[k, v] : expmap.Export()) {
    AppendFormat(&ret, "  {}: {}\n", k, PolyTypeString(v));
  }

  AppendFormat(&ret, "Global symbols:\n");
  for (const auto &[k, v] : symmap.Export()) {
    AppendFormat(&ret, "  {}: {}\n", k, PolyTypeString(v));
  }

  AppendFormat(&ret, "Types:\n");
  for (const auto &[k, v_] : symmap.Export()) {
    AppendFormat(&ret, "  {}\n", k);
  }
  return ret;
}

}  // il
