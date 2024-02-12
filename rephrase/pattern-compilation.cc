
#include "pattern-compilation.h"

#include <vector>
#include <string>
#include <unordered_set>

#include "il.h"
#include "elaboration.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "util.h"

static constexpr bool VERBOSE = false;

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

  bool Empty() const {
    return Width() == 0 && Height() == 0;
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

  bool ColWild(int x) const {
    CHECK(x >= 0 && x < Width());
    for (int y = 0; y < Height(); y++) {
      if (Cell(x, y)->type != PatType::WILD)
        return false;
    }
    return true;
  }

  // Get the type of the first nontrivial pattern in this column.
  // Trivial patterns are WILD, VAR, ANN, AS.
  std::optional<PatType> GetColumnType(int x_target) {
    for (int y = 0; y < Height(); y++) {
      PatType pt = Cell(x_target, y)->type;
      switch (pt) {
      case PatType::WILD:
      case PatType::VAR:
      case PatType::ANN:
      case PatType::AS:
        continue;
      default:
        return {pt};
      }
    }
    return std::nullopt;
  }

  // Changes widths. Invalidates pointers.
  void DeleteColumn(int x_target) {
    const int old_width = Width();
    const int old_height = Height();
    CHECK(x_target >= 0 && x_target < old_width);

    std::vector<std::string> new_objs;
    std::vector<const il::Type *> new_types;
    for (int x = 0; x < Width(); x++) {
      if (x != x_target) {
        new_objs.push_back(std::move(objs[x]));
        new_types.push_back(types[x]);
      }
    }
    objs = std::move(new_objs);
    types = std::move(new_types);
    new_objs.clear();
    new_types.clear();

    std::vector<const el::Pat *> new_pats;
    for (int y = 0; y < old_height; y++) {
      for (int x = 0; x < old_width; x++) {
        const Pat *pat = pats[y * old_width + x];
        if (x != x_target) {
          new_pats.push_back(pat);
        }
      }
    }

    CHECK((int)new_pats.size() == (old_width - 1) * old_height);
    pats = std::move(new_pats);

    // Rows and defaults stay the same.
    CHECK((int)new_pats.size() == Width() * Height());
    CHECK((int)objs.size() == Width());
    CHECK((int)types.size() == Width());
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

  // Check that no row binds a variable more than once.
  for (const auto &[p, e_] : rows_in) {
    CheckAffine(p);
  }

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
    Dec dec = Dec{.x = var, .rhs = ve};

    Context GG = G.Insert(var,
                          VarInfo{
                            .tyvars = {},
                              .type = vt,
                              .var = var,
                              .primop = std::nullopt});

    const auto &[body_exp, body_type] = elab->Elab(GG, rows_in[0].second);
    return std::make_pair(pool->Let(dec.tyvars, dec.x, dec.rhs,
                                    body_exp), body_type);
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

  return Comp(G, matrix);
}

void PatternCompilation::CheckAffine(const Pat *orig_pat) const {
  // Quick success for common cases.
  if (orig_pat->type == PatType::VAR || orig_pat->type == PatType::WILD)
    return;

  std::unordered_set<std::string> vars;
  std::function<void(const Pat *)> Rec =
      [orig_pat, &vars, &Rec](const Pat *pat) {
      switch (pat->type) {
      case PatType::VAR:
        CHECK(!vars.contains(pat->var)) << "The same variable " <<
          pat->var << " is bound more than once in the pattern:\n" <<
          PatString(orig_pat);
        vars.insert(pat->var);
        break;
      case PatType::WILD:
        break;
      case PatType::AS:
        CHECK(!vars.contains(pat->var)) << "The same variable " <<
          pat->var << " is bound more than once in the pattern:\n" <<
          PatString(orig_pat);
        Rec(pat->a);
        break;
      case PatType::ANN:
        Rec(pat->a);
        break;
      case PatType::TUPLE:
        for (const Pat *child : pat->children) {
          Rec(child);
        }
        break;
      default:
        LOG(FATAL) << "Pattern type not handled in CheckAffine.";
      }
    };

  Rec(orig_pat);
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

      // z is just "_ as z", so treat it the same way.
      while (matrix.Cell(x, y)->type == PatType::VAR) {
        const Pat *op = matrix.Cell(x, y);
        const std::string nv = op->var;
        matrix.rows[y] = SimpleBind(nv, matrix.objs[x], matrix.rows[y]);
        matrix.Cell(x, y) = elab->el_pool->WildPat();
      }
    }
  }

  // Next, remove columns that just consist of wildcards.
  for (int x = 0; x < matrix.Width(); /* in loop */) {
    if (matrix.ColWild(x)) {
      matrix.DeleteColumn(x);
      // And try again on the new x.
    } else {
      x++;
    }
  }

  // If there are no rows, then we just have the default.
  if (matrix.Height() == 0) {
    return elab->Elab(G, matrix.def);
  }

  // If there are no case objects, then we just have the first
  // row.
  if (matrix.Width() == 0) {
    if (matrix.Height() != 1) {
      printf("Redundant match?\n");
    }
    return elab->Elab(G, matrix.rows[0]);
  }

  // Now, pick some column to split on. It makes sense to use some
  // heuristic here in the future, but for now I just take the first.
  CHECK(matrix.Width() > 0) << "Just checked this above.";

  // TODO: Split record and tuple patterns first.

  const std::optional<PatType> col_type = matrix.GetColumnType(0);
  CHECK(col_type.has_value()) << "Just cleaned the pattern, so it "
    "should not be entirely trivial.";

  switch (col_type.value()) {
  case PatType::TUPLE:
    LOG(FATAL) << "Unimplemented";
  case PatType::RECORD:
    LOG(FATAL) << "Unimplemented";
  case PatType::INT:
    LOG(FATAL) << "Unimplemented";
  }

  LOG(FATAL) << "Unimplemented";

  // TODO: Unify all the arms together!

  return std::make_pair(nullptr, nullptr);
}

