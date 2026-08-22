
#include "prop-util.h"

#include <map>
#include <string>
#include <vector>
#include <unordered_set>

#include "prop.h"
#include "base/stringprintf.h"
#include "set-util.h"

std::string PropUtil::CompareZ3(
    const World &world, const Prop &prop_before, const Prop &prop_after,
    const Prop &prop_valid) {
  std::string contents;

  // Collect the distinct vars that actually occur.
  std::unordered_set<int> all_vars_set;
  for (int v : PropVars(prop_before)) all_vars_set.insert(v);
  for (int v : PropVars(prop_after)) all_vars_set.insert(v);
  for (int v : PropVars(prop_valid)) all_vars_set.insert(v);
  std::vector<int> all_vars = SetToSortedVec(all_vars_set);

  auto VarName = [&](int id) {
      if (id >= 0 && id < world.symbol_names.size() &&
          !world.symbol_names[id].empty()) {
        std::string s = world.symbol_names[id];
        for (char &c : s) {
          if (!((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9'))) {
            c = '_';
          }
        }
        if (s[0] >= '0' && s[0] <= '9') {
          s = "v_" + s;
        }
        return s;
      }
      return std::format("v{}", id);
    };

  for (int v : all_vars) {
    AppendFormat(&contents, "(declare-const {} Bool)\n", VarName(v));
  }
  contents += "(declare-const prop_before Bool)\n";
  contents += "(declare-const prop_after Bool)\n";
  contents += "(declare-const prop_valid Bool)\n\n";

  std::map<const Prop *, int> node_ids;
  std::vector<const Prop *> order;

  auto Visit = [&](auto &self, const Prop *p) {
      if (p == nullptr) return;
      if (std::holds_alternative<Value>(p->p) ||
          std::holds_alternative<Var>(p->p)) {
        return;
      }
      if (node_ids.count(p))
        return;
      node_ids[p] = -1; // mark visited

      if (const Unop *u = std::get_if<Unop>(&p->p)) {
        self(self, u->a.get());
      } else if (const Binop *b = std::get_if<Binop>(&p->p)) {
        self(self, b->a.get());
        self(self, b->b.get());
      } else if (const Ternop *t = std::get_if<Ternop>(&p->p)) {
        self(self, t->a.get());
        self(self, t->b.get());
        self(self, t->c.get());
      }
      order.push_back(p);
    };

  Visit(Visit, &prop_before);
  Visit(Visit, &prop_after);
  Visit(Visit, &prop_valid);

  for (size_t i = 0; i < order.size(); i++) {
    node_ids[order[i]] = i;
  }

  auto GetRef = [&](const Prop* p) -> std::string {
      if (const Value *v = std::get_if<Value>(&p->p)) {
        return v->value ? "true" : "false";
      } else if (const Var *v = std::get_if<Var>(&p->p)) {
        return VarName(v->id);
      } else {
        return std::format("n{}", node_ids.at(p));
      }
    };

  for (const Prop* p : order) {
    std::string expr;
    if (const Unop *u = std::get_if<Unop>(&p->p)) {
      expr = std::format("(not {})", GetRef(u->a.get()));
    } else if (const Binop *b = std::get_if<Binop>(&p->p)) {
      if (b->op == BinopOp::NAND) {
        expr = std::format("(not (and {} {}))", GetRef(b->a.get()),
                           GetRef(b->b.get()));
      } else if (b->op == BinopOp::NOR) {
        expr = std::format("(not (or {} {}))", GetRef(b->a.get()),
                           GetRef(b->b.get()));
      } else {
        std::string op = "and";
        if (b->op == BinopOp::OR) op = "or";
        else if (b->op == BinopOp::XOR) op = "xor";
        expr = std::format("({} {} {})",
                           op, GetRef(b->a.get()), GetRef(b->b.get()));
      }
    } else if (const Ternop *t = std::get_if<Ternop>(&p->p)) {
      if (t->op == TernopOp::ITE) {
        expr = std::format("(ite {} {} {})", GetRef(t->a.get()),
                           GetRef(t->b.get()), GetRef(t->c.get()));
      }
    }
    AppendFormat(&contents, "(define-fun n{} () Bool {})\n",
                 node_ids[p], expr);
  }

  contents += "(assert (= prop_before " + GetRef(&prop_before) + "))\n";
  contents += "(assert (= prop_after " + GetRef(&prop_after) + "))\n";
  contents += "(assert (= prop_valid " + GetRef(&prop_valid) + "))\n\n";

  contents += ";; Find a case where they differ:\n";
  contents += "(assert (not (= prop_before prop_after)))\n";
  contents += "(assert prop_valid)\n";
  contents += "(check-sat)\n";
  contents += "(get-value (\n";
  for (int v : all_vars) {
    AppendFormat(&contents, "  {}\n", VarName(v));
  }
  contents += "))\n";

  return contents;
}
