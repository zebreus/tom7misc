#ifndef _REPHRASE_EXECUTION_H
#define _REPHRASE_EXECUTION_H

#include <cstdint>
#include <memory>
#include <string_view>
#include <tuple>
#include <utility>
#include <unordered_map>
#include <vector>
#include <string>

#include "bc.h"
#include "bignum/big.h"
#include "primop.h"

struct Rephrasing;
struct Document;

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

  // TODO PERF: Revisit freelists. They didn't help with
  // these settings, but it wasn't a disaster, either.
  static constexpr bool USE_FREE_LIST = false;
  static constexpr size_t MAX_FREELIST_ENTRIES = 1 << 22;
  struct FreeList {
    // Owned, already-allocated pointers.
    // The values are in unspecified but valid states.
    std::vector<Value *> reuse;
  };

  struct State {
    Heap heap;
    FreeList free_list;
    std::vector<StackFrame> stack;
    std::unordered_map<std::string, Value *> globals;
    // For diagnostic purposes.
    int64_t collected = 0, freelisted = 0;
  };

  static void GC(State *state);

  // Calls "new Value" with the args; stores in heap.
  template<typename... Args>
  static Value *NewValue(Heap *heap, FreeList *free_list,
                         Args&&... args);

  template<typename... Args>
  static Value *NewValue(State *state, Args&&... args);

  // Start the program with a fresh state.
  State Start() const;

  // Take one step in the program.
  void Step(State *state);

  void RunToCompletion(State *state);

  static bool IsDone(const State &state) {
    return state.stack.empty();
  }

  // Certain hooks are useful for testing.
  virtual void FailHook(std::string_view msg);
  virtual void ConsoleHook(std::string_view msg);

  // Execution implements many primops, some of which need additional
  // resources (like the document itself). TODO: It might be better to
  // encapsulate the following into something like the "Execution
  // environment."
  //
  // Get the document. In normal situations we have one of these,
  // but for tests we sometimes provide a fake one.
  virtual Document *DocumentHook();

  virtual Rephrasing *RephrasingHook();

  // Find a file that exists in the include paths and return its
  // path (or just return the input string).
  virtual std::string FindFileHook(std::string_view name);

  // For output of layout
  virtual void OutputLayoutHook(int page_idx, int frame_idx,
                                const Value *layout);

  virtual void EmitBadnessHook(double badness);

  virtual double OptimizationHook(const std::string &name,
                                  double low,
                                  double start,
                                  double high);

 private:
  static std::pair<Value *, Value *>
  GetNodeParts(const char *what, Value *a);

  const std::string *GetObjStringField(const char *what,
                                       const std::string &field,
                                       const map_type &obj);

  const BigInt *GetObjIntField(const char *what,
                               const std::string &field,
                               const map_type &obj);

  const double *GetObjDoubleField(const char *what,
                                  const std::string &field,
                                  const map_type &obj);

  const Value *GetRequiredObjField(const char *what,
                                   const std::string &field,
                                   bc::ObjectFieldType oft,
                                   const map_type &obj);

  // Get the underlying representations (const).
  std::tuple<const map_type &, const vec_type &>
  GetNode(const char *what, Value *a);

  void InternalFail(std::string_view msg, State *state);
  static Value *NonceValue();
  Value *DoTriop(Primop primop, Value *a, Value *b, Value *c, State *state);
  Value *DoBinop(Primop primop, Value *a, Value *b, State *state);
  Value *DoUnop(Primop primop, Value *a, State *state);

  Value *Bool(bool b, State *state);
  Value *Word(uint64_t w, State *state);
  Value *String(std::string s, State *state);
  Value *Big(BigInt b, State *state);
  Value *Float(double d, State *state);
  Value *Node(Value *attrs, Value *children, State *state);
  Value *Unit(State *state);
  Value *Obj(map_type m, State *state);

  std::tuple<int64_t, int64_t> GetPageAndFrame(const char *what,
                                               const map_type *am);

  const Program &program;
  const std::unique_ptr<Document> degenerate_document;
  const std::unique_ptr<Rephrasing> degenerate_rephrasing;
};


// Template implementations follow.

// PERF: Consider freelists with specific variants?
// e.g. a BigInt has its own allocation which can probably
// often be reused.

// Calls "new Value" with the args; stores in heap.
// May take an allocation from the free_list instead of allocating, if
// the free list is non-null.
template<typename... Args>
Value *Execution::NewValue(Heap *heap, FreeList *free_list, Args&&... args) {
  // PERF could use placement new, since we have a heap and will
  // perform garbage collection. But that's far from the worst
  // performance issue in here!
  auto t{std::forward<Args>(args)...};
  Value *v = nullptr;
  if (USE_FREE_LIST && free_list != nullptr && free_list->reuse.size() > 1) {
    v = free_list->reuse.back();
    free_list->reuse.pop_back();
    v->v = std::move(t);
  } else {
    v = new Value{.v = std::move(t)};
  }
  heap->used.push_back(v);
  return v;
}

template<typename... Args>
Value *Execution::NewValue(State *state, Args&&... args) {
  return NewValue(&state->heap, &state->free_list, std::forward<Args>(args)...);
}

}  // namespace bc

#endif
