#ifndef _REPHRASE_EXECUTION_H
#define _REPHRASE_EXECUTION_H

#include <variant>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>

#include "bytecode.h"

namespace bc {

struct Execution {
  explicit Execution(const Program &pgm);

  struct StackFrame {
    // Instruction pointer.
    int ip = 0;
    // Everything heap-allocated.
    std::unordered_map<std::string, Value *> locals;
  };

  struct Heap {
    // Owned by the heap.
    std::vector<Value *> used;
  };

  struct State {
    Heap heap;
    std::vector<StackFrame> stack;
  };

  static void GC(State *state);

  // Start the program with a fresh state.
  State Start() const;

  // Take one step in the program.
  static void Step(State *state);

  static bool IsDone(const State &state) {
    return state.stack.empty();
  }

 private:
  const Program &pgm;
};

}  // namespace bc

#endif
