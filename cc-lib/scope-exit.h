
#ifndef _CC_LIB_SCOPE_EXIT_H
#define _CC_LIB_SCOPE_EXIT_H

// Like C++'s experimental scope_exit.
//
// ScopeExit exit([&]{ library_free(t); });
#include <utility>

template<class F>
struct ScopeExit {
  ScopeExit(F f) : f(std::move(f)) {}
  ~ScopeExit() { f(); }

 private:
  F f;

  ScopeExit() = delete;
  ScopeExit(const ScopeExit &other) = delete;
  ScopeExit &operator =(const ScopeExit &other) = delete;
};

#endif
