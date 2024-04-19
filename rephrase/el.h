#ifndef _REPHRASE_AST_H
#define _REPHRASE_AST_H

#include <cstddef>
#include <string>
#include <cstdint>
#include <utility>
#include <vector>

#include "ast-arena.h"
#include "bignum/big.h"

namespace el {

enum class LayoutType {
  TEXT,
  JOIN,
  EXP,
};

enum class ExpType {
  STRING,
  JOIN,
  TUPLE,
  RECORD,
  OBJECT,
  INT,
  FLOAT,
  BOOL,
  VAR,
  LAYOUT,
  LET,
  IF,
  APP,
  ANN,
  CASE,
  FN,
  ANDALSO,
  ORELSE,
  WITH,
  WITHOUT,
  // Fail with a string error message.
  // Should be replaced with exceptions.
  FAIL,
};

enum class DecType {
  VAL,
  FUN,
  DATATYPE,
  OBJECT,
  OPEN,
  TYPE,
  LOCAL,
};

enum class PatType {
  VAR,
  WILD,
  TUPLE,
  RECORD,
  OBJECT,
  ANN,
  AS,
  INT,
  STRING,
  APP,
  BOOL,
};

enum class TypeType {
  // This includes base types like int.
  // It also includes n child types in application,
  // like for "int list".
  VAR,
  ARROW,
  PRODUCT,
  RECORD,
};

struct Exp;
struct Dec;
struct Pat;

struct Type {
  TypeType type;
  std::string var;
  const Type *a = nullptr;
  const Type *b = nullptr;
  std::vector<const Type *> children;
  std::vector<std::pair<std::string, const Type *>> str_children;
  Type(TypeType t) : type(t) {}
};

struct Layout {
  LayoutType type;
  std::string str;
  std::vector<const Layout *> children;
  const Exp *exp = nullptr;
  Layout(LayoutType t) : type(t) {}
};

struct Exp {
  ExpType type;
  std::string str, str2;
  const Layout *layout = nullptr;
  BigInt integer;
  double d = 0.0;
  const Exp *a = nullptr;
  const Exp *b = nullptr;
  const Exp *c = nullptr;
  const Type *t = nullptr;
  const Pat *pat = nullptr;
  std::vector<std::pair<const Pat *, const Exp *>> clauses;
  std::vector<const Dec *> decs;
  std::vector<const Exp *> children;
  std::vector<std::pair<std::string, const Exp *>> str_children;
  // Approximate position (byte offset in the concatenated source) of
  // this expression in the input stream. For error reporting.
  size_t pos = 0;
  bool boolean = false;
  Exp(ExpType t) : type(t) {}
};

struct Pat {
  PatType type;
  std::string str;
  const Pat *a;
  const Pat *b;
  const Type *ann;
  BigInt integer;
  std::vector<const Pat *> children;
  std::vector<std::pair<std::string, const Pat *>> str_children;
  bool boolean = false;
  Pat(PatType t) : type(t) {}
};

// One arm of a datatype declaration.
struct DatatypeDec {
  // datatype (a) list = Nil | Cons of a * list
  std::string name;
  // Type may be null if absent!
  std::vector<std::pair<std::string, const Type *>> arms;
};

struct FunDec {
  std::string name;
  // Each clause can have many arguments (currying sugar), but they
  // are expected to be rectangular.
  std::vector<std::pair<std::vector<const Pat *>, const Exp *>> clauses;
};

// object Article of { title: string, year : int }
struct ObjectDec {
  std::string name;
  std::vector<std::pair<std::string, const Type *>> fields;
};

struct Dec {
  DecType type;
  std::string str;
  const Pat *pat = nullptr;
  const Exp *exp = nullptr;
  const Type *t = nullptr;
  // Explicit tyvars, used for type and datatype decl.
  std::vector<std::string> tyvars;
  // All arms in the bundle must use the same tyvars.
  std::vector<DatatypeDec> datatypes;
  std::vector<FunDec> funs;
  ObjectDec object;
  // for local decs1 in decs2 end
  std::vector<const Dec *> decs1, decs2;
  Dec(DecType t) : type(t) {}
};

struct AstPool {
  AstPool() = default;

  // Types
  const Type *VarType(const std::string &s,
                      std::vector<const Type *> v) {
    Type *ret = NewType(TypeType::VAR);
    ret->var = s;
    ret->children = std::move(v);
    return ret;
  }

  const Type *Product(std::vector<const Type *> v) {
    Type *ret = NewType(TypeType::PRODUCT);
    ret->children = std::move(v);
    return ret;
  }

  const Type *RecordType(
      std::vector<std::pair<std::string, const Type *>> v) {
    Type *ret = NewType(TypeType::RECORD);
    ret->str_children = std::move(v);
    return ret;
  }

  const Type *Arrow(const Type *dom, const Type *cod) {
    Type *ret = NewType(TypeType::ARROW);
    ret->a = dom;
    ret->b = cod;
    return ret;
  }


  // Expressions

  const Exp *String(const std::string &s, size_t pos) {
    Exp *ret = NewExp(ExpType::STRING);
    ret->str = s;
    ret->pos = pos;
    return ret;
  }

