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
  virtual ~Execution();

  struct StackFrame {
    // Pointer to the code block in the program.
    const std::vector<Inst> *insts = nullptr;
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

  // Calls "new Value" with the args; stores in heap.
  template<typename... Args>
  Value *NewValue(Heap *heap, Args&&... args);

  // Start the program with a fresh state.
  State Start() const;

  // Take one step in the program.
  void Step(State *state);

  static bool IsDone(const State &state) {
    return state.stack.empty();
  }

  // Certain hooks are useful for testing.
  virtual void FailHook(const std::string &msg);
  virtual void ConsoleHook(const std::string &msg);

 private:
  void InternalFail(const std::string &msg, State *state);
  static Value *NonceValue();
  Value *DoBinop(Primop primop, Value *a, Value *b, State *state);

  const Program &program;
};


// Template implementations follow.

// Calls "new Value" with the args; stores in heap.
template<typename... Args>
Value *Execution::NewValue(Heap *heap, Args&&... args) {
  // PERF could use placement new, since we have a heap and will
  // perform garbage collection. But that's far from the worst
  // performance issue in here!
  Value *v = new Value{.v = std::forward<Args>(args)...};
  heap->used.push_back(v);
  return v;
}


}  // namespace bc

#endif
