
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
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
#include "z3/z3.h"

static std::shared_ptr<Prop> MakeAnd(std::shared_ptr<Prop> a, std::shared_ptr<Prop> b) {
  return std::make_shared<Prop>(
      Prop{.p = Binop{.op = BinopOp::AND, .a = std::move(a), .b = std::move(b)}});
}

static std::shared_ptr<Prop> MakeOr(std::shared_ptr<Prop> a, std::shared_ptr<Prop> b) {
  return std::make_shared<Prop>(
      Prop{.p = Binop{.op = BinopOp::OR, .a = std::move(a), .b = std::move(b)}});
}

static std::shared_ptr<Prop> MakeXor(std::shared_ptr<Prop> a, std::shared_ptr<Prop> b) {
  return std::make_shared<Prop>(
      Prop{.p = Binop{.op = BinopOp::XOR, .a = std::move(a), .b = std::move(b)}});
}

static std::shared_ptr<Prop> MakeNot(std::shared_ptr<Prop> a) {
  return std::make_shared<Prop>(
      Prop{.p = Unop{.op = UnopOp::NOT, .a = std::move(a)}});
}

static bool HasOut(const Z3::SExp &e) {
  if (e.type == Z3::SExp::Type::ATOM) {
    return e.atom == "Out";
  }
  for (const Z3::SExp &child : e.list) {
    if (HasOut(child)) return true;
  }
  return false;
}

