#ifndef _REPHRASE_BYTECODE_H
#define _REPHRASE_BYTECODE_H

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
    std::unordered_map<std::string, Value *>
    >;
  t v;
};

namespace inst {

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

// make empty map
struct Alloc {
  std::string out;
};

struct SetLabel {
  // obj[lab] = arg
  std::string obj, lab, arg;
};

struct GetLabel {
  // out = obj[lab]
  std::string out, obj, lab;
};

struct Bind {
  // out = arg
  std::string out, arg;
};

struct Load {
  std::string out, data_label;
};

struct Jump {
  // Unconditional jump within current function.
  int idx;
};

struct Fail {
  std::string arg;
};

}  // namespace inst

using Inst = std::variant<
  std::monostate,
  inst::Binop,
  inst::Unop,
  inst::Call,
  inst::Ret,
  inst::If,
  inst::Alloc,
  inst::SetLabel,
  inst::GetLabel,
  inst::Bind,
  inst::Load,
  inst::Jump,
  inst::Fail
  >;


struct Program {
  std::unordered_map<std::string, std::vector<Inst>> code;
  // May only be base types; no maps.
  std::unordered_map<std::string, Value> data;
};

std::string ValueString(const Value &value);
std::string ColorInstString(const Inst &inst);

// Dump the entire program with ANSI colors codes.
void PrintProgram(const Program &pgm);

}  // namespace bc

#endif
