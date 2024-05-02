#ifndef _REPHRASE_BC_H
#define _REPHRASE_BC_H

#include <utility>
#include <variant>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>

#include "primop.h"
#include "bignum/big.h"

// Untyped bytecode.

namespace bc {

struct Value {
  using t = std::variant<
    std::monostate,
    BigInt,
    std::string,
    uint64_t,
    double,
    std::unordered_map<std::string, Value *>,
    std::vector<Value *>
    >;
  t v;
};

namespace inst {

// TODO: Easy to have distinguished symbolic instructions for
// accessing globals by string or by index. Would also be
// great to do that for locals and record fields.

// TODO: Tail calls

// TODO: Consider like "n-op" which takes a vector or
// something like that.
struct Triop {
  Primop primop = Primop::INVALID;
  std::string out, arg1, arg2, arg3;
};

struct Binop {
  Primop primop = Primop::INVALID;
  std::string out, arg1, arg2;
};

struct Unop {
  Primop primop = Primop::INVALID;
  std::string out, arg;
};

struct Call {
  std::string out, f, arg;
};

struct Ret {
  std::string arg;
};

struct If {
  std::string cond;
  int true_idx = 0;
};

struct AllocVec {
  std::string out;
};

struct SetVec {
  std::string vec, idx, arg;
};

struct GetVec {
  std::string out, vec, idx;
};

// make empty map
struct Alloc {
  std::string out;
};

// copy a map
struct Copy {
  std::string out, obj;
};

struct SetLabel {
  // obj[lab] = arg
  std::string obj, lab, arg;
};

struct GetLabel {
  // out = obj[lab]
  std::string out, obj, lab;
};

struct DeleteLabel {
  std::string obj, lab;
};

struct HasLabel {
  // out = has obj.lab
  std::string out, obj, lab;
};

struct Bind {
  // out = arg
  std::string out, arg;
};

// Load from a global.
struct Load {
  std::string out, global;
};

// Save to a new global. This is only used
// during initialization.
struct Save {
  std::string global, arg;
};

struct Jump {
  // Unconditional jump within current function.
  int idx;
};

struct Fail {
  std::string arg;
};

struct Note {
  // No-op for annotations in code.
  std::string msg;
};

// The 'lab' versions are used when in the basic block
// representation.
struct SymbolicIf {
  std::string cond;
  std::string true_lab;
};

struct SymbolicJump {
  std::string lab;
};

}  // namespace inst

using Inst = std::variant<
  std::monostate,
  inst::Triop,
  inst::Binop,
  inst::Unop,
  inst::Call,
  inst::Ret,
  inst::If,
  inst::AllocVec,
  inst::SetVec,
  inst::GetVec,
  inst::Alloc,
  inst::Copy,
  inst::DeleteLabel,
  inst::SetLabel,
  inst::GetLabel,
  inst::HasLabel,
  inst::Bind,
  inst::Load,
  inst::Save,
  inst::Jump,
  inst::Fail,
  inst::Note,

  inst::SymbolicIf,
  inst::SymbolicJump
  >;

struct Block {
  std::vector<Inst> insts;
};

struct SymbolicFn {
  std::string arg;
  // Initial block's label.
  std::string initial;
  std::unordered_map<std::string, Block> blocks;
};

struct SymbolicProgram {
  std::unordered_map<std::string, SymbolicFn> code;
  std::unordered_map<std::string, Value> data;
};

struct Program {
  // For each code label, its argument local and instructions.
  std::unordered_map<std::string,
                     std::pair<std::string, std::vector<Inst>>> code;
  // Constant data. May only be base types; no maps (or vectors? XXX check).
  // Each of these is allocated as a global at startup time;
  // other globals are created explicitly during initialization.
  std::unordered_map<std::string, Value> data;
};

std::string ColorValueString(const Value &value);
std::string ColorInstString(const Inst &inst);

// Dump the entire program with ANSI colors codes.
void PrintProgram(const Program &pgm);
void PrintSymbolicProgram(const SymbolicProgram &pgm);
void PrintBlock(const Block &block);

// Approximate data bytes; total number of instructions.
// Treats names of data and locals as constant.
std::pair<int64_t, int64_t> ProgramSize(const Program &pgm);

// The representation of layout nodes needs to be understood by
// the harness code.
inline constexpr const char *NODE_ATTRS_LABEL = "a";
inline constexpr const char *NODE_CHILDREN_LABEL = "c";

// Similarly the tagging used in field names. These go at the
// beginning of the map key. This is basically the same as the IL
// enum, but we don't want to depend on all of IL. Also, we have
// a U64 type here.
enum class ObjectFieldType {
  STRING,
  FLOAT,
  INT,
  BOOL,
  U64,
  OBJ,
  LAYOUT,
};

char ObjectFieldTypeTag(ObjectFieldType oft);

}  // namespace bc

#endif