std::pair<const Exp *, const Type *>
PatternCompilation::CompileIrrefutable(
      const Context &G,
      const el::Pat *pat,
      const el::Exp *rhs,
      const el::Exp *body) {
  // All patterns must be affine.
  CheckAffine(pat);

  const auto &[re, rt] = elab->Elab(G, rhs);

  // TODO: We'd probably get better error messages if we first do
  // a syntactic check for irrefutability.
  bool valuable = el::IsValuable(rhs);

  const auto &[GG, decs] =
    CompileIrrefutableRec(G, pat, re, rt, valuable);

  if (VERBOSE) {
    printf(AWHITE("Decs") ":\n");
    for (const Dec &dec : decs) {
      printf("  (%s) %s = %s\n", Util::Join(dec.tyvars, ",").c_str(),
             dec.x.c_str(), ExpString(dec.rhs).c_str());
    }

    printf(AWHITE("New context") ":\n%s\n(end)\n",
           GG.ToString().c_str());
    printf(AWHITE("Elaborating body") ":\n%s\n",
         ExpString(body).c_str());
  }
  const auto &[be, bt] = elab->Elab(GG, body);
  return std::make_pair(LetDecs(decs, be), bt);
}

const il::Exp *PatternCompilation::LetDecs(const std::vector<Dec> &decs,
                                           const il::Exp *body) {
  const il::Exp *ret = body;
  for (int i = decs.size() - 1; i >= 0; i--) {
    const Dec &dec = decs[i];
    ret = elab->pool->Let(dec.tyvars, dec.x, dec.rhs, ret);
  }
  return ret;
}

