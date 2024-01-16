#ifndef _REPHRASE_AST_H
#define _REPHRASE_AST_H

#include <string>
#include <cstdint>
#include <vector>

#include "base/stringprintf.h"

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
};

enum class DecType {
  VAL,
  FUN,
};

enum class PatType {
  VAR,
  WILD,
  TUPLE,
};

struct Exp;
struct Dec;

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
  int64_t integer = 0;
  const Exp *a = nullptr;
  const Exp *b = nullptr;
  std::vector<const Dec *> decs;
  std::vector<const Exp *> children;
  Exp(ExpType t) : type(t) {}
};

struct Pat {
  PatType type;
  std::string var;
  std::vector<const Pat *> children;
  Pat(PatType t) : type(t) {}
};

struct Dec {
  DecType type;
  const Pat *pat = nullptr;
  const Exp *exp = nullptr;
  Dec(DecType t) : type(t) {}
};

template<class T>
struct AstArena {
  AstArena() = default;

  template<typename... Args>
  T *New(Args&& ...args) {
    T *t = new T(std::forward<Args>(args)...);
    storage.push_back(t);
    return t;
  }

  ~AstArena() {
    for (const T *t : storage) delete t;
    storage.clear();
  }

private:
  AstArena(const AstArena &other) = delete;
  void operator=(const AstArena &other) = delete;

  std::vector<const T *> storage;
};

struct AstPool {
  AstPool() = default;

  // Expressions

  const Exp *Str(const std::string &s) {
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
    ret->integer = i;
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

  const Pat *WildPat() {
    // PERF can be singleton
    return NewPat(PatType::WILD);
  }

  const Pat *TuplePat(std::vector<const Pat *> &v) {
    Pat *ret = NewPat(PatType::WILD);
    ret->children = std::move(v);
    return ret;
  }

private:
  Exp *NewExp(ExpType t) { return exp_arena.New(t); }
  Layout *NewLayout(LayoutType t) { return layout_arena.New(t); }
  Dec *NewDec(DecType t) { return dec_arena.New(t); }
  Pat *NewPat(PatType t) { return pat_arena.New(t); }
  AstArena<Exp> exp_arena;
  AstArena<Layout> layout_arena;
  AstArena<Dec> dec_arena;
  AstArena<Pat> pat_arena;
};

std::string LayoutString(const Layout *lay);
std::string ExpString(const Exp *e);

// In-order flattening of the layout without any JOIN-type nodes,
// and dropping empty text nodes.
std::vector<const Layout *> FlattenLayout(const Layout *lay);

#endif
