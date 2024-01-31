
#include "pattern-compilation.h"

#include <vector>

#include "il.h"
#include "elaboration.h"

namespace il {

using Pat = el::Pat;
using PatType = el::PatType;


// The pattern matrix.
struct PatternCompilation::Matrix {
  int Width() const {
    return (int)objs.size();
  }

  int Height() const {
    return (int)rows.size();
  }

  // The pattern objects. These are always EL variables.
  std::vector<std::string> objs;
  // Parallel array of the types.
  std::vector<const il::Type *> types;

  // This is a 2D array of width * height patterns.
  std::vector<const el::Pat *> pats;
  const el::Pat *Cell(int x, int y) const {
    CHECK(x >= 0 && x < Width() &&
          y >= 0 && y < Height());

    return pats[y * Width() + x];
  }

  const el::Pat *&Cell(int x, int y) {
    CHECK(x >= 0 && x < Width() &&
          y >= 0 && y < Height());

    return pats[y * Width() + x];
  }


  // The expression for each case.
  std::vector<const el::Exp *> rows;

  // The default case, if nothing matches.
  const el::Exp *def = nullptr;

  // Controls whether we print error messages for redundant
  // matches. For compiler-generated patterns, this is harmless
  // and not an error.
  bool is_user_pattern = true;
};

PatternCompilation::PatternCompilation(Elaboration *elab) : elab(elab) {

}

std::pair<const Exp *, const Type *> PatternCompilation::Compile(
    const Context &G,
    const std::string &obj,
    const Type *obj_type,
    const std::vector<std::pair<const el::Pat *, const el::Exp *>> &rows_in) {

  CHECK(!rows_in.empty()) << "There must be rows.";

  // Since pattern compilation generates new trivial patterns of the form
  //  let val x = y
  //  in e
  // ... (where the pattern is the varible "x"), we need to handle that
  // as a base case or else we'll never terminate.
  if (rows_in[0].first->type == PatType::VAR) {
    if (rows_in.size() > 1) {
      // XXX only for user patterns
      printf("Warning: redundant match?\n");
    }
    el::AstPool *el_pool = elab->el_pool;
    il::AstPool *pool = elab->pool;
    const auto &[ve, vt] = elab->Elab(G, el_pool->Var(obj));
    std::string var = rows_in[0].first->var;
    std::string il_var = var;
    const il::Dec *dec = pool->ValDec(var, ve);

    Context GG = G.Insert(var,
                          VarInfo{
                            .tyvars = {},
                              .type = vt,
                              .var = var,
                              .primop = std::nullopt});

    const auto &[body_exp, body_type] = elab->Elab(GG, rows_in[0].second);
    return std::make_pair(pool->Let({dec}, body_exp), body_type);
  }

  // Prepare the pattern matrix. We work with a rectangular matrix,
  // but we always start with a single column.
  //    case (a, b, c) of
  //       (x, y, 0)    => e1
  //     | (w as z : t) => e2
  Matrix matrix;
  matrix.objs = {obj};
  matrix.types = {obj_type};

  matrix.pats.reserve(rows_in.size());
  matrix.rows.reserve(rows_in.size());
  for (const auto &[pat, exp] : rows_in) {
    matrix.pats.push_back(pat);
    matrix.rows.push_back(exp);
  }

  CheckAffine(matrix);

  return Comp(G, matrix);
}

void PatternCompilation::CheckAffine(const Matrix &m) const {
  // TODO: Check that no row binds a variable more than once.
}

// let nv = objv in body
const el::Exp *PatternCompilation::SimpleBind(std::string nv, std::string objv,
                                              const el::Exp *body) {
  el::AstPool *el_pool = elab->el_pool;
  return el_pool->Let(
      {el_pool->ValDec(el_pool->VarPat(nv), el_pool->Var(objv))},
      body);
}

std::pair<const Exp *, const Type *> PatternCompilation::Comp(
    const Context &G,
    Matrix matrix) {

  // This follows the same approach as humlock, itself loosely
  // based on TILT.
  //
  // We repeatedly apply greedy simplifications to the pattern
  // matrix to make it "clean," then perform some kind of
  // interesting split.

  // First, we remove "as" patterns and "var" patterns.
  for (int y = 0; y < matrix.Height(); y++) {
    for (int x = 0; x < matrix.Width(); x++) {
      while (matrix.Cell(x, y)->type == PatType::AS) {
        const Pat *op = matrix.Cell(x, y);
        const std::string nv = op->var;
        matrix.rows[y] = SimpleBind(nv, matrix.objs[x], matrix.rows[y]);
        matrix.Cell(x, y) = op->a;
      }

      // z is just "_ as z".
      while (matrix.Cell(x, y)->type == PatType::VAR) {
        const Pat *op = matrix.Cell(x, y);
        const std::string nv = op->var;
        matrix.rows[y] = SimpleBind(nv, matrix.objs[x], matrix.rows[y]);
        matrix.Cell(x, y) = elab->el_pool->WildPat();
      }
    }
  }

  // Next,
  LOG(FATAL) << "Unimplemented";

  return std::make_pair(nullptr, nullptr);
}

std::pair<const Exp *, const Type *>
PatternCompilation::CompileIrrefutable(
    const Context &G,
    const el::Pat *pat,
    const el::Exp *rhs,
    const el::Exp *body) {
  el::AstPool *el_pool = elab->el_pool;
  il::AstPool *pool = elab->pool;
  const auto &[re, rt] = elab->Elab(G, rhs);

  // Dispense with type constraints on the pattern.
  std::vector<std::string> vars;
  ([&]() {
      for (;;) {
        switch (pat->type) {
        case el::PatType::ANN: {
          const il::Type *at = elab->ElabType(G, pat->ann);
          Unification::Unify("pattern type constraint", at, rt);
          pat = pat->a;
          break;
        }
        case el::PatType::AS:
          vars.push_back(pat->var);
          pat = pat->a;
          break;
        case el::PatType::VAR:
          vars.push_back(pat->var);
          pat = el_pool->WildPat();
          break;
        case el::PatType::WILD:
          return;
        case el::PatType::TUPLE:
          return;
        }
      }
    }());

  CHECK(pat->type == el::PatType::WILD ||
        pat->type == el::PatType::TUPLE);

  // Get the free evars in rt.
  std::vector<EVar> free_evars = EVar::FreeEVarsInType(rt);

  // Check each with G.HasEVar. If it's not in the context,
  // then we generalize it.
  std::vector<EVar> gen_evars;
  for (const EVar &v : free_evars) {
    if (!G.HasEVar(v)) {
      gen_evars.push_back(v);
    }
  }

  // Create a new type variable for abstraction and set the evar to
  // it.
  std::vector<std::string> gen_tyvars;
  for (int i = 0; i < (int)gen_evars.size(); i++) {
    std::string a = pool->NewVar("gen");
    gen_tyvars.push_back(a);
    const Type *av = pool->VarType(a);
    gen_evars[i].Set(av);
  }

  // Collect those variables for the
  // PolyType. (Note: There's nothing to do unless there's
  // a variable here, even though it would be sound to
  // generalize in like  val _ = fn x => x. But maybe we
  // want to do this only when we're binding a variable.
  //
  // val (m, n) = (fn a => a, nil)
  //
  // val tuple : = (fn a => a, nil)


  // This is also where we handle polymorphic generalization.
  // If the RHS is a value, then
  LOG(FATAL) << "Unimplemented";
  return std::make_pair(nullptr, nullptr);
}


}  // namespace il
