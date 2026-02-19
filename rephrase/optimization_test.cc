#include "optimization.h"

#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bc.h"
#include "primop.h"

namespace bc {

static Inst Bind(const std::string &out, const std::string &arg) {
  return inst::Bind{.out = out, .arg = arg};
}
static Inst Ret(const std::string &arg) {
  return inst::Ret{.arg = arg};
}

static Inst Jmp(const std::string &lab) {
  return inst::SymbolicJump{.lab = lab};
}

static Inst If(const std::string &cond, const std::string &true_lab) {
  return inst::SymbolicIf{.cond = cond, .true_lab = true_lab};
}

#define GET_INST(fn_, lab, idx) [&]() -> const Inst & { \
  const auto &fn = (fn_);                               \
  auto it = fn.blocks.find(lab);                        \
  CHECK(it != fn.blocks.end()) <<                       \
    "Test bug? Missing " << lab;                        \
  const std::vector<Inst> &insts = it->second.insts;    \
  CHECK(idx >= 0 && idx < insts.size()) <<              \
    "Test bug? Bad idx " << lab << "." << idx;          \
  return insts[idx];                                    \
  }()

#define CHECK_DEAD(fn, lab, idx) do {                               \
    const Inst &inst = GET_INST(fn, lab, idx);                      \
    CHECK(std::holds_alternative<inst::Note>(inst)) <<              \
      "Expected instruction " << idx << " to be optimized "         \
      "away (to a 'note') but we have:\n" << ColorInstString(inst); \
  } while (0)

#define CHECK_LIVE(fn, lab, idx) do {                         \
    const Inst &inst = GET_INST(fn, lab, idx);                \
    CHECK(!std::holds_alternative<inst::Note>(inst)) <<       \
      "Expected instruction " << idx << " to be preserved, "  \
      "but we have:\n" << ColorInstString(inst);              \
  } while (0)


static void DeadLocalsBasicOverwrite() {
  // The first assignment is dead (overwritten).
  //   0  x = 1
  //   1  x = 2
  //   2  ret x
  SymbolicProgram pgm;
  pgm.data["one"] = Value{.v = uint64_t(1)};
  pgm.data["two"] = Value{.v = uint64_t(2)};

  Block b;
  b.insts.push_back(Bind("x", "one"));
  b.insts.push_back(Bind("x", "two"));
  b.insts.push_back(Ret("x"));

  SymbolicFn fn;
  fn.initial = "entry";
  fn.blocks["entry"] = b;
  pgm.code["main"] = fn;

  Optimization opt;
  SymbolicProgram out = opt.Optimize(pgm, Optimization::O_DEAD_LOCALS);

  CHECK_DEAD(out.code["main"], "entry", 0);
  CHECK_LIVE(out.code["main"], "entry", 1);
}

static void DeadLocalsBranchDead() {
  // This assignment is dead on both paths.
  //    0 x = 10
  //    1 if cond goto A
  //    2 goto B
  // A: 0 x = 20
  //    1 ret x
  // B: 0 ret 5

  SymbolicProgram pgm;
  pgm.data["10"] = Value{.v = uint64_t(10)};
  pgm.data["20"] = Value{.v = uint64_t(20)};
  pgm.data["5"] = Value{.v = uint64_t(5)};
  pgm.data["cond"] = Value{.v = uint64_t(1)};

  SymbolicFn fn;
  fn.initial = "entry";

  Block entry;
  entry.insts.push_back(Bind("x", "10"));
  entry.insts.push_back(If("cond", "A"));
  entry.insts.push_back(Jmp("B"));
  fn.blocks["entry"] = entry;

  Block blockA;
  blockA.insts.push_back(Bind("x", "20"));
  blockA.insts.push_back(Ret("x"));
  fn.blocks["A"] = blockA;

  Block blockB;
  blockB.insts.push_back(Ret("5"));
  fn.blocks["B"] = blockB;

  pgm.code["main"] = fn;

  Optimization opt;
  SymbolicProgram out = opt.Optimize(pgm, Optimization::O_DEAD_LOCALS);

  CHECK_DEAD(out.code["main"], "entry", 0);
  CHECK_LIVE(out.code["main"], "entry", 1);
  CHECK_LIVE(out.code["main"], "entry", 2);
  CHECK_LIVE(out.code["main"], "A", 0);
  CHECK_LIVE(out.code["main"], "A", 1);
  CHECK_LIVE(out.code["main"], "B", 0);
}

static void DeadLocalsBranchLive() {
  // Assignment used in block A; not dead.
  //    0  x = 10
  //    1  if cond goto A
  //    2  goto B
  // A: 0  ret x
  // B: 0  ret 5

  SymbolicProgram pgm;
  pgm.data["10"] = Value{.v = uint64_t(10)};
  pgm.data["5"] = Value{.v = uint64_t(5)};
  pgm.data["cond"] = Value{.v = uint64_t(1)};

  SymbolicFn fn;
  fn.initial = "entry";

  Block entry;
  entry.insts.push_back(Bind("x", "10"));
  entry.insts.push_back(If("cond", "A"));
  entry.insts.push_back(Jmp("B"));
  fn.blocks["entry"] = entry;

  Block blockA;
  blockA.insts.push_back(Ret("x"));
  fn.blocks["A"] = blockA;

  Block blockB;
  blockB.insts.push_back(Ret("5"));
  fn.blocks["B"] = blockB;

  pgm.code["main"] = fn;

  Optimization opt;
  SymbolicProgram out = opt.Optimize(pgm, Optimization::O_DEAD_LOCALS);

  CHECK_LIVE(out.code["main"], "entry", 0);
}

static void DeadLocalsSaturation() {
  // All of this code is live, but Loop.1 requires correct backward
  // propagation through the loop to see it.
  //       0  x = 0
  //       1  k = 1
  // Loop: 0  x = x + k
  //       1  k = 1
  //       2  if cond goto Loop
  //       3  ret x

  SymbolicProgram pgm;
  pgm.data["0"] = Value{.v = uint64_t(0)};
  pgm.data["1"] = Value{.v = uint64_t(1)};
  pgm.data["10"] = Value{.v = uint64_t(10)};

  SymbolicFn fn;
  fn.initial = "entry";

  Block entry;
  entry.insts.push_back(Bind("x", "0"));
  entry.insts.push_back(Bind("k", "1"));
  entry.insts.push_back(Jmp("loop"));
  fn.blocks["entry"] = entry;

  Block loop;
  loop.insts.push_back(inst::Binop{
      .primop = Primop::INT_PLUS, .out = "x", .arg1 = "x", .arg2 = "k"
    });

  loop.insts.push_back(Bind("k", "1"));
  loop.insts.push_back(If("cond", "loop"));
  loop.insts.push_back(Ret("x"));

  fn.blocks["loop"] = loop;
  pgm.code["main"] = fn;

  Optimization opt;
  SymbolicProgram out = opt.Optimize(pgm, Optimization::O_DEAD_LOCALS);
  CHECK_LIVE(out.code["main"], "entry", 0);
  CHECK_LIVE(out.code["main"], "entry", 1);
  CHECK_LIVE(out.code["main"], "loop", 0);
  CHECK_LIVE(out.code["main"], "loop", 1);
}

} // namespace bc

int main() {
  ANSI::Init();

  bc::DeadLocalsBasicOverwrite();
  bc::DeadLocalsBranchDead();
  bc::DeadLocalsBranchLive();
  bc::DeadLocalsSaturation();

  Print("OK\n");
  return 0;
}
