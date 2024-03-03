
#include "execution.h"

#include <variant>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "bytecode.h"
#include "base/logging.h"
#include "ansi.h"

namespace bc {

Execution::Execution(const Program &pgm) : program(pgm) {

}

Execution::~Execution() {}

Value *Execution::NonceValue() {
  return nullptr; // Value{.v = {uint64_t(0xCAFEBABE)}};
}

Execution::State Execution::Start() const {
  const auto m = program.code.find("main");
  CHECK(m != program.code.end()) << "There is no 'main'!";
  const auto &[main_arg, main_insts] = m->second;

  std::unordered_map<std::string, Value *> locals;
  locals[main_arg] = NonceValue();

  State state;
  state.stack.emplace_back(StackFrame{
      .insts = &main_insts,
      .ip = 0,
      // For uniformity, we include the main argument, although
      // there is currently no way to actually access it.
      .locals = std::move(locals),
    });

  return state;
}

void Execution::InternalFail(const std::string &msg, State *state) {
  FailHook(msg);
  // Normally, failing just immediately aborts. But if the fail hook
  // is overridden, then we want to mark the program as stuck so
  // that it doesn't continue.
  state->stack.clear();
}

// Certain hooks are useful for testing.
void Execution::FailHook(const std::string &msg) {
  fprintf(stderr, AWHITE("[") ARED("FAIL") AWHITE("]")
          ": %s\n", msg.c_str());
  LOG(FATAL) << "Program aborted with " << AWHITE("fail") << ".";
}

void Execution::ConsoleHook(const std::string &msg) {
  printf("%s", msg.c_str());
}

Value *Execution::DoBinop(Primop primop, Value *a, Value *b,
                          State *state) {
  auto TwoInts = [a, b](const char *what) ->
    std::pair<const BigInt &, const BigInt &> {
      const BigInt *abi = std::get_if<BigInt>(&a->v);
      const BigInt *bbi = std::get_if<BigInt>(&b->v);
      CHECK(abi != nullptr) << "Expected int argument (lhs) to " << what;
      CHECK(bbi != nullptr) << "Expected int argument (rhs) to " << what;
      return std::tie(*abi, *bbi);
    };

  auto TwoStrings = [a, b](const char *what) ->
    std::pair<const std::string &, const std::string &> {
    const std::string *as = std::get_if<std::string>(&a->v);
    const std::string *bs = std::get_if<std::string>(&b->v);
    CHECK(as != nullptr) << "Expected string argument (lhs) to " << what;
    CHECK(bs != nullptr) << "Expected string argument (rhs) to " << what;
    return std::tie(*as, *bs);
  };

  auto Bool = [this, state](bool x) -> Value * {
      return NewValue(&state->heap, uint64_t(x ? 1 : 0));
    };

  auto Big = [this, state](BigInt b) -> Value * {
      return NewValue(&state->heap, std::move(b));
    };

  switch (primop) {
  case Primop::SET:
    LOG(FATAL) << "unimplemented SET";

  case Primop::INT_EQ: {
    const auto &[aa, bb] = TwoInts("int_eq");
    return Bool(BigInt::Eq(aa, bb));
  }
  case Primop::INT_NEQ: {
    const auto &[aa, bb] = TwoInts("int_neq");
    return Bool(!BigInt::Eq(aa, bb));
  }
  case Primop::INT_LESS: {
    const auto &[aa, bb] = TwoInts("int_less");
    return Bool(BigInt::Less(aa, bb));
  }
  case Primop::INT_LESSEQ: {
    const auto &[aa, bb] = TwoInts("int_lesseq");
    return Bool(BigInt::LessEq(aa, bb));
  }
  case Primop::INT_GREATER: {
    const auto &[aa, bb] = TwoInts("int_greater");
    return Bool(BigInt::Greater(aa, bb));
  }
  case Primop::INT_GREATEREQ: {
    const auto &[aa, bb] = TwoInts("int_greatereq");
    return Bool(BigInt::GreaterEq(aa, bb));
  }

  case Primop::INT_TIMES: {
    const auto &[aa, bb] = TwoInts("int_times");
    return Big(BigInt::Times(aa, bb));
  }

  case Primop::INT_PLUS: {
    const auto &[aa, bb] = TwoInts("int_plus");
    return Big(BigInt::Plus(aa, bb));
  }

  case Primop::INT_MINUS: {
    const auto &[aa, bb] = TwoInts("int_minus");
    return Big(BigInt::Minus(aa, bb));
  }

  case Primop::INT_DIV: {
    const auto &[aa, bb] = TwoInts("int_div");
    if (BigInt::Eq(bb, 0)) {
      InternalFail("division by zero", state);
      return NonceValue();
    }
    return Big(BigInt::Div(aa, bb));
  }

  case Primop::INT_MOD: {
    const auto &[aa, bb] = TwoInts("int_mod");
    if (BigInt::Eq(bb, 0)) {
      InternalFail("division (mod) by zero", state);
      return NonceValue();
    }
    return Big(BigInt::CMod(aa, bb));
  }

  case Primop::INT_DIV_TO_FLOAT:
    LOG(FATAL) << "unimplemented INT_DIV_TO_FLOAT";
  case Primop::FLOAT_TIMES:
  case Primop::FLOAT_PLUS:
  case Primop::FLOAT_MINUS:
  case Primop::FLOAT_DIV:
    LOG(FATAL) << "unimplemented FLOAT ops";

  case Primop::STRING_EQ: {
    const auto &[aa, bb] = TwoStrings("string_eq");
    return Bool(aa == bb);
  }

  case Primop::INVALID:
    LOG(FATAL) << "Tried executing INVALID primop as binop.";
  default:
    LOG(FATAL) << "Invalid (or non-binop) primop " << PrimopString(primop);
  }
  return NonceValue();
}

// Take one step in the program.
void Execution::Step(State *state) {
  CHECK(!state->stack.empty());
  StackFrame &frame = state->stack.back();
  CHECK(frame.ip >= 0 && frame.ip < (int)frame.insts->size()) <<
    "Instruction pointer " << frame.ip << " out of range "
    "(this code block has " << frame.insts->size() << ").";
  const Inst &inst = (*frame.insts)[frame.ip];

  auto Load = [&frame](const std::string &local) -> Value * {
      auto it = frame.locals.find(local);
      CHECK(it != frame.locals.end()) << "Tried to load unset local " <<
        local;
      return it->second;
    };

  auto LoadString = [&Load](const std::string &local) ->
    const std::string & {
      const Value *a = Load(local);
      const std::string *s = std::get_if<std::string>(&a->v);
      CHECK(s != nullptr) << "Expected " << local << " to have a string "
        "value.";
      return *s;
    };

  auto LoadRec = [&Load](const std::string &local) ->
    std::unordered_map<std::string, Value *> * {
      Value *a = Load(local);
      auto *r =
        std::get_if<std::unordered_map<std::string, Value *>>(&a->v);
      CHECK(r != nullptr) << "Expected " << local << " to have a record "
        "value.";
      return r;
    };

  // By default, advance the ip.
  frame.ip++;

  if (const inst::Binop *binop = std::get_if<inst::Binop>(&inst)) {
    frame.locals[binop->out] =
      DoBinop(binop->primop,
              Load(binop->arg1),
              Load(binop->arg2),
              state);
  } else if (const inst::Unop *unop = std::get_if<inst::Unop>(&inst)) {
    LOG(FATAL) << "Unop unimplemented";
  } else if (const inst::Call *call = std::get_if<inst::Call>(&inst)) {
    LOG(FATAL) << "Call unimplemented";
  } else if (const inst::Ret *ret = std::get_if<inst::Ret>(&inst)) {
    LOG(FATAL) << "Ret unimplemented";
  } else if (const inst::If *iff = std::get_if<inst::If>(&inst)) {
    LOG(FATAL) << "If unimplemented";
  } else if (const inst::Alloc *alloc = std::get_if<inst::Alloc>(&inst)) {
    LOG(FATAL) << "Alloc unimplemented";
  } else if (const inst::SetLabel *setlabel =
             std::get_if<inst::SetLabel>(&inst)) {
    std::unordered_map<std::string, Value *> *rec =
      LoadRec(setlabel->obj);
    (*rec)[setlabel->lab] = Load(setlabel->arg);

  } else if (const inst::GetLabel *getlabel =
             std::get_if<inst::GetLabel>(&inst)) {
    const std::unordered_map<std::string, Value *> *rec =
      LoadRec(getlabel->obj);
    auto it = rec->find(getlabel->lab);
    CHECK(it != rec->end()) << "Label " << getlabel->lab << " not found "
      "in record.";
    frame.locals[getlabel->out] = it->second;
  } else if (const inst::Bind *bind = std::get_if<inst::Bind>(&inst)) {
    frame.locals[bind->out] = Load(bind->arg);
  } else if (const inst::Load *load = std::get_if<inst::Load>(&inst)) {
    const auto &it = program.data.find(load->data_label);
    CHECK(it != program.data.end()) << "Data label not found: " <<
      load->data_label;
    // Copying the value to the heap.
    // PERF: We could leave these as globals, but then we need to
    // somehow note them for garbage collection?
    frame.locals[load->out] = NewValue(&state->heap, it->second.v);
  } else if (const inst::Jump *jump = std::get_if<inst::Jump>(&inst)) {
    frame.ip = jump->idx;
  } else if (const inst::Fail *fail = std::get_if<inst::Fail>(&inst)) {
    InternalFail(LoadString(fail->arg), state);
    return;
  } else {
    LOG(FATAL) << "Invalid/Unimplemented instruction!";
  }

}


}  // namespace bc
