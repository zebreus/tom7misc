#ifndef _REPHRASE_AST_ARENA_H
#define _REPHRASE_AST_ARENA_H

#include <vector>
#include <utility>

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

#endif
