
#include "blif.h"

#include <format>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "prop.h"

std::string ToBLIF(std::string_view model_name,
                   const World &world, const Prop &prop,
                   const Prop &exdc) {
  std::string res = std::format(".model {}\n", model_name);
  std::set<int> var_set;
  for (int v : PropVars(prop)) var_set.insert(v);
  for (int v : PropVars(exdc)) var_set.insert(v);

  res += ".inputs";
  for (int v : var_set) {
    AppendFormat(&res, " {}", world.symbol_names[v]);
  }
  res += "\n";
  res += ".outputs out\n";

  int next_node = 0;
  auto GenLogic = [&](const Prop &p) {
      std::unordered_map<Prop, std::string> memo;
      std::string gates;

      std::vector<const Prop *> todo = {&p};
      std::vector<const Prop *> order;
      std::unordered_set<Prop> visited;

      while (!todo.empty()) {
        const Prop *curr = todo.back();
        if (visited.contains(*curr)) {
          todo.pop_back();
          continue;
        }

        bool children_done = true;
        if (const Unop *u = std::get_if<Unop>(&curr->p)) {
          if (!visited.contains(*u->a)) {
            children_done = false;
            todo.push_back(u->a.get());
          }

        } else if (const Binop *b = std::get_if<Binop>(&curr->p)) {
          if (!visited.contains(*b->a)) {
            children_done = false;
            todo.push_back(b->a.get());
          }
          if (!visited.contains(*b->b)) {
            children_done = false;
            todo.push_back(b->b.get());
          }

        } else if (const Ternop *t = std::get_if<Ternop>(&curr->p)) {
          if (!visited.contains(*t->a)) {
            children_done = false;
            todo.push_back(t->a.get());
          }
          if (!visited.contains(*t->b)) {
            children_done = false;
            todo.push_back(t->b.get());
          }
          if (!visited.contains(*t->c)) {
            children_done = false;
            todo.push_back(t->c.get());
          }
        }

        if (children_done) {
          visited.insert(*curr);
          order.push_back(curr);
          todo.pop_back();
        }
      }

      for (const Prop *curr : order) {
        if (const Value *v = std::get_if<Value>(&curr->p)) {
          std::string name = std::format("n{}", next_node++);
          AppendFormat(&gates, ".names {}\n", name);
          if (v->value) {
            gates += "1\n";
          }
          memo[*curr] = name;

        } else if (const Var *v = std::get_if<Var>(&curr->p)) {
          memo[*curr] = world.symbol_names[v->id];

        } else if (const Unop *u = std::get_if<Unop>(&curr->p)) {
          std::string name = std::format("n{}", next_node++);
          AppendFormat(&gates, ".names {} {}\n0 1\n", memo.at(*u->a), name);
          memo[*curr] = name;

        } else if (const Binop *b = std::get_if<Binop>(&curr->p)) {
          std::string name = std::format("n{}", next_node++);
          std::string name_a = memo.at(*b->a);
          std::string name_b = memo.at(*b->b);
          if (name_a == name_b) {
            // BLIF tools usually require distinct inputs to a gate.
            // When the inputs are identical, we simplify to a 1-input
            // gate or a constant.
            if (b->op == BinopOp::AND || b->op == BinopOp::OR) {
              AppendFormat(&gates, ".names {} {}\n1 1\n", name_a, name);
            } else if (b->op == BinopOp::NAND || b->op == BinopOp::NOR) {
              AppendFormat(&gates, ".names {} {}\n0 1\n", name_a, name);
            } else if (b->op == BinopOp::XOR) {
              // Same as constant 0.
              AppendFormat(&gates, ".names {}\n", name);
            } else {
              LOG(FATAL) << "Unimplemented binop(a, a)!";
            }

          } else {
            if (b->op == BinopOp::AND) {
              AppendFormat(&gates, ".names {} {} {}\n11 1\n",
                           name_a, name_b, name);
            } else if (b->op == BinopOp::OR) {
              AppendFormat(&gates, ".names {} {} {}\n1- 1\n-1 1\n",
                           name_a, name_b, name);
            } else if (b->op == BinopOp::XOR) {
              AppendFormat(&gates, ".names {} {} {}\n01 1\n10 1\n",
                           name_a, name_b, name);
            } else if (b->op == BinopOp::NAND) {
              AppendFormat(&gates, ".names {} {} {}\n0- 1\n-0 1\n",
                           name_a, name_b, name);
            } else if (b->op == BinopOp::NOR) {
              AppendFormat(&gates, ".names {} {} {}\n00 1\n",
                           name_a, name_b, name);
            } else {
              LOG(FATAL) << "Unhandled binop!";
            }
          }
          memo[*curr] = name;

        } else if (const Ternop *t = std::get_if<Ternop>(&curr->p)) {
          // Also need to handle duplicate children here.
          std::string name = std::format("n{}", next_node++);
          std::string name_a = memo.at(*t->a);
          std::string name_b = memo.at(*t->b);
          std::string name_c = memo.at(*t->c);

          if (t->op == TernopOp::ITE) {
            if (name_a == name_b && name_a == name_c) {
              AppendFormat(&gates, ".names {} {}\n1 1\n", name_a, name);
            } else if (name_a == name_b) {
              // ITE(a, a, c) = a OR c
              AppendFormat(&gates, ".names {} {} {}\n1- 1\n-1 1\n",
                           name_a, name_c, name);
            } else if (name_a == name_c) {
              // ITE(a, b, a) = a AND b
              AppendFormat(&gates, ".names {} {} {}\n11 1\n",
                           name_a, name_b, name);
            } else if (name_b == name_c) {
              // ITE(a, b, b) = b
              AppendFormat(&gates, ".names {} {}\n1 1\n", name_b, name);
            } else {
              AppendFormat(&gates, ".names {} {} {} {}\n11- 1\n0-1 1\n",
                           name_a, name_b, name_c, name);
            }

          } else {
            LOG(FATAL) << "Unhandled ternop!";
          }
          memo[*curr] = name;
        }
      }
      AppendFormat(&gates, ".names {} out\n1 1\n", memo.at(p));
      return gates;
    };

  res += GenLogic(prop);

  res += ".exdc\n";
  res += ".inputs";
  for (int v : var_set) {
    AppendFormat(&res, " {}", world.symbol_names[v]);
  }
  res += "\n";
  res += ".outputs out\n";
  res += GenLogic(exdc);

  res += ".end\n";
  return res;
}
