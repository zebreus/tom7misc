
#include "minitable.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "dense-int-set.h"
#include "inline-vector.h"
#include "prop.h"

// Props are stored with var 0, 1, 2, 3
using BestMap = std::unordered_map<uint16_t, Prop>;

// Get the truth table for the proposition.
uint16_t MiniTable::Eval(const Prop &p) {
  uint16_t tt = 0;
  for (uint8_t input = 0; input < 16; input++) {
    std::vector<bool> assignment(4, false);
    for (int v = 0; v < 4; v++) {
      assignment[v] = input & (1 << v);
    }

    bool value = EvaluateProp(assignment, p);
    tt = (tt << 1) | (value ? 0b1 : 0b0);
  }
  return tt;
}

MiniTable::MiniTable(uint32_t opts) {
  DenseIntSet done(65536);

  // Propositions of the given size, with the functions they compute.
  // We only keep one of each, since they are all equivalent.
  std::vector<BestMap> best;

  BestMap one;
  one[0] = False();
  one[0xFFFF] = True();
  for (int v = 0; v < 4; v++) {
    Prop p = Prop{Var{.id = v}};
    uint16_t tt = Eval(p);
    one[tt] = p;
  }

  for (const auto &[k, _] : one) done.Add(k);

  best.push_back(std::move(one));

  // Iteratively deepen.
  while (done.Size() < 0x10000) {
    int next_size = best.size();
    BestMap next;

    // All props large than size 1 have one of the connectives
    // on their outside, so wlog we can just try applying those
    // to smaller terms.

    if (opts & OPT_NOT) {
      for (const auto &[tt, prop] : best[next_size - 1]) {
        uint16_t ntt = ~tt;
        if (!done.Contains(ntt)) {
          next[ntt] = -prop;
          done.Add(ntt);
        }
      }
    }

    for (int x = 0; x < best.size(); x++) {
      const int y = next_size - 1 - x;
      // All of these connectives are symmetric, so we have x <= y.
      if (y < 0 || y >= next_size || x > y) continue;

      for (const auto &[tt1, p1] : best[x]) {
        for (const auto &[tt2, p2] : best[y]) {
          if (opts & OPT_AND) {
            uint16_t tt = tt1 & tt2;

            if (!done.Contains(tt)) {
              next[tt] = p1 & p2;
              done.Add(tt);
            }
          }

          if (opts & OPT_XOR) {
            uint16_t tt = tt1 ^ tt2;

            if (!done.Contains(tt)) {
              next[tt] = p1 ^ p2;
              done.Add(tt);
            }
          }

          if (opts & OPT_OR) {
            uint16_t tt = tt1 | tt2;

            if (!done.Contains(tt)) {
              next[tt] = p1 | p2;
              done.Add(tt);
            }
          }

        }
      }
    }

    best.push_back(std::move(next));
  }

  minimal.resize(65536);
  for (const BestMap &bm : best) {
    for (const auto &[tt, p] : bm) {
      minimal[tt] = p;
    }
  }
}

static Prop Substitute(const Prop &p, int a, int b, int c, int d) {
  if (std::holds_alternative<Value>(p.p)) {
    return p;

  } else if (const auto *var = std::get_if<Var>(&p.p)) {
    switch (var->id) {
    case 0: return {Var{.id = a}};
    case 1: return {Var{.id = b}};
    case 2: return {Var{.id = c}};
    case 3: return {Var{.id = d}};
    default:
      LOG(FATAL) << "Free variable out of range!";
    }

  } else if (const auto *un = std::get_if<Unop>(&p.p)) {
    return Prop{
      .p = Unop{
        .op = un->op,
        .a = std::make_shared<Prop>(Substitute(*un->a, a, b, c, d)),
      }};

  } else if (const auto *bin = std::get_if<Binop>(&p.p)) {
    return Prop{
      .p = Binop{
        .op = bin->op,
        .a = std::make_shared<Prop>(Substitute(*bin->a, a, b, c, d)),
        .b = std::make_shared<Prop>(Substitute(*bin->b, a, b, c, d)),
      }};
  } else {

    LOG(FATAL) << "Bad variant!";
  }
}

Prop MiniTable::Minimal(int a, int b, int c, int d, uint16_t fn) const {
  return Substitute(minimal[fn], a, b, c, d);
}

static bool GetQuadVars(const Prop &p, InlineVector<int> *vars) {
  if (std::holds_alternative<Value>(p.p)) {
    return true;

  } else if (const auto *var = std::get_if<Var>(&p.p)) {
    for (int id : *vars) {
      if (id == var->id) return true;
    }
    if (vars->size() == 4) return false;
    vars->push_back(var->id);
    return true;

  } else if (const auto *un = std::get_if<Unop>(&p.p)) {
    return GetQuadVars(*un->a, vars);

  } else if (const auto *bin = std::get_if<Binop>(&p.p)) {
    if (!GetQuadVars(*bin->a, vars)) return false;
    return GetQuadVars(*bin->b, vars);
  }

  LOG(FATAL) << "Bad variant?";
  return false;
}

std::optional<std::tuple<int, int, int, int, uint16_t>>
MiniTable::GetQuad(const Prop &p) {
  InlineVector<int> vars;
  if (!GetQuadVars(p, &vars)) {
    return std::nullopt;
  }
  std::sort(vars.begin(), vars.end());

  int v0 = vars.size() > 0 ? vars[0] : 0;
  int v1 = vars.size() > 1 ? vars[1] : v0;
  int v2 = vars.size() > 2 ? vars[2] : v0;
  int v3 = vars.size() > 3 ? vars[3] : v0;

  int max_var = 0;
  if (!vars.empty()) {
    max_var = vars.back();
  }

  uint16_t tt = 0;
  for (uint8_t input = 0; input < 16; input++) {
    std::vector<bool> assignment(max_var + 1, false);
    if (vars.size() > 0) assignment[v0] = (input & 1) != 0;
    if (vars.size() > 1) assignment[v1] = (input & 2) != 0;
    if (vars.size() > 2) assignment[v2] = (input & 4) != 0;
    if (vars.size() > 3) assignment[v3] = (input & 8) != 0;

    bool value = EvaluateProp(assignment, p);
    tt = (tt << 1) | (value ? 0b1 : 0b0);
  }

  return std::make_tuple(v0, v1, v2, v3, tt);
}

