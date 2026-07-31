
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "cell-library.h"
#include "chessprop.h"
#include "functional-map.h"
#include "prop.h"
#include "simplification.h"
#include "util.h"

static Prop ExactOne(const std::vector<Prop> &props) {
  Prop any = False();
  Prop any_two = False();
  for (size_t i = 0; i < props.size(); i++) {
    for (size_t j = i + 1; j < props.size(); j++) {
      any_two = any_two | (props[i] & props[j]);
    }
    any = any | props[i];
  }
  return any & -any_two;
}

static std::string ToBLIF(const World &world, const Prop &prop, const Prop &exdc) {
  std::string res = ".model chess\n";
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

static void Generate() {
  Print("Init...\n");
  CellLibrary library;
  Simplification sim;

  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);

  const int srcr = 6;
  const int srcc = 1;
  const int dstr = 4;
  const int dstc = 1;

  Print("Get chess prop...\n");
  fflush(stdout);
  Prop prop = ChessProp::IsLegal(board, srcr, srcc, dstr, dstc,
                                 ChessProp::KID_CHESS);

  size_t start_size = PropSize(prop);
  size_t start_shared_size = PropSize(prop);
  Print("Starting size: {} ({} shared)\n",
        start_size, start_shared_size);

  prop = BalanceProp(SimplifyProp(prop));
  prop = SimplifyProp(prop);
  Print("Simplify...\n");
  prop = sim.Simplify(prop);
  [[maybe_unused]]
  size_t orig_size = PropSize(prop);
  [[maybe_unused]]
  size_t orig_shared_size = SharedPropSize(prop);

  Print("Create exdc (don't care) condition...\n");
  Prop valid = True();
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      std::vector<Prop> square_props;
      for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
        square_props.push_back(board.props[ChessProp::HasContentsIdx(r, c, t)]);
      }
      valid = valid & ExactOne(square_props);
    }
  }
  Prop exdc = -valid;
  exdc = BalanceProp(SimplifyProp(exdc));
  exdc = SimplifyProp(exdc);

  Print("Write blif...\n");
  std::string contents = ToBLIF(world, prop, exdc);
  Util::WriteFile("chess.blif", contents);
}

int main(int argc, char **argv) {
  ANSI::Init();
  Generate();
  Print("OK\n");
  return 0;
}

