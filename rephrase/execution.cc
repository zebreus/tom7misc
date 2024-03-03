
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

static std::string ColorValuePtrString(const Value *value) {
  if (value == nullptr) {
    return AGREY("(null)");
  } else {
    return ColorValueString(*value);
  }
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

  auto TwoFloats = [a, b](const char *what) ->
    std::pair<double, double> {
      const double *ad = std::get_if<double>(&a->v);
      const double *bd = std::get_if<double>(&b->v);
      CHECK(ad != nullptr) << "Expected float argument (lhs) to " << what;
      CHECK(bd != nullptr) << "Expected float argument (rhs) to " << what;
      return std::make_pair(*ad, *bd);
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

  auto Float = [this, state](double d) -> Value * {
      return NewValue(&state->heap, d);
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

  case Primop::INT_DIV_TO_FLOAT: {
    const auto &[aa, bb] = TwoInts("int_mod");
    if (BigInt::Eq(bb, 0)) {
      // We can allow division by zero, returning -inf, nan, or +inf
      LOG(FATAL) << "unimplemented";
    }

    LOG(FATAL) << "unimplemented INT_DIV_TO_FLOAT";
  }

  case Primop::FLOAT_TIMES: {
    const auto &[aa, bb] = TwoFloats("float_times");
    return Float(aa * bb);
  }

  case Primop::FLOAT_PLUS: {
    const auto &[aa, bb] = TwoFloats("float_times");
    return Float(aa + bb);
  }

  case Primop::FLOAT_MINUS: {
    const auto &[aa, bb] = TwoFloats("float_times");
    return Float(aa - bb);
  }

  case Primop::FLOAT_DIV: {
    const auto &[aa, bb] = TwoFloats("float_times");
    return Float(aa / bb);
  }

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

Value *Execution::DoUnop(Primop primop, Value *a, State *state) {

  auto GetInt = [a](const char *what) -> const BigInt & {
      const BigInt *bi = std::get_if<BigInt>(&a->v);
      CHECK(bi != nullptr) << "Expected int argument (lhs) to " << what;
      return *bi;
    };

  auto GetFloat = [a](const char *what) -> double {
      const double *d = std::get_if<double>(&a->v);
      CHECK(d != nullptr) << "Expected float argument (lhs) to " << what;
      return *d;
    };

  auto Float = [this, state](double d) -> Value * {
      return NewValue(&state->heap, d);
    };

  auto Big = [this, state](BigInt b) -> Value * {
      return NewValue(&state->heap, std::move(b));
    };

  switch (primop) {
  case Primop::REF:
    LOG(FATAL) << "unimplemented REF";

  case Primop::GET:
    LOG(FATAL) << "unimplemented GET";

  case Primop::INT_NEG: {
    const BigInt &bi = GetInt("int_neg");
    return Big(BigInt::Negate(bi));
  }

  case Primop::FLOAT_NEG: {
    const double d = GetFloat("int_neg");
    return Float(-d);
  }

  case Primop::INVALID:
    LOG(FATAL) << "Tried executing INVALID primop as unop.";
  default:
    LOG(FATAL) << "Invalid (or non-unop) primop " << PrimopString(primop);
  }
  return NonceValue();
}

void Execution::RunToCompletion(State *state) {
  // TODO: Call GC with some policy.
  while (!IsDone(*state)) {
    Step(state);
  }
}

// Take one step in the program.
void Execution::Step(State *state) {
  CHECK(!state->stack.empty());
  StackFrame &frame = state->stack.back();
  CHECK(frame.ip >= 0 && frame.ip < (int)frame.insts->size()) <<
    "Instruction pointer " << frame.ip << " out of range "
    "(this code block has " << frame.insts->size() << ").";
  const Inst &inst = (*frame.insts)[frame.ip];

  // Hack: We use this in the "message" of an assertion, but it
  // prints the stack trace as an effect.
  int error_ip = frame.ip;
  auto Error = [state, &error_ip]() -> std::string {
      fprintf(stderr, "\n\n" ARED("Error") ":\n");
      if (state->stack.empty()) {
        fprintf(stderr, "(stack is empty!)");
      } else {
        StackFrame &frame = state->stack.back();

        fprintf(stderr, AWHITE("Locals") ":\n");
        for (const auto &[local, value] : frame.locals) {
          fprintf(stderr, " " ABLUE("%s") " = %s\n",
                  local.c_str(), ColorValuePtrString(value).c_str());
        }

        fprintf(stderr, AWHITE("Executing") ":\n");
        for (int i = error_ip - 3; i < error_ip + 3; i++) {
          if (i >= 0 && i < (int)frame.insts->size()) {
            fprintf(stderr, "%s%05d" ANSI_RESET " %s\n",
                    // color for line number
                    (i == error_ip) ? ARED(">") ANSI_WHITE : " " ANSI_GREY,
                    i,
                    ColorInstString((*frame.insts)[i]).c_str());
          }
        }
      }
      return "";
    };

  auto Load = [&Error, &frame](const std::string &local) -> Value * {
      auto it = frame.locals.find(local);
      CHECK(it != frame.locals.end()) << Error() <<
        "Tried to load unset local " << local;
      return it->second;
    };

  auto LoadString = [&Error, &Load](const std::string &local) ->
    const std::string & {
      const Value *a = Load(local);
      const std::string *s = std::get_if<std::string>(&a->v);
      CHECK(s != nullptr) << Error() << "Expected " << local <<
        " to have a string value. Got: " << ColorValuePtrString(a);
      return *s;
    };

  auto LoadRec = [&Error, &Load](const std::string &local) ->
    std::unordered_map<std::string, Value *> * {
      Value *a = Load(local);
      auto *r =
        std::get_if<std::unordered_map<std::string, Value *>>(&a->v);
      CHECK(r != nullptr) << Error() << "Expected " << local <<
      " to have a record value. Got: " << ColorValuePtrString(a);
      return r;
    };

  auto LoadBool = [&Error, &Load](const std::string &local) ->
    bool {
      const Value *a = Load(local);
      const uint64_t *u = std::get_if<uint64_t>(&a->v);
      CHECK(u != nullptr) << Error() << "Expected " << local <<
        " to have a bool (uint64_t) value. Got: " << ColorValuePtrString(a);
      return *u != 0;
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
    frame.locals[unop->out] =
      DoUnop(binop->primop,
             Load(unop->arg),
             state);

  } else if (const inst::Call *call = std::get_if<inst::Call>(&inst)) {
    const std::string &fp = LoadString(call->f);
    const auto it = program.code.find(fp);
    CHECK(it != program.code.end()) << Error() <<
      "Call to unknown function " << fp;
    const auto &[farg, finsts] = it->second;

    // Locals start with just the passed arg.
    std::unordered_map<std::string, Value *> flocals;
    flocals[farg] = Load(call->arg);

    // New frame.
    state->stack.push_back(StackFrame{
        .insts = &finsts,
        .ip = 0,
        .locals = std::move(flocals)
      });

  } else if (const inst::Ret *ret = std::get_if<inst::Ret>(&inst)) {
    // When we make a call, the instruction pointer is already advanced
    // one past the Call instruction, which is what we want. But we
    // need to look one previous to get the name of the local that
    // we are assigning to.
    Value *rv = Load(ret->arg);
    state->stack.pop_back();
    if (state->stack.empty()) {
      // We allow returning from main, but this ends execution.
      // The returned value is discarded.
      return;
    } else {
      StackFrame &parent_frame = state->stack.back();
      error_ip = parent_frame.ip;
      const int call_idx = parent_frame.ip - 1;
      CHECK(call_idx >= 0 && call_idx < (int)parent_frame.insts->size()) <<
        Error() << "Expected to return to one past a CALL instruction.";
      const inst::Call *call = std::get_if<inst::Call>(
          &(*parent_frame.insts)[call_idx]);
      CHECK(call != nullptr) <<
        Error() << "Expected to return to one past a CALL instruction.";
      parent_frame.locals[call->out] = rv;
      // ip already indicates the next instruction.
    }

  } else if (const inst::If *iff = std::get_if<inst::If>(&inst)) {
    const bool cond = LoadBool(iff->cond);
    if (cond) {
      frame.ip = iff->true_idx;
    }

  } else if (const inst::Alloc *alloc = std::get_if<inst::Alloc>(&inst)) {
    frame.locals[alloc->out] =
      NewValue(&state->heap, std::unordered_map<std::string, Value *>());

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
    CHECK(it != rec->end()) << Error() <<
      "Label " << getlabel->lab << " not found in record.";
    frame.locals[getlabel->out] = it->second;

  } else if (const inst::Bind *bind = std::get_if<inst::Bind>(&inst)) {
    frame.locals[bind->out] = Load(bind->arg);

  } else if (const inst::Load *load = std::get_if<inst::Load>(&inst)) {
    const auto &it = program.data.find(load->data_label);
    CHECK(it != program.data.end()) << Error() <<
      "Data label not found: " << load->data_label;
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
    LOG(FATAL) << Error() << "Invalid/Unimplemented instruction!";
  }

}


}  // namespace bc
