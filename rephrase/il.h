#ifndef _REPHRASE_IL_H
#define _REPHRASE_IL_H

#include <string>
#include <cstdint>
#include <vector>

#include "ast-arena.h"
#include "bignum/big.h"
#include "unification.h"

namespace il {

enum class ExpType {
  STRING,
  JOIN,
  RECORD,
  INTEGER,
  VAR,
  LAYOUT,
  LET,
  IF,
  APP,
};

enum class DecType {
  VAL,
  FUN,
  DATATYPE,
};

struct Exp;
struct Dec;

enum class TypeType {
  VAR,
  SUM,
  ARROW,
  MU,
  RECORD,
  EVAR,
  // There is no "app"; primitive tycons
  // are to be added here.
  STRING,
  INT,
};

struct Type {
  TypeType type;
  std::string var;
  const Type *a = nullptr;
  const Type *b = nullptr;
  EVar evar;
  std::vector<const Type *> children;
  // For sums and products.
  std::vector<std::pair<std::string, const Type *>> labeled_children;
  Type(TypeType t) : type(t) {}
};

struct Exp {
  ExpType type;
  std::string str;
  BigInt integer;
  const Exp *a = nullptr;
  const Exp *b = nullptr;
  const Exp *c = nullptr;
  std::vector<const Dec *> decs;
  std::vector<const Exp *> children;
  // The order here gives the evaluation order.
  std::vector<std::pair<std::string, const Exp *>> labeled_children;
  Exp(ExpType t) : type(t) {}
};

struct Dec {
  DecType type;
  std::string str;
  const Exp *exp = nullptr;
  Dec(DecType t) : type(t) {}
};

struct AstPool {
  AstPool() = default;

  // Types
  const Type *VarType(const std::string &s,
                      std::vector<const Type *> v = {}) {
    Type *ret = NewType(TypeType::VAR);
    ret->var = s;
    ret->children = std::move(v);
    return ret;
  }

  const Type *RecordType(std::vector<std::pair<std::string, const Type *>> v) {
    Type *ret = NewType(TypeType::RECORD);
    ret->labeled_children = std::move(v);
    return ret;
  }

  const Type *StringType() {
    return NewType(TypeType::STRING);
  }

  const Type *IntType() {
    return NewType(TypeType::INT);
  }

  const Type *EVar(EVar a) {
    Type *ret = NewType(TypeType::EVAR);
    ret->evar = std::move(a);
    return ret;
  }

  // Derived form for Record {1: t1, 2: t2, ...}.
  const Type *Product(const std::vector<const Type *> &v);

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

  const Exp *Int(BigInt i) {
    Exp *ret = NewExp(ExpType::INTEGER);
    ret->integer = std::move(i);
    return ret;
  }

  const Exp *Record(std::vector<std::pair<std::string, const Exp *>> lv) {
    Exp *ret = NewExp(ExpType::RECORD);
    ret->labeled_children = std::move(lv);
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

  // Declarations

  const Dec *ValDec(const std::string &v, const Exp *rhs) {
    Dec *ret = NewDec(DecType::VAL);
    ret->str = v;
    ret->exp = rhs;
    return ret;
  }

private:
  Type *NewType(TypeType t) { return type_arena.New(t); }
  Exp *NewExp(ExpType t) { return exp_arena.New(t); }
  Dec *NewDec(DecType t) { return dec_arena.New(t); }
  AstArena<Exp> exp_arena;
  AstArena<Dec> dec_arena;
  AstArena<Type> type_arena;
};

const char *TypeTypeString(const TypeType t);
std::string TypeString(const Type *t);
std::string DecString(const Dec *d);
std::string ExpString(const Exp *e);

}  // namespace ei

#endif
