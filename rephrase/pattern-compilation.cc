
#include "pattern-compilation.h"

#include <vector>

#include "il.h"
#include "elaboration.h"
#include "base/logging.h"
#include "base/stringprintf.h"

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
    // XXX I think we want to use CompileIrrefutable for this.
    if (rows_in.size() > 1) {
      // XXX only for user patterns
      printf("Warning: redundant match?\n");
    }
    el::AstPool *el_pool = elab->el_pool;
    il::AstPool *pool = elab->pool;
    const auto &[ve, vt] = elab->Elab(G, el_pool->Var(obj));
    std::string var = rows_in[0].first->var;
    std::string il_var = var;
    const il::Dec *dec = pool->ValDec({}, var, ve);

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
  const auto &[re, rt] = elab->Elab(G, rhs);

  // TODO: We'd probably get better error messages if we first do
  // a syntactic check for irrefutability.
  bool valuable = el::IsValuable(rhs);

  const auto &[GG, decs] =
    CompileIrrefutableRec(G, pat, re, rt, valuable);

  const auto &[be, bt] = elab->Elab(GG, body);
  return std::make_pair(elab->pool->Let(decs, be), bt);
}


std::pair<Context, std::vector<const Dec *>>
PatternCompilation::CompileIrrefutableRec(
    const Context &G,
    const el::Pat *pat,
    const il::Exp *rhs,
    const il::Type *rhs_type,
    bool rhs_valuable) {
  el::AstPool *el_pool = elab->el_pool;
  il::AstPool *pool = elab->pool;

  // Dispense with type constraints on the pattern.
  // All these vars are bound to the case object.
  std::vector<std::string> vars;
  ([&]() {
      for (;;) {
        switch (pat->type) {
        case el::PatType::ANN: {
          const il::Type *at = elab->ElabType(G, pat->ann);
          Unification::Unify("pattern type constraint", at, rhs_type);
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

  if (pat->type == el::PatType::WILD) {
    // The base case. This covers variable bindings too, which we
    // rewrote to (_ as x).
    return GeneralizeOne(G, vars, rhs, rhs_type, rhs_valuable);

  } else {
    // TODO: This should actually compile record patterns.
    CHECK(pat->type == el::PatType::TUPLE);

    std::vector<const il::Type *> shape;
    for (int i = 0; i < (int)pat->children.size(); i++) {
      shape.push_back(elab->NewEVar());
    }
    Unification::Unify("tuple pattern",
                       // Don't move; we use shape again below.
                       pool->Product(shape),
                       rhs_type);

    // val {l1: p1, l2: p2, l3: p3} as [v1, v2, ...] = rhs
    // in body

    // This is the same as the EL expression
    // val r = rhs
    // val v1 = r
    // val v2 = r
    // val p1 = #1 r
    // val p2 = #2 r
    // val p3 = #3 r

    std::string r = pool->NewVar("r");
    // PERF: Could use one of the variables in here, if non-empty.
    vars.push_back(r);
    // This is just a variable binding.
    const auto &[GG, rdecs] =
      GeneralizeOne(G, vars, rhs, rhs_type, rhs_valuable);

    // Now we have the subpatterns.
    // rdecs bound r, among other things.
    // const VarInfo *rvi = GG.Find(r);
    CHECK(GG.Find(r) != nullptr) << "Bug: Generated variable " << r <<
      " was not found in the context after GeneralizeOne.";

    // Since we have to instantiate the variable at evars if it's
    // polymorphic, the easiest way is to repeatedly elaborate
    // a synthetic EL expression that is just the variable.
    const el::Exp *el_rexp = el_pool->Var(r);

    // Now, for each subpattern, compile it recursively.

    std::vector<const il::Dec *> decs = rdecs;

    Context GGG = GG;
    for (int i = 0; i < (int)pat->children.size(); i++) {
      const Pat *p = pat->children[i];
      std::string label = StringPrintf("%d", i + 1);
      const auto &[rec_rhs, rec_rhs_type] = elab->Elab(GG, el_rexp);
      // but we are projecting a label. We checked above
      // that the rhs conforms to the tuple type, so we
      // do not need another unification.
      //
      // If the RHS was valuable, then projecting from it remains
      // valuable.
      const il::Exp *rhs = pool->Project(label, rec_rhs);
      const il::Type *rhs_type = shape[i];

      const auto &[Gi, decsi] = CompileIrrefutableRec(
          GGG, p, rhs, rhs_type, rhs_valuable);

      for (const Dec *d : decsi) decs.push_back(d);

      GGG = Gi;
    }

    return std::make_pair(GGG, decs);
  }
}

// Compile
//
// val x as y as z = rhs
//
// This is the base case of irrefutable patterns. The vector
// of variables may be empty. Importantly, this is the one
// case where we may perform polymorphic generalization.
std::pair<Context, std::vector<const Dec *>>
PatternCompilation::GeneralizeOne(
    const Context &G,
    const std::vector<std::string> &vars,
    const il::Exp *rhs,
    const il::Type *type,
    bool rhs_valuable) {

  if (rhs_valuable && !vars.empty()) {
    // Then we are attempting to do polymorphic generalization.

    // Get the free evars in the rhs's type.
    std::vector<EVar> free_evars = EVar::FreeEVarsInType(type);

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
    il::AstPool *pool = elab->pool;
    std::vector<std::string> gen_tyvars;
    // Used for instantiating a copy of the variable.
    std::vector<const Type *> gen_tyvar_args;
    for (int i = 0; i < (int)gen_evars.size(); i++) {
      std::string a = pool->NewVar("gen");
      gen_tyvars.push_back(a);
      const Type *av = pool->VarType(a);
      gen_tyvar_args.push_back(av);
      gen_evars[i].Set(av);
    }

    // Now bind one of the variables. We already checked that we
    // have at least one in the vector.
    const std::string &ov = vars[0];
    const std::string &ilov = pool->NewVar(ov);
    const Dec *odec = pool->ValDec(gen_tyvars, ilov, rhs);
    VarInfo oinfo{
      .tyvars = gen_tyvars,
      .type = type,
      .var = ilov,
    };

    std::vector<const Dec *> decs = {odec};
    Context GG = G.Insert(ov, oinfo);

    // Now bind the rest of the variables as copies. Most of the
    // time there are none, so we don't worry about simplifying.
    for (int i = 1; i < (int)vars.size(); i++) {
      const std::string &v = vars[0];
      const std::string &ilv = pool->NewVar(v);
      // ... but importantly we reuse the variable above as the
      // rhs.
      const Dec *dec = pool->ValDec(gen_tyvars, ilv,
                                    pool->Var(ov, gen_tyvar_args));
      VarInfo info{
        .tyvars = gen_tyvars,
        .type = type,
        .var = ilov,
      };

      GG = G.Insert(v, info);
      decs.push_back(dec);
    }

    return std::make_pair(GG, decs);
  } else {

    CHECK(false) << "Unimplemented: easy case!";

    return std::make_pair(Context(), std::vector<const Dec *>{});
  }
}

}  // namespace il

