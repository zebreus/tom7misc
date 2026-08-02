
#include "blif.h"

#include <unordered_set>
#include <vector>
#include <unordered_map>
#include <format>
#include <string>
#include <set>
#include <string_view>
#include <variant>

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
    res += std::format(" {}", world.symbol_names[v]);
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
          gates += std::format(".names {}\n", name);
          if (v->value) {
            gates += "1\n";
          }
          memo[*curr] = name;
        } else if (const Var *v = std::get_if<Var>(&curr->p)) {
          memo[*curr] = world.symbol_names[v->id];
        } else if (const Unop *u = std::get_if<Unop>(&curr->p)) {
          std::string name = std::format("n{}", next_node++);
          gates += std::format(".names {} {}\n0 1\n", memo.at(*u->a), name);
          memo[*curr] = name;
        } else if (const Binop *b = std::get_if<Binop>(&curr->p)) {
          std::string name = std::format("n{}", next_node++);
          std::string name_a = memo.at(*b->a);
          std::string name_b = memo.at(*b->b);
          if (name_a == name_b) {
            if (b->op == BinopOp::AND || b->op == BinopOp::OR) {
              gates += std::format(".names {} {}\n1 1\n", name_a, name);
            } else {
              gates += std::format(".names {}\n", name);
            }
          } else {
            if (b->op == BinopOp::AND) {
              gates += std::format(".names {} {} {}\n11 1\n",
                                   name_a, name_b, name);
            } else if (b->op == BinopOp::OR) {
              gates += std::format(".names {} {} {}\n1- 1\n-1 1\n",
                                   name_a, name_b, name);
            } else if (b->op == BinopOp::XOR) {
              gates += std::format(".names {} {} {}\n01 1\n10 1\n",
                                   name_a, name_b, name);
            }
          }
          memo[*curr] = name;
        }
      }
      gates += std::format(".names {} out\n1 1\n", memo.at(p));
      return gates;
    };

  res += GenLogic(prop);

  res += ".exdc\n";
  res += ".inputs";
  for (int v : var_set) {
    res += std::format(" {}", world.symbol_names[v]);
  }
  res += "\n";
  res += ".outputs out\n";
  res += GenLogic(exdc);

  res += ".end\n";
  return res;
}
