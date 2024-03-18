
#include "execution.h"

#include <variant>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "bytecode.h"
#include "base/logging.h"
#include "ansi.h"
#include "document.h"
#include "util.h"

namespace bc {

struct DegenerateDocument : public Document {

};

Execution::Execution(const Program &pgm) :
  program(pgm),
  degenerate_document(new DegenerateDocument) {

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

  // Copy everything from the constant data into globals, allocating
  // it in the heap.
  for (const auto &[lab, value] : program.data) {
    state.globals[lab] = NewValue(&state.heap, Value::t(value.v));
  }

  return state;
}

void Execution::InternalFail(const std::string &msg, State *state) {
  FailHook(msg);
  // Normally, failing just immediately aborts. But if the fail hook
  // is overridden, then we want to mark the program as stuck so
  // that it doesn't continue.
  state->stack.clear();
}

Value *Execution::RephraseHook(Value *layout) {
  return layout;
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

void Execution::OutputLayoutHook(int page_idx, const Value *doc) {
  printf("(output layout ignored)\n");
}

Document *Execution::DocumentHook() {
  return degenerate_document.get();
}

static std::string ColorValuePtrString(const Value *value) {
  if (value == nullptr) {
    return AGREY("(null)");
  } else {
    return ColorValueString(*value);
  }
}

static int64_t GetInt64(const char *what, const BigInt &x) {
  const std::optional<int64_t> xo = x.ToInt();
  CHECK(xo.has_value()) << "In " << what << ", integer needs to fit "
    "into 64 bits! Got: " << x.ToString();
  return xo.value();
}

Value *Execution::Bool(bool x, State *state) {
  return NewValue(&state->heap, uint64_t(x ? 1 : 0));
}

Value *Execution::String(std::string s, State *state) {
  return NewValue(&state->heap, std::move(s));
}

Value *Execution::Float(double d, State *state) {
  return NewValue(&state->heap, d);
}

Value *Execution::DoTriop(Primop primop, Value *a, Value *b, Value *c,
                          State *state) {
  switch (primop) {
  case Primop::STRING_SUBSTR: {
    const std::string *as = std::get_if<std::string>(&a->v);
    const BigInt *bi = std::get_if<BigInt>(&b->v);
    const BigInt *ci = std::get_if<BigInt>(&c->v);
    CHECK(as != nullptr && bi != nullptr && ci != nullptr) <<
      "Expected string,int,int to string-substr";
    const int64_t bb = GetInt64("substr start", *bi);
    const int64_t cc = GetInt64("substr length", *ci);
    CHECK(bb >= 0 && cc >= 0 &&
          bb <= (int64_t)as->size() &&
          bb + cc <= (int64_t)as->size())
      << "In string-substr, out of range start/length: "
      << bb << ", " << cc;

    std::string s = as->substr(bb, cc);
    return String(s, state);
  }

  case Primop::STRING_REPLACE: {
    const std::string *as = std::get_if<std::string>(&a->v);
    const std::string *bs = std::get_if<std::string>(&b->v);
    const std::string *cs = std::get_if<std::string>(&c->v);

    CHECK(as != nullptr && bs != nullptr && cs != nullptr) <<
      "Expected string,string,string to string-replace";

    return String(Util::Replace(*as, *bs, *cs), state);
  }

  case Primop::REGISTER_FONT: {
    const std::string *as = std::get_if<std::string>(&a->v);
    const std::string *bs = std::get_if<std::string>(&b->v);
    const BigInt *ci = std::get_if<BigInt>(&c->v);
    CHECK(as != nullptr && bs != nullptr && ci != nullptr) <<
      "Expected string,string,int to register-font";

    const Font *font = DocumentHook()->GetFontByName(*as);

    Document::TextProps props;
    props.font_family = *bs;
    // unused
    props.font_size = 1.0;
    props.font_bold = !!BigInt::BitwiseAnd(*ci, 1);
    props.font_italic = !!BigInt::BitwiseAnd(*ci, 2);
    DocumentHook()->RegisterFont(props, font);
    return Unit(state);
  }

  default:
    LOG(FATAL) << "Unimplemented or non-triop primop: "
               << PrimopString(primop);
  }

  return NonceValue();
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

  auto Big = [state](BigInt b) -> Value * {
      return NewValue(&state->heap, std::move(b));
    };

  switch (primop) {
  case Primop::INT_EQ: {
    const auto &[aa, bb] = TwoInts("int_eq");
    return Bool(BigInt::Eq(aa, bb), state);
  }

  case Primop::INT_NEQ: {
    const auto &[aa, bb] = TwoInts("int_neq");
    return Bool(!BigInt::Eq(aa, bb), state);
  }

  case Primop::INT_LESS: {
    const auto &[aa, bb] = TwoInts("int_less");
    return Bool(BigInt::Less(aa, bb), state);
  }

  case Primop::INT_LESSEQ: {
    const auto &[aa, bb] = TwoInts("int_lesseq");
    return Bool(BigInt::LessEq(aa, bb), state);
  }

  case Primop::INT_GREATER: {
    const auto &[aa, bb] = TwoInts("int_greater");
    return Bool(BigInt::Greater(aa, bb), state);
  }

  case Primop::INT_GREATEREQ: {
    const auto &[aa, bb] = TwoInts("int_greatereq");
    return Bool(BigInt::GreaterEq(aa, bb), state);
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

    // We allow division by zero, returning -inf, nan, or +inf.

    // TODO: Can improve precision here with BigRat. This works
    // fine unless the numbers are very large, though.
    double numer = aa.ToDouble();
    double denom = bb.ToDouble();
    return Float(numer / denom, state);
  }

  case Primop::FLOAT_TIMES: {
    const auto &[aa, bb] = TwoFloats("float_times");
    return Float(aa * bb, state);
  }

  case Primop::FLOAT_PLUS: {
    const auto &[aa, bb] = TwoFloats("float_times");
    return Float(aa + bb, state);
  }

  case Primop::FLOAT_MINUS: {
    const auto &[aa, bb] = TwoFloats("float_times");
    return Float(aa - bb, state);
  }

  case Primop::FLOAT_DIV: {
    const auto &[aa, bb] = TwoFloats("float_times");
    return Float(aa / bb, state);
  }

  case Primop::FLOAT_EQ: {
    const auto &[aa, bb] = TwoFloats("float_eq");
    return Bool(aa == bb, state);
  }

  case Primop::FLOAT_NEQ: {
    const auto &[aa, bb] = TwoFloats("float_neq");
    return Bool(aa != bb, state);
  }

  case Primop::FLOAT_LESS: {
    const auto &[aa, bb] = TwoFloats("float_less");
    return Bool(aa < bb, state);
  }

  case Primop::FLOAT_LESSEQ: {
    const auto &[aa, bb] = TwoFloats("float_lesseq");
    return Bool(aa <= bb, state);
  }

  case Primop::FLOAT_GREATER: {
    const auto &[aa, bb] = TwoFloats("float_greater");
    return Bool(aa > bb, state);
  }

  case Primop::FLOAT_GREATEREQ: {
    const auto &[aa, bb] = TwoFloats("float_greatereq");
    return Bool(aa >= bb, state);
  }

  case Primop::STRING_EQ: {
    const auto &[aa, bb] = TwoStrings("string_eq");
    return Bool(aa == bb, state);
  }

  case Primop::STRING_CONCAT: {
    const auto &[aa, bb] = TwoStrings("string_concat");
    return String(aa + bb, state);
  }

  case Primop::STRING_FIND: {
    const auto &[aa, bb] = TwoStrings("string_concat");
    const auto pos = aa.find(bb);
    if (pos == std::string::npos) {
      return Big(BigInt(-1));
    } else {
      return Big(BigInt(pos));
    }
  }

  case Primop::OUT_LAYOUT: {
    const BigInt *ai = std::get_if<BigInt>(&a->v);
    CHECK(ai != nullptr) << "out-layout expects an integer as its first "
      "argument.";
    std::optional<int64_t> io = ai->ToInt();
    CHECK(io.has_value()) << "Index is way too big! " << ai->ToString();
    const int64_t page_idx = io.value();
    CHECK(page_idx >= 0 && page_idx < 1'000'000'000LL) << "Page index "
      "is nonsensical!";
    OutputLayoutHook((int)page_idx, b);
    return Unit(state);
  }

  case Primop::PACK_BOXES: {
    const double *ad = std::get_if<double>(&a->v);
    CHECK(ad != nullptr) << "Expected double argument (lhs) to pack_boxes";
    DocTree doc = ValueToDocTree(b);
    DocTree packdoc = DocumentHook()->PackBoxes(*ad, doc);
    return DocTreeToValue(&state->heap.used, packdoc);
  }

    // PERF: Could compile this away to GetVec.
  case Primop::LAYOUT_VEC_SUB: {
    const BigInt *bb = std::get_if<BigInt>(&b->v);
    CHECK(bb != nullptr) << "Expected int argument (rhs) to layout-vec-sub";
    const auto &[attrs, children] = GetNode("layout-vec-sub", a);
    std::optional<int64_t> io = bb->ToInt();
    CHECK(io.has_value()) << "Index is way too big! " << bb->ToString();
    const int64_t idx = io.value();
    CHECK(idx >= 0 && idx < (int64_t)children.size()) <<
      "Index out of bounds in layout-vec-sub.\nIndex: " <<
      idx << "\nVector size: " << children.size();
    return children[idx];
  }

  case Primop::SET_ATTRS: {
    const map_type *aa = std::get_if<map_type>(&a->v);
    CHECK(aa != nullptr) << "Expected obj first argument to set-attrs.";
    const auto &[attrs, children] = GetNodeParts("set-attrs", b);
    return Node(a, children, state);
  }

  case Primop::REF_SET:
    LOG(FATAL) << "SET should have been compiled away.";
  case Primop::INVALID:
    LOG(FATAL) << "Tried executing INVALID primop as binop.";
  default:
    LOG(FATAL) << "Invalid (or non-binop) primop " << PrimopString(primop);
  }
  return NonceValue();
}

// attrs, children vector
std::pair<Value *, Value *>
Execution::GetNodeParts(const char *what, Value *a) {
  map_type *obj = std::get_if<map_type>(&a->v);
  CHECK(obj != nullptr) << what << " on a (presumably) text node. "
      "Must check it first with is-text.";

  auto ait = obj->find(bc::NODE_ATTRS_LABEL);
  CHECK(ait != obj->end()) << "(" << what << ") Nodes always have an "
      "attribute object, even if it is empty.";
  Value *attrs = ait->second;

  // Debugging
  const map_type *aobj = std::get_if<map_type>(&attrs->v);
  CHECK(aobj != nullptr) << "(" << what << ") In a node, the attribute "
      "field is always an object, even if it is empty.";

  auto cit = obj->find(bc::NODE_CHILDREN_LABEL);
  CHECK(cit != obj->end()) << "(" << what << ") Nodes always have a "
      "children vector, even if it is empty.";
  Value *children = cit->second;
  return std::make_pair(attrs, children);
}

// Returns the attrs (as a Value *) and the children (as a vector
std::pair<const Execution::map_type &, const Execution::vec_type &>
Execution::GetNode(const char *what, Value *a) {
  const auto &[attrs, children] = GetNodeParts(what, a);

  map_type *aobj = std::get_if<map_type>(&attrs->v);
  CHECK(aobj != nullptr) << "(" << what << ") In a node, the attrs "
      "field is always an object.";

  vec_type *cvec = std::get_if<vec_type>(&children->v);
  CHECK(cvec != nullptr) << "(" << what << ") In a node, the children "
      "field is always a vector.";
  return std::tie(*aobj, *cvec);
};

// Make a (non-text) node.
Value *Execution::Node(Value *attrs, Value *children, State *state) {
  map_type layout = {
    {bc::NODE_ATTRS_LABEL, attrs},
    {bc::NODE_CHILDREN_LABEL, children}
  };

  return NewValue(&state->heap, std::move(layout));
}

Value *Execution::Unit(State *state) {
  // PERF: Don't represent unit with an allocated record :(
  return NewValue(&state->heap,
                  std::unordered_map<std::string, Value *>());
}

Value *Execution::DoUnop(Primop primop, Value *a, State *state) {

  auto GetInt = [a](const char *what) -> const BigInt & {
      const BigInt *bi = std::get_if<BigInt>(&a->v);
      CHECK(bi != nullptr) << "Expected int argument to " << what;
      return *bi;
    };

  auto GetFloat = [a](const char *what) -> double {
      const double *d = std::get_if<double>(&a->v);
      CHECK(d != nullptr) << "Expected float argument to " << what;
      return *d;
    };

  auto GetString = [a](const char *what) -> const std::string & {
      const std::string *s = std::get_if<std::string>(&a->v);
      CHECK(s != nullptr) << "Expected string argument to " << what;
      return *s;
  };

  auto Big = [state](BigInt b) -> Value * {
      return NewValue(&state->heap, std::move(b));
    };

  switch (primop) {
  case Primop::INT_NEG: {
    const BigInt &bi = GetInt("int_neg");
    return Big(BigInt::Negate(bi));
  }

  case Primop::FLOAT_NEG: {
    const double d = GetFloat("float_neg");
    return Float(-d, state);
  }

  case Primop::INT_TO_STRING: {
    const BigInt &bi = GetInt("int_to_string");
    return String(bi.ToString(), state);
  }

  case Primop::INT_TO_FLOAT: {
    const BigInt &bi = GetInt("int_to_float");
    return Float(bi.ToDouble(), state);
  }

  case Primop::OUT_STRING: {
    const std::string &s = GetString("out_string");
    ConsoleHook(s);
    return Unit(state);
  }

  case Primop::OBJ_EMPTY: {
    const map_type *obj = std::get_if<map_type>(&a->v);
    CHECK(obj != nullptr) << "obj_empty on a non-object.";
    return Bool(obj->empty(), state);
  }

  case Primop::STRING_EMPTY: {
    const std::string &s = GetString("string-empty");
    return Bool(s.empty(), state);
  }

  case Primop::STRING_SIZE: {
    const std::string &s = GetString("string-size");
    return Big(BigInt(s.size()));
  }

  case Primop::REPHRASE: {
    return RephraseHook(a);
  }

  case Primop::GET_BOXES: {
    DocTree doc = ValueToDocTree(a);
    // DebugPrintDocTree(doc);
    DocTree boxdoc = DocumentHook()->GetBoxes(doc);
    return DocTreeToValue(&state->heap.used, boxdoc);
  }

  case Primop::IS_TEXT: {
    const bool is_text = std::holds_alternative<std::string>(a->v);
    return Bool(is_text, state);
  }

  case Primop::GET_TEXT: {
    const std::string *text = std::get_if<std::string>(&a->v);
    CHECK(text != nullptr) << "get-text on non-text node. Must check it "
      "first with is-text.";
    return String(*text, state);
  }

  case Primop::GET_ATTRS: {
    const auto [attrs, children] = GetNodeParts("get-attrs", a);
    return attrs;
  }

  case Primop::LAYOUT_VEC_SIZE: {
    const auto &[attrs, children] = GetNode("layout-vec-size", a);
    return Big(BigInt((int)children.size()));
  }

  case Primop::DEBUG_PRINT_DOC: {
    DocTree doc = ValueToDocTree(a);
    DebugPrintDocTree(doc);
    return Unit(state);
  }

  case Primop::LOAD_FONT_FILE: {
    const std::string filename = GetString("load-font");
    std::string f = DocumentHook()->LoadFontFile(filename);
    return String(std::move(f), state);
  }

  case Primop::REF:
    LOG(FATAL) << "REF should have been compiled away";
  case Primop::REF_GET:
    LOG(FATAL) << "GET should have been compiled away";
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
  auto Error = [this, state, &error_ip]() -> std::string {
      fprintf(stderr, "\n\n" ARED("Error") ":\n");

      if (state->stack.empty()) {
        fprintf(stderr, "(stack is empty!)");
      } else {
        StackFrame &frame = state->stack.back();

        // Recover the name of the function.
        std::string fn = "(!unknown!)";
        for (const auto &[name, arg_insts] : program.code) {
          const auto &[arg, insts] = arg_insts;
          if (frame.insts == &insts) {
            fn = name;
            break;
          }
        }

        fprintf(stderr, "In code " AYELLOW("%s") "\n", fn.c_str());

        fprintf(stderr, AWHITE("Globals") ":\n");
        for (const auto &[global, value] : state->globals) {
          #define AGLOBAL_LAB(s) AFGCOLOR(200, 160, 40, s)
          fprintf(stderr, " " AGLOBAL_LAB("%s") " = %s\n",
                  global.c_str(), ColorValuePtrString(value).c_str());
        }

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

  auto LoadVec = [&Error, &Load](const std::string &local) ->
    std::vector<Value *> * {
      Value *a = Load(local);
      auto *r =
        std::get_if<std::vector<Value *>>(&a->v);
      CHECK(r != nullptr) << Error() << "Expected " << local <<
      " to have a vector value. Got: " << ColorValuePtrString(a);
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

#if 0
  auto LoadU64 = [&Error, &Load](const std::string &local) -> uint64_t {
      const Value *a = Load(local);
      const uint64_t *u = std::get_if<uint64_t>(&a->v);
      CHECK(u != nullptr) << Error() << "Expected " << local <<
        " to have a uint64 value. Got: " << ColorValuePtrString(a);
      return *u;
    };
#endif

  auto LoadInt = [&Error, &Load](const std::string &local) -> const BigInt & {
      const Value *a = Load(local);
      const BigInt *b = std::get_if<BigInt>(&a->v);
      CHECK(b != nullptr) << Error() << "Expected " << local <<
        " to have a int value. Got: " << ColorValuePtrString(a);
      return *b;
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
      DoUnop(unop->primop,
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

  } else if (const inst::AllocVec *allocvec =
             std::get_if<inst::AllocVec>(&inst)) {
    frame.locals[allocvec->out] =
      NewValue(&state->heap, std::vector<Value *>());

  } else if (const inst::SetVec *setvec =
             std::get_if<inst::SetVec>(&inst)) {
    std::vector<Value *> *vec = LoadVec(setvec->vec);
    const BigInt &bidx = LoadInt(setvec->idx);

    std::optional<int64_t> io = bidx.ToInt();
    CHECK(io.has_value()) << "Index is way too big! " << bidx.ToString();
    const int64_t idx = io.value();
    CHECK(idx >= 0) << "SetVec Index cannot be less than zero: " << idx;
    while ((int64_t)vec->size() <= idx) vec->push_back(nullptr);
    (*vec)[idx] = Load(setvec->arg);

  } else if (const inst::GetVec *getvec =
             std::get_if<inst::GetVec>(&inst)) {
    std::vector<Value *> *vec = LoadVec(getvec->vec);
    const BigInt &bidx = LoadInt(getvec->idx);

    std::optional<int64_t> io = bidx.ToInt();
    CHECK(io.has_value()) << "Index is way too big! " << bidx.ToString();
    const int64_t idx = io.value();
    CHECK(idx >= 0) << "GetVec Index cannot be less than zero: " << idx;
    while ((int64_t)vec->size() <= idx) vec->push_back(nullptr);
    frame.locals[getvec->out] = (*vec)[idx];

  } else if (const inst::Copy *copy = std::get_if<inst::Copy>(&inst)) {
    std::unordered_map<std::string, Value *> *rec = LoadRec(copy->obj);
    frame.locals[copy->out] = NewValue(&state->heap, *rec);

  } else if (const inst::DeleteLabel *deletelabel =
             std::get_if<inst::DeleteLabel>(&inst)) {
    std::unordered_map<std::string, Value *> *rec = LoadRec(deletelabel->obj);
    // It is allowed to delete a label that is not present.
    (void)rec->erase(deletelabel->lab);

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

  } else if (const inst::HasLabel *haslabel =
             std::get_if<inst::HasLabel>(&inst)) {
    const std::unordered_map<std::string, Value *> *rec =
      LoadRec(haslabel->obj);
    const bool has = rec->contains(haslabel->lab);
    const uint64_t u = has ? 1 : 0;
    frame.locals[haslabel->out] = NewValue(&state->heap, Value::t(u));

  } else if (const inst::Bind *bind = std::get_if<inst::Bind>(&inst)) {
    frame.locals[bind->out] = Load(bind->arg);

  } else if (const inst::Save *save = std::get_if<inst::Save>(&inst)) {
    CHECK(!state->globals.contains(save->global)) << "Saving over "
      "an existing global. There's no reason we can't do this, but it "
      "is unexpected (initialization code ran twice?). Global: " <<
      save->global;
    state->globals[save->global] = Load(save->arg);

  } else if (const inst::Load *load = std::get_if<inst::Load>(&inst)) {
    const auto &it = state->globals.find(load->global);
    CHECK(it != state->globals.end()) << Error() <<
      "Global not found: " << load->global;

    frame.locals[load->out] = it->second;

  } else if (const inst::Jump *jump = std::get_if<inst::Jump>(&inst)) {
    frame.ip = jump->idx;

  } else if (const inst::Fail *fail = std::get_if<inst::Fail>(&inst)) {
    InternalFail(LoadString(fail->arg), state);
    return;

  } else if (const inst::Note *note = std::get_if<inst::Note>(&inst)) {
    // no-op. But we could print the message in super-verbose modes?
    return;

  } else if (const inst::Triop *triop = std::get_if<inst::Triop>(&inst)) {
    frame.locals[triop->out] =
      DoTriop(triop->primop,
              Load(triop->arg1),
              Load(triop->arg2),
              Load(triop->arg3),
              state);

  } else {
    LOG(FATAL) << Error() << "Invalid/Unimplemented instruction!";
  }

}


}  // namespace bc