static std::shared_ptr<Prop> BuildPropRec(
    const Z3::SExp &e,
    const FunctionalMap<std::string, std::shared_ptr<Prop>> &env) {
  if (e.type == Z3::SExp::Type::LIST) {
    if (e.list.empty()) {
      CHECK(false) << "Unsupported expression: " << Z3::ToString(e);
      return std::make_shared<Prop>(False());
    }
    std::string_view op = e.list[0].atom;
    if (op == "goals") {
      if (e.list.size() > 1) return BuildPropRec(e.list[1], env);

    } else if (op == "goal") {
      for (size_t i = 1; i < e.list.size(); i++) {
        if (HasOut(e.list[i])) {
          return BuildPropRec(e.list[i], env);
        }
      }

      LOG(FATAL) << "No arg to goal has (= Out)?";


    } else if (op == "let") {
      FunctionalMap<std::string, std::shared_ptr<Prop>> new_env = env;
      if (e.list.size() == 3 && e.list[1].type == Z3::SExp::Type::LIST) {
        for (const Z3::SExp &binding : e.list[1].list) {
          if (binding.type == Z3::SExp::Type::LIST && binding.list.size() == 2) {
            new_env = new_env.Insert(binding.list[0].atom,
                                     BuildPropRec(binding.list[1], env));
          }
        }
        return BuildPropRec(e.list[2], new_env);
      }

    } else if (op == "and") {
      if (e.list.size() == 1) return std::make_shared<Prop>(True());
      std::shared_ptr<Prop> p = BuildPropRec(e.list[1], env);
      for (size_t i = 2; i < e.list.size(); i++) {
        p = MakeAnd(std::move(p), BuildPropRec(e.list[i], env));
      }
      return p;

    } else if (op == "or") {
      if (e.list.size() == 1) return std::make_shared<Prop>(False());
      std::shared_ptr<Prop> p = BuildPropRec(e.list[1], env);
      for (size_t i = 2; i < e.list.size(); i++) {
        p = MakeOr(std::move(p), BuildPropRec(e.list[i], env));
      }
      return p;

    } else if (op == "xor") {
      if (e.list.size() > 1) {
        std::shared_ptr<Prop> p = BuildPropRec(e.list[1], env);
        for (size_t i = 2; i < e.list.size(); i++) {
          p = MakeXor(std::move(p), BuildPropRec(e.list[i], env));
        }
        return p;
      }

    } else if (op == "not") {
      if (e.list.size() == 2) {
        return MakeNot(BuildPropRec(e.list[1], env));
      }

      LOG(FATAL) << "Expected 1 arg to not. Got: " << Z3::ToString(e);

    } else if (op == "ite") {
      // Note that this duplicates the condition!
      if (e.list.size() == 4) {
        std::shared_ptr<Prop> cond = BuildPropRec(e.list[1], env);
        std::shared_ptr<Prop> t = BuildPropRec(e.list[2], env);
        std::shared_ptr<Prop> f = BuildPropRec(e.list[3], env);
        return MakeOr(MakeAnd(cond, std::move(t)),
                      MakeAnd(MakeNot(cond), std::move(f)));
      }

      LOG(FATAL) << "Expected 3 args to ite. Got: " << Z3::ToString(e);

    } else if (op == "=") {
      if (e.list.size() == 3) {
        if (e.list[1].type == Z3::SExp::Type::ATOM && e.list[1].atom == "Out") {
          return BuildPropRec(e.list[2], env);
        }
        if (e.list[2].type == Z3::SExp::Type::ATOM && e.list[2].atom == "Out") {
          return BuildPropRec(e.list[1], env);
        }
        std::shared_ptr<Prop> a = BuildPropRec(e.list[1], env);
        std::shared_ptr<Prop> b = BuildPropRec(e.list[2], env);
        return MakeNot(MakeXor(std::move(a), std::move(b)));
      }

      LOG(FATAL) << "Expected 2 args to =. Got: " << Z3::ToString(e);

    } else if (op == "=>") {
      if (e.list.size() == 3) {
        std::shared_ptr<Prop> a = BuildPropRec(e.list[1], env);
        std::shared_ptr<Prop> b = BuildPropRec(e.list[2], env);
        return MakeOr(MakeNot(std::move(a)), std::move(b));
      }

      LOG(FATAL) << "Expected 2 args to =>. Got: " << Z3::ToString(e);
    }

  } else if (e.type == Z3::SExp::Type::ATOM) {
    if (e.atom == "true") return std::make_shared<Prop>(True());
    if (e.atom == "false") return std::make_shared<Prop>(False());
    if (e.atom == "Out") return std::make_shared<Prop>(True());
    if (const std::shared_ptr<Prop> *p = env.FindPtr(e.atom)) {
      return *p;
    }
    if (e.atom.size() > 1 && e.atom[0] == 'v') {
      return std::make_shared<Prop>(Prop{.p = Var{.id = std::stoi(e.atom.substr(1))}});
    }
  }

  LOG(FATAL) << "Unsupported expression: " << Z3::ToString(e);
  return std::make_shared<Prop>(False());
}

static Prop ExtractProp(const Z3::SExp &e) {
  return *BuildPropRec(e, {});
}

