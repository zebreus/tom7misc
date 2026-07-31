
#include "simplification.h"

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <utility>
#include <variant>

#include "inline-vector.h"
#include "minitable.h"
#include "prop.h"

namespace {
// Holds at most 5 vars, at which point it is "infinite".
struct LimitedSet {
  InlineVector<int> vec;

  LimitedSet() { }

  LimitedSet(int i) {
    vec.push_back(i);
  }

  bool Empty() const {
    return vec.empty();
  }

  bool Infinite() const {
    return vec.size() > 4;
  }

  bool Contains(int v) const {
    for (int vv : vec)
      if (v == vv)
        return true;
    return false;
  }

  void Add(int v) {
    if (Infinite()) return;
    if (Contains(v)) return;

    vec.push_back(v);
    std::sort(vec.begin(), vec.end());
  }
};

struct Simplifier {
  Simplifier(const MiniTable *table) : table(table) {}

  std::pair<Prop, LimitedSet> SimplifyRec(const Prop &prop) {

    if (std::holds_alternative<Value>(prop.p)) {
      seen.insert(prop);
      return std::make_pair(prop, LimitedSet());

    } else if (const Var *v = std::get_if<Var>(&prop.p)) {
      seen.insert(prop);
      return std::make_pair(prop, LimitedSet(v->id));

    } else if (const Unop *u = std::get_if<Unop>(&prop.p)) {
      const auto &[a, vs] = SimplifyRec(*u->a);
      Prop np = -a;

      if (vs.Infinite()) {
        // TODO: Consider rewriting, especially if not(not(...))
        seen.insert(np);
        return std::make_pair(np, vs);

      } else {

        // Otherwise, use the minitable.
        auto oo = MiniTable::GetQuad(np);
        CHECK(oo.has_value()) << "This should always succeed when there "
          "are four or fewer variables..?";

        const auto &[a, b, c, d, tt] = oo.value();

        Prop m = table->Minimal(a, b, c, d, tt);
        return std::make_pair(m, vs);
      }

    } else if (const Binop *bop = std::get_if<Binop>(&prop.p)) {
      const auto &[a, av] = SimplifyRec(*bop->a);
      const auto &[b, bv] = SimplifyRec(*bop->b);

      LimitedSet vs = av;
      for (int v : bv.vec) vs.Add(v);

      // Normalize the order to promote sharing.
      // Slightly better would be to try both orders, since we
      // might end up sharing with a prop from the MiniTable.
      std::shared_ptr<Prop> aa = std::make_shared<Prop>(a);
      std::shared_ptr<Prop> bb = std::make_shared<Prop>(b);
      if (*aa > *bb) std::swap(aa, bb);

      Prop p = {Binop{
          .op = bop->op,
          .a = std::move(aa),
          .b = std::move(bb),
        }};

      if (vs.Infinite()) {
        seen.insert(p);
        return std::make_pair(p, vs);

      } else {

        // Otherwise, use the minitable.
        auto oo = MiniTable::GetQuad(p);
        CHECK(oo.has_value()) << "This should always succeed when there "
          "are four or fewer variables..?";

        const auto &[a, b, c, d, tt] = oo.value();

        Prop m = table->Minimal(a, b, c, d, tt);
        return std::make_pair(m, vs);
      }

    } else {
      LOG(FATAL) << "Bad variant?";
    }
  }

  std::unordered_set<Prop> seen;
  const MiniTable *table = nullptr;
};

}  // namespace

Prop Simplification::Simplify(const Prop &in) const {
  Simplifier sim(table.get());

  return std::get<0>(sim.SimplifyRec(in));
}

Simplification::Simplification() :
  table(new MiniTable(MiniTable::OPT_AND |
                      MiniTable::OPT_OR |
                      MiniTable::OPT_NOT)) {
}