  const Exp *LayoutExp(const Layout *lay) {
    Exp *ret = NewExp(ExpType::LAYOUT);
    ret->layout = lay;
    return ret;
  }

  const Exp *Var(const std::string &v, size_t pos) {
    Exp *ret = NewExp(ExpType::VAR);
    ret->str = v;
    ret->pos = pos;
    return ret;
  }

  const Exp *Bool(bool b) {
    Exp *ret = NewExp(ExpType::BOOL);
    ret->boolean = b;
    return ret;
  }

  const Exp *Int(int64_t i) {
    Exp *ret = NewExp(ExpType::INT);
    ret->integer = BigInt(i);
    return ret;
  }

  const Exp *Int(BigInt i) {
    Exp *ret = NewExp(ExpType::INT);
    ret->integer = std::move(i);
    return ret;
  }

  const Exp *Float(double d) {
    Exp *ret = NewExp(ExpType::FLOAT);
    ret->d = d;
    return ret;
  }

  const Exp *Andalso(const Exp *a, const Exp *b) {
    Exp *ret = NewExp(ExpType::ANDALSO);
    ret->a = a;
    ret->b = b;
    return ret;
  }

  const Exp *Orelse(const Exp *a, const Exp *b) {
    Exp *ret = NewExp(ExpType::ORELSE);
    ret->a = a;
    ret->b = b;
    return ret;
  }

  const Exp *With(const Exp *a, std::string objname, std::string fieldname,
                  const Exp *b) {
    Exp *ret = NewExp(ExpType::WITH);
    ret->a = a;
    ret->str = std::move(objname);
    ret->str2 = std::move(fieldname);
    ret->b = b;
    return ret;
  }

  const Exp *Without(const Exp *a, std::string objname, std::string fieldname) {
    Exp *ret = NewExp(ExpType::WITHOUT);
    ret->a = a;
    ret->str = std::move(objname);
    ret->str2 = std::move(fieldname);
    return ret;
  }

  const Exp *Tuple(std::vector<const Exp *> v) {
    Exp *ret = NewExp(ExpType::TUPLE);
    ret->children = std::move(v);
    return ret;
  }

  const Exp *Record(std::vector<std::pair<std::string, const Exp *>> v) {
    Exp *ret = NewExp(ExpType::RECORD);
    ret->str_children = std::move(v);
    return ret;
  }

  const Exp *Object(std::string objtype,
                    std::vector<std::pair<std::string, const Exp *>> v) {
    Exp *ret = NewExp(ExpType::OBJECT);
    ret->str = std::move(objtype);
    ret->str_children = std::move(v);
    return ret;
  }

  const Exp *Case(const Exp *obj,
                  std::vector<std::pair<const Pat *, const Exp *>> clauses) {
    Exp *ret = NewExp(ExpType::CASE);
    ret->a = obj;
    ret->clauses = std::move(clauses);
    return ret;
  }

  const Exp *Fn(std::string self,
                std::vector<std::pair<const Pat *, const Exp *>> clauses) {
    Exp *ret = NewExp(ExpType::FN);
    ret->str = self;
    ret->clauses = std::move(clauses);
    return ret;
  }

  const Exp *Join(std::vector<const Exp *> v) {
    Exp *ret = NewExp(ExpType::JOIN);
    ret->children = std::move(v);
    return ret;
  }

  const Exp *Let(std::vector<const Dec *> ds, const Exp *e) {
    Exp *ret = NewExp(ExpType::LET);
    ret->decs = std::move(ds);
    ret->a = e;
    return ret;
  }

  const Exp *If(const Exp *cond, const Exp *t, const Exp *f) {
    Exp *ret = NewExp(ExpType::IF);
    ret->a = cond;
    ret->b = t;
    ret->c = f;
    return ret;
  }

  const Exp *App(const Exp *f, const Exp *arg) {
    Exp *ret = NewExp(ExpType::APP);
    ret->a = f;
    ret->b = arg;
    return ret;
  }

  const Exp *Ann(const Exp *e, const Type *t) {
    Exp *ret = NewExp(ExpType::ANN);
    ret->a = e;
    ret->t = t;
    return ret;
  }

  const Exp *Fail(const Exp *e) {
    Exp *ret = NewExp(ExpType::FAIL);
    ret->a = e;
    return ret;
  }

  // Layout

  const Layout *TextLayout(std::string s) {
    Layout *ret = NewLayout(LayoutType::TEXT);
    ret->str = std::move(s);
    return ret;
  }

  const Layout *JoinLayout(std::vector<const Layout *> v) {
    Layout *ret = NewLayout(LayoutType::JOIN);
    ret->children = std::move(v);
    return ret;
  }

  const Layout *ExpLayout(const Exp *e) {
    Layout *ret = NewLayout(LayoutType::EXP);
    ret->exp = e;
    return ret;
  }

  // Declarations

  const Dec *ValDec(const Pat *pat, const Exp *rhs) {
    Dec *ret = NewDec(DecType::VAL);
    ret->pat = pat;
    ret->exp = rhs;
    return ret;
  }

