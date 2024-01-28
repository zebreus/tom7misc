#ifndef _REPHRASE_AST_H
#define _REPHRASE_AST_H

#include <string>
#include <cstdint>
#include <vector>

#include "ast-arena.h"
#include "bignum/big.h"

namespace el {

// TODO:
enum class LayoutType {
  TEXT,
  JOIN,
  EXP,
};

enum class ExpType {
  STRING,
  JOIN,
  TUPLE,
  INTEGER,
  VAR,
  LAYOUT,
  LET,
  IF,
  APP,
  ANN,
};

enum class DecType {
  VAL,
  FUN,
  DATATYPE,
};

enum class PatType {
  VAR,
  WILD,
  TUPLE,
  ANN,
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
  std::string str;
  const Layout *layout = nullptr;
  BigInt integer;
  const Exp *a = nullptr;
  const Exp *b = nullptr;
  const Exp *c = nullptr;
  const Type *t = nullptr;
  std::vector<const Dec *> decs;
  std::vector<const Exp *> children;
  Exp(ExpType t) : type(t) {}
};

struct Pat {
  PatType type;
  std::string var;
  const Pat *a;
  const Type *ann;
  std::vector<const Pat *> children;
  Pat(PatType t) : type(t) {}
};

struct Dec {
  DecType type;
  std::string str;
  const Pat *pat = nullptr;
  const Exp *exp = nullptr;
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

  const Exp *String(const std::string &s) {
    Exp *ret = NewExp(ExpType::STRING);
    ret->str = s;
    return ret;
  }

  const Exp *LayoutExp(const Layout *lay) {
    Exp *ret = NewExp(ExpType::LAYOUT);
    ret->layout = lay;
    return ret;
  }

  const Exp *Var(const std::string &v) {
    Exp *ret = NewExp(ExpType::VAR);
    ret->str = v;
    return ret;
  }

  const Exp *Int(int64_t i) {
    Exp *ret = NewExp(ExpType::INTEGER);
    ret->integer = BigInt(i);
    return ret;
  }

  const Exp *Tuple(std::vector<const Exp *> v) {
    Exp *ret = NewExp(ExpType::TUPLE);
    ret->children = std::move(v);
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

  const Exp *AnnExp(const Exp *e, const Type *t) {
    Exp *ret = NewExp(ExpType::ANN);
    ret->a = e;
    ret->t = t;
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

  const Dec *FunDec(const std::string &name,
                    const Pat *args, const Exp *body) {
    Dec *ret = NewDec(DecType::FUN);
    ret->str = name;
    ret->pat = args;
    ret->exp = body;
    return ret;
  }

  // Patterns

  const Pat *VarPat(const std::string &v) {
    Pat *ret = NewPat(PatType::VAR);
    ret->var = v;
    return ret;
  }

  const Pat *AnnPat(const Pat *p, const Type *t) {
    Pat *ret = NewPat(PatType::ANN);
    ret->a = p;
    ret->ann = t;
    return ret;
  }

  const Pat *WildPat() {
    // PERF can be singleton
    return NewPat(PatType::WILD);
  }

  const Pat *TuplePat(std::vector<const Pat *> v) {
    Pat *ret = NewPat(PatType::TUPLE);
    ret->children = std::move(v);
    return ret;
  }

private:
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
};

std::string TypeString(const Type *t);
std::string PatString(const Pat *p);
std::string DecString(const Dec *d);
std::string LayoutString(const Layout *lay);
std::string ExpString(const Exp *e);


// In-order flattening of the layout without any JOIN-type nodes,
// and dropping empty text nodes.
std::vector<const Layout *> FlattenLayout(const Layout *lay);

}  // namespace el

#endif
