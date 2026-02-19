#ifndef _REPHRASE_BC_H
#define _REPHRASE_BC_H

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "bignum/big.h"
#include "primop.h"

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

struct ValueHash {
  inline std::size_t operator()(const Value& obj) const;
};

struct ValueEq {
  inline bool operator()(const Value &a, const Value &b) const;
};

namespace inst {

// TODO: Easy to have distinguished symbolic instructions for
// accessing globals by string or by index. Would also be
// great to do that for locals and record fields.

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

// Same as calling the function and returning its result,
// but uses no additional stack space.
struct TailCall {
  std::string f, arg;
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
  inst::TailCall,
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
// References are also exposed as records with this one field.
// This is used for output arguments for some primops.
inline constexpr const char *REF_LABEL = "r";

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


// Implementations follow.


inline std::size_t ValueHash::operator ()(const Value& obj) const {
  if (const BigInt *bi = std::get_if<BigInt>(&obj.v)) {
    return 0xB10000000 + (size_t)BigInt::HashCode(*bi);
  } else if (const std::string *str = std::get_if<std::string>(&obj.v)) {
    return std::hash<std::string>()(*str);
  } else if (const uint64_t *u = std::get_if<uint64_t>(&obj.v)) {
    return 0x420000000 + std::hash<uint64_t>()(*u);
  } else if (std::holds_alternative<std::monostate>(obj.v)) {
    return 0x123456789;
  } else if (const double *d = std::get_if<double>(&obj.v)) {
    return 0xEE0000000 + std::hash<double>()(*d);
  } else if (const std::unordered_map<std::string, Value *> *m =
             std::get_if<std::unordered_map<std::string, Value *>>(&obj.v)) {
    uint64_t ret = 0x56789123;
    // (Note we are deliberately insensitive to iteration order here,
    // since for one thing std::unordered_map does not guarantee
    // stability.)
    for (const auto &[k, v] : *m) {
      uint64_t a = std::hash<std::string>()(k);
      a = std::rotr<uint64_t>(a, 17);
      uint64_t b = this->operator ()(*v);
      ret += a * b;
    }
    return ret;
  } else if (const std::vector<Value *> *v =
             std::get_if<std::vector<Value *>>(&obj.v)) {
    uint64_t ret = 0x31415926535;
    for (const Value *elt : *v) {
      ret = std::rotr<uint64_t>(ret, 55);
      ret ^= this->operator()(*elt);
      ret *= uint64_t{10481402436096723497u};
    }
    return ret;
  } else {
    LOG(FATAL) << "Unimplemented value type in hash.";
    return 0;
  }
}

inline bool ValueEq::operator ()(const Value &a, const Value &b) const {
  {
    const BigInt *aa = std::get_if<BigInt>(&a.v);
    const BigInt *bb = std::get_if<BigInt>(&b.v);
    if ((aa == nullptr) != (bb == nullptr))
      return false;
    if (aa != nullptr) {
      return BigInt::Eq(*aa, *bb);
    }
  }

  {
    const std::string *aa = std::get_if<std::string>(&a.v);
    const std::string *bb = std::get_if<std::string>(&b.v);
    if ((aa == nullptr) != (bb == nullptr))
      return false;
    if (aa != nullptr) {
      return *aa == *bb;
    }
  }

  {
    const uint64_t *aa = std::get_if<uint64_t>(&a.v);
    const uint64_t *bb = std::get_if<uint64_t>(&b.v);
    if ((aa == nullptr) != (bb == nullptr))
      return false;
    if (aa != nullptr) {
      return *aa == *bb;
    }
  }

  {
    const double *aa = std::get_if<double>(&a.v);
    const double *bb = std::get_if<double>(&b.v);
    if ((aa == nullptr) != (bb == nullptr))
      return false;
    if (aa != nullptr) {
      return *aa == *bb;
    }
  }

  {
    const std::monostate *aa = std::get_if<std::monostate>(&a.v);
    const std::monostate *bb = std::get_if<std::monostate>(&b.v);
    if ((aa == nullptr) != (bb == nullptr))
      return false;
    if (aa != nullptr) {
      return true;
    }
  }

  {
    const std::unordered_map<std::string, Value *> *aa =
      std::get_if<std::unordered_map<std::string, Value *>>(&a.v);
    const std::unordered_map<std::string, Value *> *bb =
      std::get_if<std::unordered_map<std::string, Value *>>(&b.v);
    if ((aa == nullptr) != (bb == nullptr))
      return false;
    if (aa != nullptr) {
      if (aa->size() != bb->size()) return false;
      for (const auto &[ka, va] : *aa) {
        const auto bit = bb->find(ka);
        if (bit == bb->end()) return false;
        if (!this->operator()(*va, *bit->second)) return false;
      }
      return true;
    }
  }

  {
    const std::vector<Value *> *aa =
      std::get_if<std::vector<Value *>>(&a.v);
    const std::vector<Value *> *bb =
      std::get_if<std::vector<Value *>>(&b.v);
    if ((aa == nullptr) != (bb == nullptr))
      return false;
    if (aa != nullptr) {
      if (aa->size() != bb->size()) return false;
      for (int i = 0; i < (int)aa->size(); i++) {
        if (!this->operator()(*(*aa)[i], *(*bb)[i])) return false;
      }
      return true;
    }
  }

  LOG(FATAL) << "Unimplemented case in value equality";
  return false;
}

}  // namespace bc

#endif
