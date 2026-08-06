
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

      auto Not = [](const Prop &p) {
          if (const Unop *un = std::get_if<Unop>(&p.p);
              un && un->op == UnopOp::NOT) {
            return *un->a;
          }
          return -p;
        };

      Prop np;
      const Unop *ua = std::get_if<Unop>(&a.p);
      const Binop *bop = std::get_if<Binop>(&a.p);
      const Ternop *top = std::get_if<Ternop>(&a.p);

      if (ua && ua->op == UnopOp::NOT) {
        np = *ua->a;
      } else if (bop && bop->op == BinopOp::AND) {
        np = Not(*bop->a) | Not(*bop->b);
      } else if (bop && bop->op == BinopOp::OR) {
        np = Not(*bop->a) & Not(*bop->b);
      } else if (bop && bop->op == BinopOp::NAND) {
        np = *bop->a & *bop->b;
      } else if (bop && bop->op == BinopOp::NOR) {
        np = *bop->a | *bop->b;
      } else if (top && top->op == TernopOp::ITE) {
        np = Ite(*top->a, Not(*top->b), Not(*top->c));
      } else {
        np = -a;
      }

      if (vs.Infinite()) {
        seen.insert(np);
        return std::make_pair(np, vs);

      } else {

        // Otherwise, use the minitable.
        auto oo = MiniTable::GetQuad(np);
        CHECK(oo.has_value()) << "This should always succeed when there "
          "are four or fewer variables..?";

        const auto &[qa, qb, qc, qd, tt] = oo.value();

        Prop m = table->Minimal(qa, qb, qc, qd, tt);
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

        const auto &[qa, qb, qc, qd, tt] = oo.value();

        Prop m = table->Minimal(qa, qb, qc, qd, tt);
        return std::make_pair(m, vs);
      }

    } else if (const Ternop *top = std::get_if<Ternop>(&prop.p)) {
      const auto &[a, av] = SimplifyRec(*top->a);
      const auto &[b, bv] = SimplifyRec(*top->b);
      const auto &[c, cv] = SimplifyRec(*top->c);

      LimitedSet vs = av;
      for (int v : bv.vec) vs.Add(v);
      for (int v : cv.vec) vs.Add(v);

      Prop p = {Ternop{
          .op = top->op,
          .a = std::make_shared<Prop>(a),
          .b = std::make_shared<Prop>(b),
          .c = std::make_shared<Prop>(c),
        }};

      if (vs.Infinite()) {
        seen.insert(p);
        return std::make_pair(p, vs);

      } else {

        // Otherwise, use the minitable.
        auto oo = MiniTable::GetQuad(p);
        CHECK(oo.has_value()) << "This should always succeed when there "
          "are four or fewer variables..?";

        const auto &[qa, qb, qc, qd, tt] = oo.value();

        Prop m = table->Minimal(qa, qb, qc, qd, tt);
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

Simplification::Simplification(uint32_t opts) :
  table(new MiniTable(opts)) {
}