  const Dec *FunDec(std::vector<FunDec> funs) {
    Dec *ret = NewDec(DecType::FUN);
    ret->funs = std::move(funs);
    return ret;
  }

  const Dec *LocalDec(std::vector<const Dec *> ds1,
                      std::vector<const Dec *> ds2) {
    Dec *ret = NewDec(DecType::LOCAL);
    ret->decs1 = std::move(ds1);
    ret->decs2 = std::move(ds2);
    return ret;
  }

  const Dec *DatatypeDec(
      std::vector<std::string> tyvars,
      std::vector<DatatypeDec> datatypes) {
    Dec *ret = NewDec(DecType::DATATYPE);
    ret->tyvars = std::move(tyvars);
    ret->datatypes = std::move(datatypes);
    return ret;
  }

  const Dec *ObjectDec(ObjectDec objdec) {
    Dec *ret = NewDec(DecType::OBJECT);
    ret->object = std::move(objdec);
    return ret;
  }

  const Dec *TypeDec(std::vector<std::string> tyvars,
                     std::string s, const Type *t) {
    Dec *ret = NewDec(DecType::TYPE);
    ret->tyvars = std::move(tyvars);
    ret->str = std::move(s);
    ret->t = t;
    return ret;
  }

  const Dec *OpenDec(const Exp *e) {
    Dec *ret = NewDec(DecType::OPEN);
    ret->exp = e;
    return ret;
  }


  // Patterns

  const Pat *AppPat(std::string s, const Pat *p) {
    Pat *ret = NewPat(PatType::APP);
    ret->str = std::move(s);
    ret->a = p;
    return ret;
  }

  const Pat *StringPat(std::string s) {
    Pat *ret = NewPat(PatType::STRING);
    ret->str = std::move(s);
    return ret;
  }

  const Pat *IntPat(BigInt i) {
    Pat *ret = NewPat(PatType::INT);
    ret->integer = std::move(i);
    return ret;
  }

  const Pat *BoolPat(bool b) {
    Pat *ret = NewPat(PatType::BOOL);
    ret->boolean = b;
    return ret;
  }

  // Unlike in ML, "as" takes patterns on both sides.
  const Pat *AsPat(const Pat *a, const Pat *b) {
    Pat *ret = NewPat(PatType::AS);
    ret->a = a;
    ret->b = b;
    return ret;
  }

  const Pat *VarPat(const std::string &v) {
    Pat *ret = NewPat(PatType::VAR);
    ret->str = v;
    return ret;
  }

  const Pat *AnnPat(const Pat *p, const Type *t) {
    Pat *ret = NewPat(PatType::ANN);
    ret->a = p;
    ret->ann = t;
    return ret;
  }

  const Pat *WildPat() {
    return &wild_pat_;
  }

  const Pat *TuplePat(std::vector<const Pat *> v) {
    Pat *ret = NewPat(PatType::TUPLE);
    ret->children = std::move(v);
    return ret;
  }

  const Pat *RecordPat(std::vector<std::pair<std::string, const Pat *>> v) {
    Pat *ret = NewPat(PatType::RECORD);
    ret->str_children = std::move(v);
    return ret;
  }

  const Pat *ObjectPat(std::string objtype,
                       std::vector<std::pair<std::string, const Pat *>> v) {
    Pat *ret = NewPat(PatType::OBJECT);
    ret->str = std::move(objtype);
    ret->str_children = std::move(v);
    return ret;
  }

  std::string NewInternalVar(const std::string &hint);

 private:
  const Pat wild_pat_ = Pat(PatType::WILD);

  Type *NewType(TypeType t) { return type_arena.New(t); }
  Exp *NewExp(ExpType t) { return exp_arena.New(t); }
  Layout *NewLayout(LayoutType t) { return layout_arena.New(t); }
  Dec *NewDec(DecType t) { return dec_arena.New(t); }
  Pat *NewPat(PatType t) { return pat_arena.New(t); }
  AstArena<Exp> exp_arena;
  AstArena<Layout> layout_arena;
  AstArena<Dec> dec_arena;
  AstArena<Pat> pat_arena;
  AstArena<Type> type_arena;
  int next_internal_var = 0;
};

const char *PatTypeString(PatType pt);
const char *TypeTypeString(TypeType pt);
const char *LayoutTypeString(LayoutType pt);
std::string TypeString(const Type *t);
std::string PatString(const Pat *p);
std::string DecString(const Dec *d);
std::string LayoutString(const Layout *lay);
std::string ExpString(const Exp *e);

// For error messages etc.
std::string ShortColorPatString(const el::Pat *pat);
std::string ShortColorExpString(const el::Exp *exp);

size_t ExpNearbyPos(const el::Exp *exp);

// In-order flattening of the layout without any JOIN-type nodes,
// and dropping empty text nodes.
std::vector<const Layout *> FlattenLayout(const Layout *lay);
// True if the expression syntactically passes the value
// restriction.
bool IsValuable(const Exp *e);
bool IsLayoutValuable(const Layout *lay);

}  // namespace el

#endif
