
#include "pattern-compilation.h"

#include <set>
#include <vector>
#include <string>
#include <unordered_set>

#include "il.h"
#include "il-util.h"
#include "elaboration.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "util.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"

static constexpr int VERBOSE = 0;

namespace il {

using Pat = el::Pat;
using PatType = el::PatType;

static const Type *SelectLabel(const Type *sum_type,
                               const std::string &label) {
  for (const auto &[lab, t] : sum_type->Sum()) {
    if (lab == label) return t;
  }
  LOG(FATAL) << "Label " << label << " not found in sum type: "
             << TypeString(sum_type);
  return nullptr;
}

// The pattern matrix.
struct PatternCompilation::Matrix {
  Matrix(el::AstPool *pool) : pool(pool) {}

  int Width() const {
    return (int)objs.size();
  }

  int Height() const {
    return (int)rows.size();
  }

  bool Empty() const {
    return Width() == 0 && Height() == 0;
  }

  // Get the object (EL variable) in the xth column.
  const std::string &Obj(int x) const {
    CHECK(x >= 0 && x < Width());
    return objs[x];
  }

  const il::Type *Type(int x) const {
    CHECK(x >= 0 && x < Width());
    return types[x];
  }


  // Get the IL expression corresponding to the xth column.
  std::pair<const Exp *, const il::Type *>
  GetObjIL(il::AstPool *pool, const ElabContext &G, int x) {
    CHECK(x >= 0 && x < Width());
    const VarInfo *vi = G.Find(objs[x]);
    CHECK(vi != nullptr) << "Ill-formed pattern: The case object " <<
      objs[x] << " was not found in the context!";
    return std::make_pair(pool->Var({}, vi->var), vi->type);
  }

  // For splitting: Create a copy of the matrix where the value in the
  // column is known to be some value, specified by the function Match
  // (should be testing for a specific constant in an INT pattern, for
  // example). Stops when it reaches a WILD pattern. The resulting
  // matrix removes that column (its value is known so does not need
  // to be tested). This duplicates the default, so you may want to
  // hoist that first. (It also duplicates row expressions but it's
  // expected that those will be discarded from the original matrix,
  // since they should only match through this path.)
  //
  // The type E is extracted from each match; return nullopt to reject
  // the case. The returned value also gives the vector of extracts,
  // which will be the same height as the returned matrix.
  // For SumCase, this is used to pull out the subpattern so that
  // it can be appended to the matrix. It's a trivial type for
  // intcase/stringcase because we don't need any information other
  // than the match against the int.
  template<class E>
  std::pair<Matrix, std::vector<E>>
  Quotient(int target_x,
           std::function<std::optional<E>(const Pat *)> Match) const {
    CHECK(target_x >= 0 && target_x < Width());
    std::vector<E> extracts;
    // There will be one fewer column.
    Matrix quot(pool);
    quot.def = def;
    for (int x = 0; x < Width(); x++) {
      if (x != target_x) {
        quot.objs.push_back(objs[x]);
        quot.types.push_back(types[x]);
      }
    }

    for (int y = 0; y < Height(); y++) {
      // First check to see if we're done.
      if (Cell(target_x, y)->type == PatType::WILD)
        break;

      // Are we keeping this row?
      if (const std::optional<E> ex = Match(Cell(target_x, y))) {
        quot.rows.push_back(rows[y]);
        extracts.push_back(ex.value());
        for (int x = 0; x < Width(); x++) {
          if (x != target_x) {
            quot.pats.push_back(Cell(x, y));
          }
        }
      }
    }

    CHECK((int)quot.pats.size() == quot.Width() * quot.Height());
    CHECK((int)extracts.size() == quot.Height());
    return std::make_pair(quot, extracts);
  }

  // Add a column at the end. The cells will be initialized
  // to WILD patterns, but can be modified.
  void AddColumn(const std::string &el_obj_var,
                 const il::Type *obj_type) {
    if (VERBOSE) {
      printf("AddColumn. current %dx%d\n", Width(), Height());
    }
    const int old_width = Width();
    const int old_height = Height();

    objs.push_back(el_obj_var);
    types.push_back(obj_type);

    std::vector<const el::Pat *> new_pats;
    new_pats.reserve((old_width + 1) * old_height);
    for (int y = 0; y < old_height; y++) {
      for (int x = 0; x < old_width + 1; x++) {
        if (x < old_width) {
          const Pat *pat = pats[y * old_width + x];
          new_pats.push_back(pat);
        } else {
          new_pats.push_back(pool->WildPat());
        }
      }
    }

    CHECK((int)new_pats.size() == (old_width + 1) * old_height);
    pats = std::move(new_pats);
    new_pats.clear();

    // Rows and defaults stay the same.
    CHECK((int)objs.size() == Width());
    CHECK((int)types.size() == Width());
    CHECK((int)pats.size() == Width() * Height()) <<
      pats.size() << " vs " << Width() << " * " << Height();
  }

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

  bool RowWild(int y) const {
    CHECK(y >= 0 && y < Height());
    for (int x = 0; x < Width(); x++) {
      if (Cell(x, y)->type != PatType::WILD)
        return false;
    }
    return true;
  }

  bool ColHasAs(int x) const{
    for (int y = 0; y < Height(); y++)
      if (Cell(x, y)->type == PatType::AS)
        return true;
    return false;
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

  // Deletes the row and everything after it. Changes height
  // to be y_target.
  // Invalidates pointers.
  void DeleteRowsFrom(int y_target) {
    const int old_width = Width();
    const int old_height = Height();
    CHECK(y_target >= 0 && y_target <= old_height);

    rows.resize(y_target);
    pats.resize(y_target * old_width);

    // Objects and types and defaults stay the same.
    CHECK((int)pats.size() == Width() * Height());
    CHECK((int)objs.size() == Width());
    CHECK((int)types.size() == Width());
    CHECK(Height() == y_target);
  }

  // Delete the first n rows.
  void DeleteFirstRows(int n) {
    const int old_width = Width();
    const int old_height = Height();
    CHECK(n >= 0 && n <= old_height);

    const int new_height = old_height - n;

    std::vector<const el::Exp *> new_rows;
    for (int y = n; y < old_height; y++)
      new_rows.push_back(rows[y]);
    std::vector<const Pat *> new_pats;
    for (int y = n; y < old_height; y++) {
      for (int x = 0; x < old_width; x++) {
        new_pats.push_back(Cell(x, y));
      }
    }

    rows = std::move(new_rows);
    pats = std::move(new_pats);

    // Objects and types and defaults stay the same.
    CHECK((int)rows.size() == new_height);
    CHECK((int)pats.size() == Width() * Height());
    CHECK((int)objs.size() == Width());
    CHECK((int)types.size() == Width());
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
    new_pats.clear();

    // Rows and defaults stay the same.
    CHECK((int)pats.size() == Width() * Height());
    CHECK((int)objs.size() == Width());
    CHECK((int)types.size() == Width());
  }

  std::string ToString() const {
    std::string ret = "case (";
    CHECK((int)objs.size() == Width());
    CHECK((int)types.size() == Width());
    for (int x = 0; x < Width(); x++) {
      if (x != 0) StringAppendF(&ret, ", ");
      StringAppendF(&ret, "%s : %s",
                    objs[x].c_str(),
                    TypeString(types[x]).c_str());
    }
    StringAppendF(&ret, ") of\n");

    // Would be nice to use a table here.
    for (int y = 0; y < Height(); y++) {
      StringAppendF(&ret, " |");
      for (int x = 0; x < Width(); x++) {
        const Pat *p = Cell(x, y);
        if (p == nullptr) {
          StringAppendF(&ret, " NULL?!");
        } else {
          StringAppendF(&ret, " %s", PatString(p).c_str());
        }
      }

      const el::Exp *exp = rows[y];
      if (exp == nullptr) {
        StringAppendF(&ret, " => NULL!?");
      } else {
        StringAppendF(&ret, " => %s\n", el::ExpString(exp).c_str());
      }
    }
    StringAppendF(&ret, "   _ => %s\n", el::ExpString(def).c_str());

    return ret;
  }

#define AKEYWORD(s) AYELLOW(s)

  std::string ShortColorString() const {
    std::string ret =
      StringPrintf(AKEYWORD("case") " %s " AKEYWORD("of") "\n",
                   Util::Join(objs, AGREY(",") " ").c_str());
    // Would be nice to use a table here.
    for (int y = 0; y < Height(); y++) {
      StringAppendF(&ret, " |");
      for (int x = 0; x < Width(); x++) {
        const Pat *p = Cell(x, y);
        StringAppendF(&ret, " %s", el::ShortColorPatString(p).c_str());
      }

      const el::Exp *exp = rows[y];
      StringAppendF(&ret, " => %s\n", el::ShortColorExpString(exp).c_str());
    }
    StringAppendF(&ret, "   " AORANGE("_") " => %s\n",
                  el::ShortColorExpString(def).c_str());

    return ret;
  }


  // The pattern objects. These are always EL variables.
  std::vector<std::string> objs;
  // Parallel array of the types.
  std::vector<const il::Type *> types;

  // Not owned.
  el::AstPool *pool = nullptr;

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
    const ElabContext &G,
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
      // printf("Warning: redundant match?\n");
    }
    el::AstPool *el_pool = elab->el_pool;
    il::AstPool *pool = elab->pool;
    const auto &[ve, vt] = elab->Elab(G, el_pool->Var(obj));
    std::string var = rows_in[0].first->str;
    std::string il_var = var;
    Dec dec = Dec{.x = var, .rhs = ve};

