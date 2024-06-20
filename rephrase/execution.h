#ifndef _REPHRASE_EXECUTION_H
#define _REPHRASE_EXECUTION_H

#include <cstdint>
#include <memory>
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

  using map_type = std::unordered_map<std::string, Value *>;
  using vec_type = std::vector<bc::Value *>;

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
    std::unordered_map<std::string, Value *> globals;
    // For diagnostic purposes.
    int64_t collected = 0;
  };

  static void GC(State *state);

  // Calls "new Value" with the args; stores in heap.
  template<typename... Args>
  static Value *NewValue(Heap *heap, Args&&... args);

  // Start the program with a fresh state.
  State Start() const;

  // Take one step in the program.
  void Step(State *state);

  void RunToCompletion(State *state);

  static bool IsDone(const State &state) {
    return state.stack.empty();
  }

  // Certain hooks are useful for testing.
  virtual void FailHook(const std::string &msg);
  virtual void ConsoleHook(const std::string &msg);

  // Get the document. In normal situations we have one of these,
  // but for tests we sometimes provide a fake one.
  virtual Document *DocumentHook();

  virtual Rephrasing *RephrasingHook();

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
  std::tuple<const Execution::map_type &, const Execution::vec_type &>
  GetNode(const char *what, Value *a);

  void InternalFail(const std::string &msg, State *state);
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

// Calls "new Value" with the args; stores in heap.
template<typename... Args>
Value *Execution::NewValue(Heap *heap, Args&&... args) {
  // PERF could use placement new, since we have a heap and will
  // perform garbage collection. But that's far from the worst
  // performance issue in here!
  auto t{std::forward<Args>(args)...};
  Value *v = new Value{.v = std::move(t)};
  heap->used.push_back(v);
  return v;
}

}  // namespace bc

#endif