static void Generate() {
  CellLibrary library;
  Simplification sim;

  World world;
  ChessProp::Board board = ChessProp::NewBoard(&world);

  const int srcr = 6;
  const int srcc = 1;
  const int dstr = 4;
  const int dstc = 1;

  Prop prop = ChessProp::IsLegal(board, srcr, srcc, dstr, dstc,
                                 ChessProp::REAL_CHESS);

  size_t start_size = PropSize(prop);
  size_t start_shared_size = PropSize(prop);
  Print("Starting size: {} ({} shared)\n",
        start_size, start_shared_size);

  prop = BalanceProp(SimplifyProp(prop));
  prop = SimplifyProp(prop);
  prop = sim.Simplify(prop);
  size_t orig_size = PropSize(prop);
  size_t orig_shared_size = SharedPropSize(prop);

  std::string contents;

  contents.reserve(10 * 1024 * 1024);
  contents += "(set-option :pp.min_alias_size 1)\n\n";

  int max_var = ChessProp::NUM_BOARD_PROPS;
  for (int v : PropVars(prop)) {
    if (v >= max_var) max_var = v + 1;
  }

  for (int i = 0; i < max_var; i++) {
    contents += "(declare-const v" + std::to_string(i) + " Bool)\n";
  }
  contents += "\n(declare-const Out Bool)\n\n";

  if (true) {
    contents += "\n;; Board constraints\n";
    // Board rules: exactly one piece type per square
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        contents += "(assert (or";
        for (int t = 0; t < ChessProp::NUM_TYPES; t++) {
          contents += " v" + std::to_string(ChessProp::HasContentsIdx(r, c, t));
        }
        contents += "))\n";

        for (int t1 = 0; t1 < ChessProp::NUM_TYPES; t1++) {
          for (int t2 = t1 + 1; t2 < ChessProp::NUM_TYPES; t2++) {
            contents += "(assert (not (and v" +
                        std::to_string(ChessProp::HasContentsIdx(r, c, t1)) +
                        " v" +
                        std::to_string(ChessProp::HasContentsIdx(r, c, t2)) +
                        ")))\n";
          }
        }
      }
    }

    // En passant rule: at most one column is the en passant target
    for (int c1 = 0; c1 < 8; c1++) {
      for (int c2 = c1 + 1; c2 < 8; c2++) {
        contents += std::format("(assert (not (and v{} v{})))\n",
                                ChessProp::EnPassantColIdx(c1),
                                ChessProp::EnPassantColIdx(c2));
      }
    }
  }

  contents += "\n;; Prop\n";

  // Convert DAG to sequentially defined macros to avoid exponential expansion
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
      }
      order.push_back(p);
    };
  Visit(Visit, &prop);

  for (size_t i = 0; i < order.size(); i++) {
    node_ids[order[i]] = i;
  }

  auto GetRef = [&](const Prop* p) -> std::string {
      if (const Value *v = std::get_if<Value>(&p->p)) {
        return v->value ? "true" : "false";
      } else if (const Var *v = std::get_if<Var>(&p->p)) {
        return std::format("v{}", v->id);
      } else {
        return std::format("n{}", node_ids.at(p));
      }
    };

  for (const Prop* p : order) {
    std::string expr;
    if (const Unop *u = std::get_if<Unop>(&p->p)) {
      expr = std::format("(not {})", GetRef(u->a.get()));
    } else if (const Binop *b = std::get_if<Binop>(&p->p)) {
      std::string op = "and";
      if (b->op == BinopOp::OR) op = "or";
      else if (b->op == BinopOp::XOR) op = "xor";
      expr = std::format("({} {} {})",
                         op, GetRef(b->a.get()), GetRef(b->b.get()));
    }
    contents +=
      std::format("(define-fun n{} () Bool {})\n",
                  node_ids[p], expr);
  }

  contents += "(assert (= Out " + GetRef(&prop) + "))\n\n";

  contents += "\n\n; Apply tactics\n";
  // contents += "(apply (then ctx-solver-simplify aig))\n";
  contents += "(apply (then simplify propagate-values "
    "ctx-solver-simplify aig simplify))";
  // This optimizes away "Out"!
  // contents += "(apply (then simplify propagate-values solve-eqs "
  // "elim-uncnstr ctx-solver-simplify aig))";

  std::vector<Z3::SExp> res = Z3::Run(contents);

  CHECK(res.size()) << "Expected exactly one 'goals' sexp";

  Print("\nGot: {}\n\n", Z3::ToString(res[0]));

  Prop opt = ExtractProp(res[0]);

  Print(AYELLOW("Optimized internally") ":\n"
        "{}\n", PropString(prop));

  Print("\n"
        AYELLOW("Optimized with Z3") ":\n");
  Print("{}\n", PropString(opt));

  size_t z3_size = PropSize(opt);
  size_t z3_shared_size = SharedPropSize(opt);

  Print("Size before: {} ({} shared)\n"
        "Size after:  {} ({} shared)\n",
        orig_size, orig_shared_size,
        z3_size, z3_shared_size);
}

int main(int argc, char **argv) {
  ANSI::Init();
  Generate();
  Print("OK\n");
  return 0;
}