std::pair<Context, std::vector<PatternCompilation::Dec>>
PatternCompilation::CompileIrrefutableRec(
    const Context &G,
    const el::Pat *pat,
    const il::Exp *rhs,
    const il::Type *rhs_type,
    bool rhs_valuable) {
  el::AstPool *el_pool = elab->el_pool;
  il::AstPool *pool = elab->pool;

  // Initial rewrites:
  //  - Dispense with type constraints on the pattern.
  //    All these vars are bound to the case object.
  //  - Transform variables into wildcards.
  //  - Transform tuple patterns into record patterns.
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
        case el::PatType::TUPLE: {
          // Transform into the equivalent record.
          std::vector<std::pair<std::string, const Pat *>> lpat;
          lpat.reserve(pat->children.size());
          for (int i = 0; i < (int)pat->children.size(); i++) {
            lpat.emplace_back(StringPrintf("%d", i + 1),
                              pat->children[i]);
            printf(APURPLE("%d = %s") "\n",
                   i + 1,
                   PatString(pat->children[i]).c_str());
          }
          pat = el_pool->RecordPat(std::move(lpat));
          return;
        }
        case el::PatType::RECORD:
          return;
        }
      }
    }());

  if (verbose >= 2) {
    printf("Cleaned irrefutable pat: %s\n", PatString(pat).c_str());
  }

  CHECK(pat->type == el::PatType::WILD ||
        pat->type == el::PatType::RECORD);

  if (pat->type == el::PatType::WILD) {
    // The base case. This covers variable bindings too, which we
    // rewrote to (_ as x).
    return GeneralizeOne(G, vars, rhs, rhs_type, rhs_valuable);

  } else {
    // TODO HERE!: This should actually compile record patterns.

    CHECK(pat->type == el::PatType::RECORD);

    std::vector<std::pair<std::string, const il::Type *>> shape;
    shape.reserve(pat->str_children.size());
    for (const auto &[lab, child] : pat->str_children) {
      shape.push_back(std::make_pair(lab, elab->NewEVar()));
    }
    Unification::Unify("record pattern",
                       // Don't move; we use shape again below.
                       pool->RecordType(shape),
                       rhs_type);

    // val {l1: p1, l2: p2, l3: p3} as [v1, v2, ...] = rhs
    // in body

    // This is the same as the EL expression
    // val r = rhs
    // val v1 = r
    // val v2 = r
    // val p1 = #l1 r
    // val p2 = #l2 r
    // val p3 = #l3 r

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

    std::vector<Dec> decs = rdecs;

    Context GGG = GG;
    for (int i = 0; i < (int)pat->str_children.size(); i++) {
      const auto &[label, p] = pat->str_children[i];
      const auto &[rec_rhs, rec_rhs_type] = elab->Elab(GG, el_rexp);
      // but we are projecting a label. We checked above
      // that the rhs conforms to the record type, so we
      // do not need another unification.
      //
      // If the RHS was valuable, then projecting from it remains
      // valuable.
      const il::Exp *rhs = pool->Project(label, rec_rhs);
      CHECK(label == shape[i].first);
      const il::Type *rhs_type = shape[i].second;

      const auto &[Gi, decsi] = CompileIrrefutableRec(
          GGG, p, rhs, rhs_type, rhs_valuable);

      for (const Dec &d : decsi) decs.push_back(d);

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
std::pair<Context, std::vector<PatternCompilation::Dec>>
PatternCompilation::GeneralizeOne(
    const Context &G,
    std::vector<std::string> vars,
    const il::Exp *rhs,
    const il::Type *type,
    bool rhs_valuable) {

  il::AstPool *pool = elab->pool;

  // The bound type variables.
  std::vector<std::string> gen_tyvars;
  // Used for instantiating a copy of the variable.
  std::vector<const Type *> gen_tyvar_args;

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
    for (int i = 0; i < (int)gen_evars.size(); i++) {
      std::string a = pool->NewVar("gen");
      gen_tyvars.push_back(a);
      const Type *av = pool->VarType(a);
      gen_tyvar_args.push_back(av);
      gen_evars[i].Set(av);
    }

  } else {

    // No generalization, either because there are no variables
    // being bound, or because the rhs is not valuable. We just
    // generate a basic val dec with no type variables. Just
    // make sure we have a variable to bind.

    if (vars.empty()) {
      // XXX maybe would be cleaner of we had NewVar for EL as
      // well? But it's just a string.
      vars.push_back(pool->NewVar("unused"));
    }
  }

  // Now bind one of the variables. We already checked that we
  // have at least one in the vector.
  CHECK(!vars.empty());
  const std::string &ov = vars[0];
  const std::string &ilov = pool->NewVar(ov);
  Dec odec{.tyvars = gen_tyvars, .x = ilov, .rhs = rhs};
  VarInfo oinfo{
    .tyvars = gen_tyvars,
    .type = type,
    .var = ilov,
  };

  std::vector<Dec> decs = {odec};
  Context GG = G.Insert(ov, oinfo);

  // Now bind the rest of the variables as copies. Most of the
  // time there are none, so we don't worry about simplifying.
  for (int i = 1; i < (int)vars.size(); i++) {
    const std::string &v = vars[i];
    const std::string &ilv = pool->NewVar(v);
    // ... but importantly we reuse the variable above as the
    // rhs.
    Dec dec{
      .tyvars = gen_tyvars,
      .x = ilv,
      .rhs = pool->Var(gen_tyvar_args, ilov),
    };
    VarInfo info{
      .tyvars = gen_tyvars,
      .type = type,
      .var = ilv,
    };

    GG = GG.Insert(v, info);
    decs.push_back(std::move(dec));
  }

  return std::make_pair(GG, decs);
}

}  // namespace il

