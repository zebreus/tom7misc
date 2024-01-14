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
};

struct Exp;

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
  std::vector<const Exp *> children;
  Exp(ExpType t) : type(t) {}
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

private:
  Exp *NewExp(ExpType t) { return exp_arena.New(t); }
  Layout *NewLayout(LayoutType t) { return layout_arena.New(t); }
  AstArena<Exp> exp_arena;
  AstArena<Layout> layout_arena;
};

static std::string LayoutString(const Layout *lay) {
  switch (lay->type) {
    case LayoutType::TEXT:
      return lay->str;
    default:
      return "TODO LAYOUT TYPE";
  }
}

static std::string ExpString(const Exp *e) {
  switch (e->type) {
  case ExpType::STRING:
    // XXX escaping
    return StringPrintf("\"%s\"", e->str.c_str());
  case ExpType::VAR:
    return e->str;
  case ExpType::INTEGER:
    return StringPrintf("%lld", e->integer);
  case ExpType::TUPLE: {
    std::string ret = "(";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      ret += ExpString(e->children[i]);
    }
    ret += ")";
    return ret;
  }
  case ExpType::JOIN: {
    std::string ret = "[";
    for (int i = 0; i < (int)e->children.size(); i++) {
      if (i != 0) StringAppendF(&ret, ", ");
      ret += ExpString(e->children[i]);
    }
    ret += "]";
    return ret;
  }
  case ExpType::LAYOUT:
    return StringPrintf("[%s]", LayoutString(e->layout).c_str());
  default:
    return "ILLEGAL EXPRESSION";
  }
}

#endif