    ElabContext GG = G.Insert(var,
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
  Matrix matrix(elab->el_pool);
  matrix.objs = {obj};
  matrix.types = {obj_type};

  matrix.pats.reserve(rows_in.size());
  matrix.rows.reserve(rows_in.size());
  for (const auto &[pat, exp] : rows_in) {
    matrix.pats.push_back(pat);
    matrix.rows.push_back(exp);
  }

  matrix.def = elab->el_pool->Fail(elab->el_pool->String("match"));

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
        CHECK(!vars.contains(pat->str)) << "The same variable " <<
          pat->str << " is bound more than once in the pattern:\n" <<
          PatString(orig_pat);
        vars.insert(pat->str);
        return;
      case PatType::WILD:
        return;
      case PatType::AS:
        Rec(pat->a);
        Rec(pat->b);
        return;
      case PatType::ANN:
        Rec(pat->a);
        return;
      case PatType::RECORD:
        for (const auto &[lab, child] : pat->str_children) {
          Rec(child);
        }
        return;
      case PatType::TUPLE:
        for (const Pat *child : pat->children) {
          Rec(child);
        }
        return;
      case PatType::INT:
        return;
      case PatType::STRING:
        return;
      case PatType::BOOL:
        return;
      case PatType::APP:
        Rec(pat->a);
        return;
      case PatType::OBJECT:
        for (const auto &[lab, child] : pat->str_children) {
          Rec(child);
        }
        return;
      }
      LOG(FATAL) << "Pattern type " << PatTypeString(pat->type)
                 << " not handled in CheckAffine.";
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
    const ElabContext &G,
    Matrix matrix) {

  auto Error = [this, &matrix](const std::string &msg) {
      return MatrixError(matrix, msg);
    };

  if (VERBOSE) {
    printf(AWHITE("Comp():") "\n");
    printf("%s\n",
           matrix.ToString().c_str());
  }

  // This follows the same approach as humlock, itself loosely
  // based on TILT.
  //
  // We repeatedly apply greedy simplifications to the pattern
  // matrix to make it "clean," then perform some kind of
  // interesting split.

  // First, we handle the following types of patterns:
  //   - VAR is rewritten to WILD after binding a copy of the variable.
  //   - TUPLE is rewritten to the equivalent record.
  //   - ANN patterns are unified and replaced with the subpattern.
  //   - Empty OBJECT patterns are equivalent to (_ : obj).
  for (int y = 0; y < matrix.Height(); y++) {
    for (int x = 0; x < matrix.Width(); x++) {
      while (matrix.Cell(x, y)->type == PatType::ANN) {
        const Pat *op = matrix.Cell(x, y);
        const il::Type *at = elab->ElabType(G, op->ann);
        Unification::Unify(Error("pattern type constraint"),
                           at, matrix.Type(x));
        matrix.Cell(x, y) = op->a;
      }

      // Transform a variable z into a wildcard match, but bind that
      // variable to the corresponding case object.
      if (matrix.Cell(x, y)->type == PatType::VAR) {
        const Pat *op = matrix.Cell(x, y);
        const std::string nv = op->str;
        if (VERBOSE) {
          printf("Cleaned var " ABLUE("%s") "\n", nv.c_str());
        }
        matrix.rows[y] = SimpleBind(nv, matrix.objs[x], matrix.rows[y]);
        matrix.Cell(x, y) = elab->el_pool->WildPat();
      }

      if (matrix.Cell(x, y)->type == PatType::TUPLE) {
        const Pat *op = matrix.Cell(x, y);
        std::vector<std::pair<std::string, const Pat *>> rp;
        for (int i = 0; i < (int)op->children.size(); i++) {
          rp.emplace_back(StringPrintf("%d", i + 1), op->children[i]);
        }
        matrix.Cell(x, y) = elab->el_pool->RecordPat(std::move(rp));
      }

      if (matrix.Cell(x, y)->type == PatType::OBJECT &&
          matrix.Cell(x, y)->str_children.empty()) {
        Unification::Unify(Error("object pattern"),
                           matrix.Type(x), elab->pool->ObjType());
        matrix.Cell(x, y) = elab->el_pool->WildPat();
      }
    }
  }

  if (VERBOSE) {
    printf(APURPLE("Cleaned") ":\n");
    printf("%s\n",
           matrix.ToString().c_str());
  }

  // Once we find a row with all wildcards, the row and all following it
  // can be dropped (and it becomes the new default).
  for (int y = 0; y < matrix.Height(); y++) {
    if (matrix.RowWild(y)) {
      matrix.def = matrix.rows[y];
      matrix.DeleteRowsFrom(y);
      break;
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

  // Split any columns with AS patterns, by duplicating the case
  // object. To avoid messes, we split all the AS patterns in a column
  // at once.
  for (int x = 0; x < matrix.Width(); x++) {
    if (matrix.ColHasAs(x)) {
      return SplitAsPattern(G, matrix, x);
    }
  }

  // First, split any record patterns.
  for (int x = 0; x < matrix.Width(); x++) {
    const std::optional<PatType> col_type = matrix.GetColumnType(x);
    CHECK(col_type.has_value()) << "Just cleaned the pattern, so it "
      "should not be entirely trivial. Column: " << x;
    if (col_type == PatType::RECORD) {
      return SplitRecordPattern(G, matrix, x);
    }
  }

  CHECK(matrix.Width() > 0) << "Just checked this above.";
  CHECK(matrix.Height() > 0) << "Just checked this above.";

  // Now, the first row can only be WILD or refutable patterns like INT.
  // We find one of the refutable patterns.
  for (int x = 0; x < matrix.Width(); x++) {
    switch (matrix.Cell(x, 0)->type) {
    case PatType::VAR:
    case PatType::AS:
    case PatType::ANN:
    case PatType::TUPLE:
    case PatType::RECORD:
      LOG(FATAL) << "Bug: These should have been transformed away "
        "already.";
    case PatType::WILD:
      // Can't effectively split on wildcards.
      continue;
    case PatType::INT:
      return SplitIntPattern(G, matrix, x);
    case PatType::BOOL:
      return SplitBoolPattern(G, matrix, x);
    case PatType::STRING:
      return SplitStringPattern(G, matrix, x);
    case PatType::APP:
      return SplitAppPattern(G, matrix, x);
    case PatType::OBJECT:
      return SplitObjectPattern(G, matrix, x);
    default:
      LOG(FATAL) << "Unhandled pattern type when looking for "
        "a split: " << PatTypeString(matrix.Cell(x, 0)->type);
    }
  }

  LOG(FATAL) << "Bug: The first row of the matrix must have "
    "something other than wildcards in it, or else we would "
    "have reduced its size above.";

  // TODO: Unify all the arms together!

  return std::make_pair(nullptr, nullptr);
}

// Split the column x, which must begin with an int pattern. The
// column must be cleaned, meaning it only has INT and WILD.
std::pair<const Exp *, const Type *>
PatternCompilation::SplitIntPattern(
    const ElabContext &G,
    Matrix matrix,
    int x) {

  auto Error = [this, &matrix](const std::string &msg) {
      return MatrixError(matrix, msg);
    };

  CHECK(x >= 0 && x < matrix.Width());
  CHECK(matrix.Cell(x, 0)->type == PatType::INT);

  Unification::Unify(Error("int pattern"),
                     matrix.types[x], elab->pool->IntType());

  // The pattern must look like this (here x selecting the second
  // column):
  //
  // case (a,  b, c) of
  //       p1  1  r1  => e1
  //       p2  5  r2  => e2
  //       p3  1  r3  => e3
  //          ...
  //       pn  _  rn  => en
  //       pn1 7  rn1 => en1
  //          ...
  //
  // where some non-empty prefix of the column is integers.
  // We generate:
  //
  // intcase b of
  //     1 => (case (a,  c) of
  //                 p1  r1 => e1     ;; all the cases for 1
  //                 p3  r3 => e3
  //                 _      => (case (a,  b, c) of
  //                                  pn  _  rn  => en
  //                                  pn1 7  rn1 => en1
  //                                  ...)
  //     5 => (case (a, c) of
  //                 p2  r2 => e2     ;; all the cases for 5
  //                 _      => (case (a,  b, c) of
  //                                  pn  _  rn  => en
  //                                  pn1 7  rn1 => en1
  //                                  ...)
  //     _ => (case (a,  b, c) of
  //                 pn  _  rn  => en
  //                 pn1 7  rn1 => en1
  //                 ...)
  // Note how the default is the same in each case (it means: the object
  // variable doesn't match anything in the prefix), so we hoist
  // this out.

  int constant_height = 0;

  // The collated matching cases (up until any wildcard), in the original
  // order, with the matrix that implements the rest of the pattern.
  std::vector<std::pair<BigInt, Matrix>> int_cases;
  // Since we get all the matching cases when we see the first one,
  // keep track of what we've already done.
  std::set<BigInt> done;
  for (int y = 0; y < matrix.Height(); y++) {
    const Pat *cell = matrix.Cell(x, y);
    if (cell->type == PatType::INT) {
      if (!done.contains(cell->integer)) {
        done.insert(cell->integer);
        const auto &[mtx, extracts_] =
          matrix.Quotient<bool>(x,
                                [&](const Pat *p) -> std::optional<bool> {
                                  if (p->type == PatType::INT &&
                                      p->integer == cell->integer) {
                                    return {true};
                                  } else {
                                    return std::nullopt;
                                  }
                                });

        int_cases.emplace_back(cell->integer, mtx);
      }
    } else {
      CHECK(cell->type == PatType::WILD) << "Inconsistent pattern types";
      break;
    }
    constant_height++;
  }

  CHECK(constant_height > 0);

  matrix.DeleteFirstRows(constant_height);

  // The failure continuation, if none of the intcases match.
  const auto &[fexp, ftype] = Comp(G, matrix);

  // Bind hoisted failure continuation.
  const Type *fn_type = elab->pool->Arrow(elab->pool->RecordType({}),
                                          ftype);
  const Exp *fn = elab->pool->Fn("",
                                 elab->pool->NewVar("unused"),
                                 fn_type,
                                 fexp);
  const std::string el_cont_var = elab->pool->NewVar("hoist");
  const std::string il_cont_var = elab->pool->NewVar(el_cont_var);
  ElabContext GG = G.Insert(el_cont_var,
                        VarInfo{
                          .tyvars = {},
                          .type = fn_type,
                          .var = il_cont_var});

  // Failure continuation calls the hoisted expression.
  const el::Exp *failure_cont =
    elab->el_pool->App(elab->el_pool->Var(el_cont_var),
                       elab->el_pool->Record({}));

  const auto &[obj_exp, obj_type] = matrix.GetObjIL(elab->pool, GG, x);
  Unification::Unify(Error("int pattern"),
                     obj_type, elab->pool->IntType());

  std::vector<std::pair<BigInt, const Exp *>> arms;
  arms.reserve(int_cases.size());

  // Update the defaults for the arms: It's the same failure
  // continuation in each case. We could add annotations that mark
  // values as known, although it's also evident from the intcase
  // itself.
  for (auto &[bi, mtx] : int_cases) {
    mtx.def = failure_cont;
  }

  for (const auto &[bi, mtx] : int_cases) {
    const auto &[arm_exp, arm_type] = Comp(GG, mtx);
    Unification::Unify(Error("int pattern result"), arm_type, ftype);
    arms.emplace_back(bi, arm_exp);
  }
  const Exp *intcase =
    elab->pool->IntCase(obj_exp,
                        std::move(arms),
                        elab->pool->App(
                            elab->pool->Var({}, il_cont_var),
                            elab->pool->Record({})));

  const Exp *ret =
    elab->pool->Let({}, il_cont_var, fn, intcase);

  return std::make_pair(ret, ftype);
}

// As above, but it's just a finite radix.
std::pair<const Exp *, const Type *>
PatternCompilation::SplitBoolPattern(
    const ElabContext &G,
    Matrix matrix,
    int x) {

  auto Error = [this, &matrix](const std::string &msg) {
      return MatrixError(matrix, msg);
    };

  CHECK(x >= 0 && x < matrix.Width());
  CHECK(matrix.Cell(x, 0)->type == PatType::BOOL);

  Unification::Unify(Error("bool pattern"), matrix.types[x],
                     elab->pool->BoolType());

  int constant_height = 0;

  // The collated matching cases (up until any wildcard), in the original
  // order, with the matrix that implements the rest of the pattern.
  std::vector<std::pair<bool, Matrix>> bool_cases;
  // Since we get all the matching cases when we see the first one,
  // keep track of what we've already done.
  std::set<bool> done;
  for (int y = 0; y < matrix.Height(); y++) {
    const Pat *cell = matrix.Cell(x, y);
    if (cell->type == PatType::BOOL) {
      if (!done.contains(cell->boolean)) {
        done.insert(cell->boolean);
        const auto &[mtx, extracts_] =
          matrix.Quotient<bool>(x,
                                [&](const Pat *p) -> std::optional<bool> {
                                  if (p->type == PatType::BOOL &&
                                      p->boolean == cell->boolean) {
                                    return {true};
                                  } else {
                                    return std::nullopt;
                                  }
                                });

        bool_cases.emplace_back(cell->boolean, mtx);
      }
    } else {
      CHECK(cell->type == PatType::WILD) << "Inconsistent pattern types";
      break;
    }
    constant_height++;
  }

  CHECK(constant_height > 0);

  matrix.DeleteFirstRows(constant_height);

  // The failure continuation, if we don't have both a true and false
  // branch.
  const auto &[fail_exp, fail_type] = Comp(G, matrix);

  const Type *fn_type = elab->pool->Arrow(elab->pool->RecordType({}),
                                          fail_type);
  const Exp *fn = elab->pool->Fn("",
                                 elab->pool->NewVar("unused"),
                                 fn_type,
                                 fail_exp);
  const std::string el_cont_var = elab->pool->NewVar("hoist");
  const std::string il_cont_var = elab->pool->NewVar(el_cont_var);
  ElabContext GG = G.Insert(el_cont_var,
                        VarInfo{
                          .tyvars = {},
                          .type = fn_type,
                          .var = il_cont_var});

  // Failure continuation calls the hoisted expression.
  const el::Exp *failure_cont =
    elab->el_pool->App(elab->el_pool->Var(el_cont_var),
                       elab->el_pool->Record({}));

  const auto &[obj_exp, obj_type] = matrix.GetObjIL(elab->pool, G, x);
  Unification::Unify(Error("bool pattern"), obj_type, elab->pool->BoolType());

  const Exp *true_arm = nullptr;
  const Exp *false_arm = nullptr;

  // Update the defaults for the arms: It's the same failure
  // continuation in each case.
  for (auto &[b, mtx] : bool_cases) {
    mtx.def = failure_cont;
  }

  for (const auto &[b, mtx] : bool_cases) {
    const auto &[arm_exp, arm_type] = Comp(GG, mtx);
    Unification::Unify(Error("bool pattern result"), arm_type, fail_type);
    if (b) {
      CHECK(true_arm == nullptr);
      true_arm = arm_exp;
    } else {
      CHECK(false_arm == nullptr);
      false_arm = arm_exp;
    }
  }

  CHECK(true_arm != nullptr || false_arm != nullptr);
  if (true_arm == nullptr || false_arm == nullptr) {
    const il::Exp *il_failure_cont =
      elab->pool->App(
          elab->pool->Var({}, il_cont_var),
          elab->pool->Record({}));
    if (true_arm == nullptr) true_arm = il_failure_cont;
    if (false_arm == nullptr) false_arm = il_failure_cont;
  }

  // "boolcase" is just "if"
  const Exp *boolcase =
    elab->pool->If(obj_exp,
                   true_arm,
                   false_arm);

  const Exp *ret =
    elab->pool->Let({}, il_cont_var, fn, boolcase);

  return std::make_pair(ret, fail_type);
}


// Split the column x, which must begin with a string pattern. The
// column must be cleaned, meaning it only has STRING and WILD.
std::pair<const Exp *, const Type *>
PatternCompilation::SplitStringPattern(
    const ElabContext &G,
    Matrix matrix,
    int x) {

  auto Error = [this, &matrix](const std::string &msg) {
      return MatrixError(matrix, msg);
    };

  CHECK(x >= 0 && x < matrix.Width());
  CHECK(matrix.Cell(x, 0)->type == PatType::STRING);

  Unification::Unify(Error("string pattern"),
                     matrix.types[x], elab->pool->StringType());

  // The pattern must look like this (here x selecting the second
  // column):
  //
  // case (a,  b, c) of
  //       p1  "x"  r1  => e1
  //       p2  "y"  r2  => e2
  //       p3  "x"  r3  => e3
  //          ...
  //       pn   _   rn  => en
  //       pn1 "x"  rn1 => en1
  //          ...
  //
  // where some non-empty prefix of the column is constant strings.
  // This is just like the approach taken in intcase; so see that
  // code above.

  int constant_height = 0;

  // The collated matching cases (up until any wildcard), in the original
  // order, with the matrix that implements the rest of the pattern.
  std::vector<std::pair<std::string, Matrix>> string_cases;
  // Since we get all the matching cases when we see the first one,
  // keep track of what we've already done.
  std::unordered_set<std::string> done;
  for (int y = 0; y < matrix.Height(); y++) {
    const Pat *cell = matrix.Cell(x, y);
    if (cell->type == PatType::STRING) {
      if (!done.contains(cell->str)) {
        done.insert(cell->str);
        const auto &[mtx, extracts_] =
          matrix.Quotient<bool>(x,
                                [&](const Pat *p) -> std::optional<bool> {
                                  if (p->type == PatType::STRING &&
                                      p->str == cell->str) {
                                    return {true};
                                  } else {
                                    return std::nullopt;
                                  }
                                });
        string_cases.emplace_back(cell->str, mtx);
      }
    } else {
      CHECK(cell->type == PatType::WILD) << "Inconsistent pattern types";
      break;
    }
    constant_height++;
  }

  CHECK(constant_height > 0);

  matrix.DeleteFirstRows(constant_height);

  // The failure continuation, if none of the stringcases match.
  const auto &[fexp, ftype] = Comp(G, matrix);

  // Bind hoisted failure continuation.
  const Type *fn_type = elab->pool->Arrow(elab->pool->RecordType({}),
                                          ftype);
  const Exp *fn = elab->pool->Fn("",
                                 elab->pool->NewVar("unused"),
                                 fn_type,
                                 fexp);
  const std::string el_cont_var = elab->pool->NewVar("hoist");
  const std::string il_cont_var = elab->pool->NewVar(el_cont_var);
  ElabContext GG = G.Insert(el_cont_var,
                        VarInfo{
                          .tyvars = {},
                          .type = fn_type,
                          .var = il_cont_var});

  // Failure continuation calls the hoisted expression.
  const el::Exp *failure_cont =
    elab->el_pool->App(elab->el_pool->Var(el_cont_var),
                       elab->el_pool->Record({}));

  const auto &[obj_exp, obj_type] = matrix.GetObjIL(elab->pool, GG, x);
  Unification::Unify(Error("string pattern"),
                     obj_type, elab->pool->StringType());

  std::vector<std::pair<std::string, const Exp *>> arms;
  arms.reserve(string_cases.size());

  // Update the defaults for the arms: It's the same failure
  // continuation in each case. We could add annotations that mark
  // values as known, although it's also evident from the stringcase
  // itself.
  for (auto &[s, mtx] : string_cases) {
    mtx.def = failure_cont;
  }

  for (const auto &[s, mtx] : string_cases) {
    const auto &[arm_exp, arm_type] = Comp(GG, mtx);
    Unification::Unify(Error("int pattern result"),
                       arm_type, ftype);
    arms.emplace_back(s, arm_exp);
  }
  const Exp *stringcase =
    elab->pool->StringCase(obj_exp,
                           std::move(arms),
                           elab->pool->App(
                               elab->pool->Var({}, il_cont_var),
                               elab->pool->Record({})));

  const Exp *ret =
    elab->pool->Let({}, il_cont_var, fn, stringcase);

  return std::make_pair(ret, ftype);
}

// Split the column x, which must begin with an app pattern. The
// column must be cleaned, meaning it only has APP and WILD.
std::pair<const Exp *, const Type *>
PatternCompilation::SplitAppPattern(
    const ElabContext &G,
    Matrix matrix,
    int x) {

  auto Error = [this, &matrix](const std::string &msg) {
      return MatrixError(matrix, msg);
    };

  CHECK(x >= 0 && x < matrix.Width());
  const Pat *first_pat = matrix.Cell(x, 0);
  CHECK(first_pat->type == PatType::APP);

  // This is just like int/stringcase except for the following:
  //  - The (finite) domain comes from the datatype that is being destructed.
  //  - We need to destruct the case object (unroll) to get a sum.
  //  - The arms bind a variable for the contents of the sum.
  //

  auto GetConstructor = [&](const Pat *p) {
      CHECK(p->type == PatType::APP) << "Bug! Precondition.";
      const std::string &ctor = p->str;
      const VarInfo *vi = G.Find(ctor);
      CHECK(vi != nullptr) << "Unbound constructor " << ctor <<
        " in application pattern: " << PatString(p);
      CHECK(vi->ctor.has_value()) << "Identifier " << ctor << " is not "
        "a constructor in application pattern: " << PatString(p);

      const auto &[first_idx, mu_type, label] = vi->ctor.value();

      const Type *monotype = elab->EVarize(vi->tyvars, mu_type);
      if (VERBOSE) {
        printf("Mu Monotype: %s\n",
               TypeString(monotype).c_str());
      }

      return std::make_tuple(first_idx,
                             monotype,
                             label);
    };

  // First, look up the first constructor to get the mu/sum.
  const auto &[first_idx, mu_type, label] = GetConstructor(first_pat);

  Unification::Unify(Error("app pattern"), matrix.types[x], mu_type);

  // The pattern must look like this (here x selecting the second
  // column):
  //
  // case (a,  b,     c) of
  //       p1  (A q1)  r1  => e1
  //       p2  (B q2)  r2  => e2
  //       p3  (A q3)  r3  => e3
  //          ...
  //       pn  _       rn  => en
  //       pn1 (B qn1) rn1 => en1
  //          ...
  //
  // where some non-empty prefix of the column is application
  // patterns.
  // We generate:
  //
  // sumcase b of
  //     A va => (case (a, va, c) of
  //                    p1 q1 r1 => e1     ;; all the cases for A
  //                    p3 q3 r3 => e3
  //                    _        => hoisted ()
  //     B vb => (case (a, vb, c) of
  //                    p2 q2 => e2        ;; all the cases for B
  //                    _      => hoisted ()
  //     _ => hoisted ()
  //
  // As in the intcase and stringcase routines, we hoist out the
  // failure continuation so that it is not duplicated.

  int constant_height = 0;


  // The collated matching cases (up until any wildcard), in the
  // original order. The elements are the sum label, the bound el
  // variable and bound il variable, its type, and the matrix that
  // implements the rest of the pattern.
  std::vector<std::tuple<std::string,
                         std::string, std::string,
                         const il::Type *,
                         Matrix>> sum_cases;
  // Set of labels we've already processed.
  std::set<std::string> done;

  for (int y = 0; y < matrix.Height(); y++) {
    const Pat *cell = matrix.Cell(x, y);
    if (cell->type == PatType::APP) {
      if (!done.contains(cell->str)) {

        // Get the constructor type.
        const auto &[mu_idx, mu_type, label] = GetConstructor(cell);

        Unification::Unify(Error("app pattern"), matrix.types[x], mu_type);

        // The variable bound by the sumcase for this case. The el
        // variable is just used to denote the object in the case
        // expression.
        std::string el_var = elab->pool->NewVar(Util::lcase(cell->str));
        std::string il_var = elab->pool->NewVar(el_var);
        done.insert(cell->str);

        const auto &[mtx_small, extracts] =
          matrix.Quotient<const Pat *>(
              x,
              [&](const Pat *p) -> std::optional<const Pat *> {
                if (p->type == PatType::APP &&
                    p->str == cell->str) {
                  return std::make_optional(p->a);
                } else {
                  return std::nullopt;
                }
              });

        // Add a column for the subpatterns.
        Matrix mtx = mtx_small;
        const Type *sum_type = elab->pool->UnrollType(mu_type);
        if (VERBOSE) {
          printf(
              "Mu type: %s\n"
              "Unrolled type: %s\n",
              TypeString(mu_type).c_str(),
              TypeString(sum_type).c_str());
        }
        const Type *col_type = SelectLabel(sum_type, label);
        const int new_x = mtx.Width();
        mtx.AddColumn(el_var, col_type);

        CHECK(mtx.Height() == (int)extracts.size());
        for (int y = 0; y < mtx.Height(); y++)
          mtx.Cell(new_x, y) = extracts[y];

        sum_cases.emplace_back(
            label, el_var, il_var, col_type, std::move(mtx));
      }
    } else {
      CHECK(cell->type == PatType::WILD) << "Inconsistent pattern types";
      break;
    }
    constant_height++;
  }

  CHECK(constant_height > 0);

  matrix.DeleteFirstRows(constant_height);

  // The failure continuation, if none of the sumcases match.
  const auto &[fexp, ftype] = Comp(G, matrix);

  // Bind hoisted failure continuation.
  const Type *fn_type = elab->pool->Arrow(elab->pool->RecordType({}),
                                          ftype);
  const Exp *fn = elab->pool->Fn("",
                                 elab->pool->NewVar("unused"),
                                 fn_type,
                                 fexp);
  const std::string el_cont_var = elab->pool->NewVar("hoist");
  const std::string il_cont_var = elab->pool->NewVar(el_cont_var);
  ElabContext GG = G.Insert(el_cont_var,
                        VarInfo{
                          .tyvars = {},
                          .type = fn_type,
                          .var = il_cont_var});

  // Failure continuation calls the hoisted expression.
  const el::Exp *failure_cont =
    elab->el_pool->App(elab->el_pool->Var(el_cont_var),
                       elab->el_pool->Record({}));

  const auto &[obj_exp, obj_type] = matrix.GetObjIL(elab->pool, GG, x);
  Unification::Unify(Error("sum pattern"), obj_type, mu_type);

  std::vector<std::tuple<std::string, std::string, const Exp *>> arms;
  arms.reserve(sum_cases.size());

  // Update the defaults for the arms: It's the same failure
  // continuation in each case.
  for (auto &[label, el, il, typ, mtx] : sum_cases) {
    mtx.def = failure_cont;
  }

  for (const auto &[label, el_var, il_var, typ, mtx] : sum_cases) {
    // Recursively compile the arm. It has a new variable
    // bound, which is the variable in the sumcase.
    if (VERBOSE) {
      printf("Bind sumcase %s => %s : %s\n",
             el_var.c_str(),
             il_var.c_str(),
             TypeString(typ).c_str());
    }

    ElabContext GGG = GG.Insert(el_var,
                            VarInfo{
                              .tyvars = {},
                              .type = typ,
                              .var = il_var,
                            });

    const auto &[arm_exp, arm_type] = Comp(GGG, mtx);
    Unification::Unify(Error("sum pattern result"), arm_type, ftype);
    arms.emplace_back(label, il_var, arm_exp);
  }

  const Exp *sumcase =
    elab->pool->SumCase(elab->pool->Unroll(obj_exp),
                        std::move(arms),
                        elab->pool->App(
                            elab->pool->Var({}, il_cont_var),
                            elab->pool->Record({})));

  const Exp *ret =
    elab->pool->Let({}, il_cont_var, fn, sumcase);

  return std::make_pair(ret, ftype);
}

// Split the column x, which must begin with a non-empty object pattern.
// Like in other cases, the column can only have OBJECT and WILD
// patterns in it.
std::pair<const Exp *, const Type *>
PatternCompilation::SplitObjectPattern(
    const ElabContext &G,
    Matrix matrix,
    int x) {

  auto Error = [this, &matrix](const std::string &msg) {
      return MatrixError(matrix, msg);
    };

  CHECK(x >= 0 && x < matrix.Width());
  const Pat *first_pat = matrix.Cell(x, 0);
  CHECK(first_pat->type == PatType::OBJECT);
  CHECK(!first_pat->str_children.empty());

  // Object patterns are curious because they are matched incrementally.
  // When we have something like { (Article} title, year }
  // we just match one of the fields (e.g. title) and strip it from
  // all the patterns where it might match.
  //
  // So what we do here is find the field that has the longest prefix
  // (the leading rows that include it), and split on just that.

  // First of all, this implies an obj type.
  Unification::Unify(Error("obj pattern"),
                     matrix.types[x], elab->pool->ObjType());

  auto GetFields = [&](const Pat *p) {
      CHECK(p->type == PatType::OBJECT);
      const ObjVarInfo *ovi = G.FindObj(p->str);
      CHECK(ovi != nullptr) << "Unbound object name " << p->str <<
        " in object pattern: " << PatString(p);

      std::unordered_map<std::string, std::pair<const Pat *, const Type *>>
        fields;
      for (const auto &[lab, pp] : p->str_children) {
        auto it = ovi->fields.find(lab);
        CHECK(it != ovi->fields.end()) << "The object " << p->str <<
          "does not have a field called " << lab << ".\nWhen "
          "compiling object pattern: " << PatString(p);
        fields[lab] = std::make_pair(pp, it->second);
      }
      return fields;
    };

  int prefix_rows = 1;
  // We aren't actually going to use the subpattern in this first
  // pass, but it is nice to just have one GetFields method.
  std::unordered_map<std::string, std::pair<const Pat *, const Type *>>
    fields = GetFields(first_pat);
  CHECK(!fields.empty()) << "Bug: The pattern was non-empty!";

  for (int y = 1; y < matrix.Height(); y++) {
    const Pat *pat = matrix.Cell(x, y);
    if (pat->type == PatType::OBJECT) {
      std::unordered_map<std::string, std::pair<const Pat *, const Type *>>
        row_fields = GetFields(pat);

      // Compute the intersection, by modifying the new fields in place.
      std::vector<std::string> ineligible;
      for (const auto &[lab, info] : row_fields) {
        const auto it = fields.find(lab);
        if (it == fields.end()) {
          // The field isn't in the running set, so delete it.
          ineligible.emplace_back(lab);
        } else {
          // Field is present in both.
          // But if it has a different type, it's a different field.
          const auto &[pp1, tt1] = info;
          const auto &[pp2, tt2] = it->second;
          CHECK(tt1->type != TypeType::EVAR &&
                tt2->type != TypeType::EVAR) << "Bug: This shouldn't happen, "
            "but it would be cleaner if we handled EVars here. (e.g. use "
            "GetObjFieldType.)";
          if (tt1->type != tt2->type) {
            ineligible.push_back(lab);
          }
        }
      }

      for (const std::string &lab : ineligible)
        row_fields.erase(lab);


      if (row_fields.empty()) {
        // Then we have to pick from fields.
        break;

      } else {
        // Continue with this subset.
        fields = std::move(row_fields);
        prefix_rows++;
      }

    } else if (pat->type == PatType::WILD) {
      // We can pick any field in the remaining set.
      break;

    } else {
      LOG(FATAL) << "Bug: The column was not clean!";
    }
  }

  // We can get to the end by finding an empty intersection,
  // a wild pattern, or finishing the column. But we will
  // always have at least one field.
  CHECK(!fields.empty());
  CHECK(prefix_rows > 0);

  // Not sure if there is any useful heuristic to pick between
  // these; like maybe we would prefer one with interesting
  // subpatterns? Or that *doesn't* appear below in the column?
  const std::string split_field = std::get<0>(*fields.begin());
  const Type *split_type = std::get<1>(std::get<1>(*fields.begin()));

  // The pattern must look like this (here x selecting the second
  // column):
  //
  // case (a,  b,                    c) of
  //       p1  {() field, others1 }  r1  => e1
  //       p2  {() field, others2 }  r2  => e2
  //          ...
  //       pn  _       rn  => en
  //       pn1 {() field, others3 } rn1 => en1
  //          ...
  //
  // where the first prefix_rows patterns all have the identified
  // field.

  // We generate:
  //
  // if has b.field
  // then let v = get b.field
  //      in case (a, b.va, c) of
  //               p1 {() others1 } r1 => e1
  //               p2 {() others2 } r2 => e2
  //                   _        => hoisted ()
  //      end
  // else hoisted ()
  //
  // Like the rest of the split functions, hoisted() is pulled
  // out to avoid duplicating code. Here hoisted is
  //
  // case (a,  b,                   c) of
  //       pn  _                    rn  => en
  //       pn1 {() field, others3 } rn1 => en1
  //
  // that is, it's just all the rows after prefix_rows.

  const auto Without = [this](const Pat *p, const std::string &lab) ->
    // extracted subpattern; remaining fields
    std::pair<const Pat *, const Pat *> {
      CHECK(p->type == PatType::OBJECT);
      std::vector<std::pair<std::string, const Pat *>> fields;
      fields.reserve(p->str_children.size());
      std::optional<const Pat *> found;
      for (const auto &[ll, pp] : p->str_children) {
        if (ll == lab) {
          CHECK(!found.has_value()) << "Duplicate label " << ll;
          found = pp;
        } else {
          fields.emplace_back(ll, pp);
        }
      }
      CHECK(found.has_value()) << "Bug: Tried to remove the field " <<
      lab << " from the object pattern " << el::PatString(p) << " but "
      "it wasn't in there.";

      return std::make_pair(
          found.value(),
          elab->el_pool->ObjectPat(p->str, std::move(fields)));
    };

  // Name for the extracted field, which becomes another case object
  // in the success branch. It has type split_type.
  const std::string el_subvar = elab->pool->NewVar(split_field);
  const std::string il_subvar = elab->pool->NewVar(el_subvar);

  // Create the pattern match matrix for the success case (where the
  // label is present).
  Matrix matrix_matched = matrix;
  matrix_matched.DeleteRowsFrom(prefix_rows);

  // We add a new column to handle the subpattern in the matched field
  // for each row.
  const int subpattern_col = matrix_matched.Width();
  matrix_matched.AddColumn(el_subvar, split_type);

  // Now remove the field from the old column, and write the subpattern
  // to the new column.
  CHECK(matrix_matched.Height() == prefix_rows);
  for (int y = 0; y < matrix_matched.Height(); y++) {
    // Remove the field.
    const Pat *p = matrix_matched.Cell(x, y);
    const auto &[subp, restp] = Without(p, split_field);
    matrix_matched.Cell(x, y) = restp;
    matrix_matched.Cell(subpattern_col, y) = subp;
  }

  // The current matrix becomes the failure case (label not present,
  // or subpatterns didn't match).
  matrix.DeleteFirstRows(prefix_rows);

  const auto &[fexp, ftype] = Comp(G, matrix);

  // Bind hoisted failure continuation.
  const Type *fn_type = elab->pool->Arrow(elab->pool->RecordType({}),
                                          ftype);
  const Exp *fn = elab->pool->Fn("",
                                 elab->pool->NewVar("unused"),
                                 fn_type,
                                 fexp);
  const std::string el_cont_var = elab->pool->NewVar("hoist");
  const std::string il_cont_var = elab->pool->NewVar(el_cont_var);
  ElabContext GG = G.Insert(el_cont_var,
                        VarInfo{
                          .tyvars = {},
                          .type = fn_type,
                          .var = il_cont_var});

  // Failure continuation calls the hoisted expression.
  const el::Exp *el_failure_cont =
    elab->el_pool->App(elab->el_pool->Var(el_cont_var),
                       elab->el_pool->Record({}));

  const il::Exp *il_failure_cont =
    elab->pool->App(elab->pool->Var({}, il_cont_var),
                    elab->pool->Record({}));

  matrix_matched.def = el_failure_cont;

  ElabContext GGG = GG.Insert(el_subvar, VarInfo{
          .tyvars = {},
          .type = split_type,
          .var = il_subvar,
        });

  const auto &[sexp, stype] = Comp(GGG, matrix_matched);

  Unification::Unify(Error("object pattern cases"), ftype, stype);

  const auto &[ilobj, ilobjtyp] = matrix.GetObjIL(elab->pool, G, x);
  Unification::Unify(Error("case object (object pattern)"), ilobjtyp,
                     elab->pool->ObjType());

  const std::optional<ObjFieldType> ooft = ILUtil::GetObjFieldType(split_type);
  CHECK(ooft.has_value()) << "The type " << TypeString(split_type) << " is "
    "not allowed as an object field type. Field name: " << split_field << "\n"
    "Object pattern: " << PatString(first_pat);

  ObjFieldType split_oft = ooft.value();

  const Exp *ret =
    elab->pool->If(
        elab->pool->Has(ilobj, split_field, split_oft),
        // Success case.
        elab->pool->Let(
            {}, il_subvar, elab->pool->Get(ilobj, split_field, split_oft),
            sexp),
        // Failure case.
        il_failure_cont);


  ret =
    elab->pool->Let({},
                    il_cont_var, fn,
                    ret);

  return std::make_pair(ret, ftype);
}

// Split the column x, which has at least one AS pattern in it,
// and is otherwise clean.
std::pair<const Exp *, const Type *>
PatternCompilation::SplitAsPattern(
    const ElabContext &G,
    Matrix matrix,
    int x) {
  std::vector<const Pat *> leftcol, rightcol;
  leftcol.reserve(matrix.Height());
  rightcol.reserve(matrix.Height());
  for (int y = 0; y < matrix.Height(); y++) {
    const Pat *cell = matrix.Cell(x, y);
    switch (cell->type) {
    case PatType::AS:
      leftcol.push_back(cell->a);
      rightcol.push_back(cell->b);
      break;
    default:
      leftcol.push_back(cell);
      rightcol.push_back(elab->el_pool->WildPat());
    }
  }

  // PERF: We can swap the left and right column anywhere.
  // It could be helpful to try to match the AS patterns
  // with eachother and with the wildcards, e.g. if we had
  //    case x of
  //       (1 as _) => e1
  //       (_ as 2) => e2
  //       3 => e3
  // it's better to make a wild column and a int column
  // than two heterogeneous columns. But as patterns are
  // rare and this situation is artificial.
  //
  // Probably a better way to implement this would be to
  // have an optimization as part of the general Comp()
  // routine, which finds columns that act on the same
  // object and simplifies them. Then it would not matter
  // what we generate here, and we'd benefit other situations
  // like when the user just writes "case (x, x) of ...".

  const int left_x = x;
  const int right_x = matrix.Width();
  // Use the same object to make a new column.
  matrix.AddColumn(matrix.Obj(x), matrix.Type(x));
  for (int y = 0; y < matrix.Height(); y++) {
    // Now replace the cells. The left column is overwriting
    // the original column that had AS in it; the right column
    // is a new one.
    matrix.Cell(left_x, y) = leftcol[y];
    matrix.Cell(right_x, y) = rightcol[y];
  }

  // Now just recurse on the simpler matrix. Since the objects
  // were already bound, there's no wrapping to do.
  return Comp(G, std::move(matrix));
}


// Split the column x, which must be a record pattern. The column
// must be cleaned, meaning it is just WILD and RECORD patterns.
std::pair<const Exp *, const Type *>
PatternCompilation::SplitRecordPattern(
    const ElabContext &G,
    Matrix matrix,
    int x) {

  auto Error = [this, &matrix](const std::string &msg) {
      return MatrixError(matrix, msg);
    };

  CHECK(x >= 0 && x < matrix.Width());

  // Split it eagerly and recurse.
  // All the record patterns have to have the same set of labels.
  // In the first pass, get those labels. (We could just take the
  // first one since record patterns are always complete, but this
  // approach would let us add ... decorations in the future.)
  std::set<std::string> label_set;
  for (int y = 0; y < matrix.Height(); y++) {
    const Pat *cell = matrix.Cell(x, y);
    switch (cell->type) {
    case PatType::RECORD: {
      std::unordered_set<std::string> labs;
      for (const auto &[lab, pat_] : cell->str_children) {
        CHECK(!labs.contains(lab)) << "Duplicate label " << lab <<
          " in record pattern: " << PatString(cell);
        label_set.insert(lab);
      }
      break;
    }
    case PatType::WILD:
      break;
    default:
      LOG(FATAL) << "Bug: A column of record patterns should only "
        "have WILD and RECORD after cleaning. But got: " <<
        PatString(cell);
    }
  }

  // Now we make a submatrix with labels.size() columns and the
  // same number of rows.
  std::vector<std::string> labels;
  std::vector<std::pair<std::string, const Type *>> record_type;
  std::vector<const Type *> types;
  std::vector<std::string> el_objs;
  for (const std::string &l : label_set) {
    labels.push_back(l);
    types.push_back(elab->NewEVar());
    record_type.emplace_back(labels.back(), types.back());
    std::string v = elab->pool->NewVar(
        elab->pool->BaseVar(matrix.objs[x]) + "_" + l);
    el_objs.emplace_back(v);
  }

  // Old object variable must be this record type.
  Unification::Unify(Error("record pattern column"),
                     matrix.types[x],
                     elab->pool->RecordType(record_type));

  auto GetLabel = [&matrix](const Pat *pat, const std::string &lab) {
      CHECK(pat->type == PatType::RECORD);
      for (const auto &[plab, p] : pat->str_children) {
        if (plab == lab) return p;
      }
      CHECK(false) << "Expected to find label " << lab << " in "
        "record pattern (due to its presence in other patterns from "
        "the same column): " << PatString(pat) << "\nMatrix:\n" <<
        matrix.ToString();
    };

  // We temporarily leave the old column in place so that we can
  // refer to it while populating the new ones. Since these are
  // added at the end, it doesn't disturb existing coordinates.
  for (int xx = 0; xx < (int)labels.size(); xx++) {
    int new_x = matrix.Width();
    matrix.AddColumn(el_objs[xx], types[xx]);
    for (int y = 0; y < matrix.Height(); y++) {
      const Pat *old_cell = matrix.Cell(x, y);
      if (old_cell->type == PatType::WILD) {
        // New column's cells default to WILD, so there's
        // nothing to do.
        CHECK(matrix.Cell(new_x, y)->type == PatType::WILD);
      } else if (old_cell->type == PatType::RECORD) {
        // Get the pattern for this label in the original record
        // pattern.
        const Pat *p = GetLabel(old_cell, labels[xx]);
        matrix.Cell(new_x, y) = p;
      } else {
        LOG(FATAL) << "Bug: We already checked that this column "
          "is all WILD and RECORD patterns.";
      }
    }
  }

  // Save the original variable so we can project from it below.
  const VarInfo *old_vi = G.Find(matrix.objs[x]);
  CHECK(old_vi != nullptr) << "Bug: Case object not found "
    "in pattern compilation (Record pattern): " << matrix.objs[x];
  const il::Exp *original_var = elab->pool->Var({}, old_vi->var);

  // Now we can delete the original column.
  matrix.DeleteColumn(x);

  // Update context with new bindings.
  ElabContext GG = G;
  std::vector<std::string> il_objs;
  for (int xx = 0; xx < (int)el_objs.size(); xx++) {
    const std::string &elv = el_objs[xx];
    const std::string ilv = elab->pool->NewVar(elv);
    il_objs.push_back(ilv);
    if (VERBOSE) {
      printf("Bind %s : %s => %s\n", elv.c_str(),
             TypeString(types[xx]).c_str(),
             ilv.c_str());
    }
    GG = GG.Insert(elv,
                   VarInfo{
                     .tyvars = {},
                     .type = types[xx],
                     .var = ilv
                   });
  }

  // Now everything is set up to translate the new matrix
  // recursively.
  const auto &[exp, type] = Comp(GG, std::move(matrix));

  // Now wrap the result to put the bindings in place.
  // We can do this in any order because the variables are
  // fresh.
  CHECK(il_objs.size() == labels.size());
  const il::Exp *result = exp;
  for (int i = 0; i < (int)il_objs.size(); i++) {
    // let il_var = #lab(old_obj) in result
    result = elab->pool->Let(
        {}, il_objs[i],
        elab->pool->Project(labels[i], original_var),
        result);
  }

  return std::make_pair(result, type);
}

std::pair<const Exp *, const Type *>
PatternCompilation::CompileIrrefutable(
      const ElabContext &G,
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

std::pair<ElabContext, std::vector<PatternCompilation::Dec>>
PatternCompilation::CompileIrrefutableRec(
    const ElabContext &G,
    const el::Pat *pat,
    const il::Exp *rhs,
    const il::Type *rhs_type,
    bool rhs_valuable) {
  el::AstPool *el_pool = elab->el_pool;
  il::AstPool *pool = elab->pool;

  auto Error = [](const std::string &msg) {
      return std::function<std::string()>(
          [msg]() {
            return StringPrintf("Pattern compilation (irrefutable): %s",
                                msg.c_str());
          });
    };

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
          Unification::Unify(Error("pattern type constraint"), at, rhs_type);
          pat = pat->a;
          break;
        }
        case el::PatType::AS:
          // TODO: This can be supported (with both sides are irrefutable),
          // but we would need to change the approach.
          LOG(FATAL) << "Pattern must be irrefutable, but got as: " <<
            PatString(pat);
          break;
        case el::PatType::VAR:
          vars.push_back(pat->str);
          pat = el_pool->WildPat();
          break;
        case el::PatType::WILD:
          return;
        case el::PatType::INT:
          LOG(FATAL) << "Pattern must be irrefutable, but got int: " <<
            PatString(pat);
          return;
        case el::PatType::STRING:
          LOG(FATAL) << "Pattern must be irrefutable, but got string: " <<
            PatString(pat);
          return;
        case el::PatType::BOOL:
          LOG(FATAL) << "Pattern must be irrefutable, but got bool: " <<
            PatString(pat);
          return;
        case el::PatType::OBJECT:
          LOG(FATAL) << "Pattern must be irrefutable, but got object: " <<
            PatString(pat);
          return;
        case el::PatType::APP:
          // TODO: Could allow this for singleton datatypes.
          LOG(FATAL) << "Pattern must be irrefutable, but got constructor "
            "application: " << PatString(pat);
          return;
        case el::PatType::TUPLE: {
          // Transform into the equivalent record.
          std::vector<std::pair<std::string, const Pat *>> lpat;
          lpat.reserve(pat->children.size());
          for (int i = 0; i < (int)pat->children.size(); i++) {
            lpat.emplace_back(StringPrintf("%d", i + 1),
                              pat->children[i]);
            if (VERBOSE) {
              printf("%d = %s\n",
                     i + 1,
                     PatString(pat->children[i]).c_str());
            }
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
    CHECK(pat->type == el::PatType::RECORD);

    std::vector<std::pair<std::string, const il::Type *>> shape;
    shape.reserve(pat->str_children.size());
    for (const auto &[lab, child] : pat->str_children) {
      shape.push_back(std::make_pair(lab, elab->NewEVar()));
    }
    Unification::Unify(Error("record pattern"),
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

    ElabContext GGG = GG;
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
std::pair<ElabContext, std::vector<PatternCompilation::Dec>>
PatternCompilation::GeneralizeOne(
    const ElabContext &G,
    std::vector<std::string> vars,
    const il::Exp *rhs,
    const il::Type *type,
    bool rhs_valuable) {

  il::AstPool *pool = elab->pool;

  // The bound type variables.
  std::vector<std::string> gen_tyvars;
  // Used for instantiating a copy of the variable.
  std::vector<const Type *> gen_tyvar_args;

  if (VERBOSE > 1) {
    printf(AYELLOW("Maybe generalize") " %s %s\n"
           "with type %s\n",
           Util::Join(vars, AGREY(",")).c_str(),
           rhs_valuable ? AGREEN("valuable") : AORANGE("not valuable"),
           TypeString(type).c_str());
  }

  if (rhs_valuable && !vars.empty()) {
    // Then we are attempting to do polymorphic generalization.

    // Get the free evars in the rhs's type.
    std::vector<EVar> free_evars = EVar::FreeEVarsInType(type);
    if (VERBOSE > 1) {
      if (free_evars.empty()) {
        printf("  " AORANGE("no free evars") "\n");
      } else {
        for (const EVar &v : free_evars) {
          printf("  Free: " ABLUE("%s") "\n", v.ToString().c_str());
        }
      }
    }

    // Check each with G.HasEVar. If it's not in the context,
    // then we generalize it.
    std::vector<EVar> gen_evars;
    for (const EVar &v : free_evars) {
      if (G.HasEVar(v)) {
        if (VERBOSE > 1) {
          printf("  " AORANGE("still in context") "\n");
        }
      } else {
        gen_evars.push_back(v);
        if (VERBOSE > 1) {
          printf("  " AGREEN("generalize") "!\n");
        }
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
  ElabContext GG = G.Insert(ov, oinfo);

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


std::function<std::string()> PatternCompilation::MatrixError(
    const Matrix &matrix,
    const std::string &e) {
  // PERF: Copying the matrix here, since (among other things)
  // we sometimes modify it and temporarily put it in an invalid
  // state.
  return [matrix, e]() -> std::string {
      return StringPrintf("Pattern compilation: %s\n"
                          AWHITE("Matrix") ":\n%s\n",
                          e.c_str(),
                          matrix.ShortColorString().c_str());
    };
}

}  // namespace il

