
#include "execution.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <unordered_set>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "achievements.h"
#include "animation.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bc.h"
#include "bignum/big.h"
#include "boxes-and-glue.h"
#include "document.h"
#include "image.h"
#include "primop.h"
#include "rephrasing.h"
#include "timer.h"
#include "utf.h"
#include "utf8.h"
#include "util.h"

namespace bc {

struct DegenerateDocument : public Document {
  DegenerateDocument() : Document(".") {}
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

// Certain hooks are useful for testing.
void Execution::FailHook(const std::string &msg) {
  fprintf(stderr, AWHITE("[") ARED("FAIL") AWHITE("]")
          ": %s\n", msg.c_str());
  LOG(FATAL) << "Program aborted with " << AWHITE("fail") << ".";
}

void Execution::ConsoleHook(const std::string &msg) {
  printf("%s", msg.c_str());
}

void Execution::OutputLayoutHook(int page_idx, int frame_idx,
                                 const Value *doc) {
  printf("(output layout ignored)\n");
}

void Execution::EmitBadnessHook(double badness) {
  printf("Badness " ARED("%.5f") "\n", badness);
}

double Execution::OptimizationHook(const std::string &name,
                                   double low,
                                   double start,
                                   double high) {
  printf("(Base execution does not optimize.)\n");
  return start;
}

Document *Execution::DocumentHook() {
  return degenerate_document.get();
}

Rephrasing *Execution::RephrasingHook() {
  LOG(FATAL) << "No rephrasing support in base Execution.";
  return nullptr;
}

std::string Execution::FindFileHook(std::string_view name) {
  // No include paths in base Execution.
  return std::string(name);
}

static std::string ColorValuePtrString(const Value *value) {
  if (value == nullptr) {
    return AGREY("(null)");
  } else {
    return ColorValueString(*value);
  }
}

// For printing internal errors.
static std::string StateDump(const Execution::State &state) {
  std::string ret;
  if (state.stack.empty()) {
    return ARED("No stack?");
  }

  // TODO: Print stack (but need program to cross-reference).

  const Execution::StackFrame &frame = state.stack.back();
  if (frame.insts == nullptr) return ARED("null instructions?");

  StringAppendF(&ret, AWHITE("Locals:") "\n");
  for (const auto &[x, v] : frame.locals) {
    StringAppendF(&ret,
                  "  " APURPLE("%s") " = %s\n",
                  x.c_str(), ColorValuePtrString(v).c_str());
  }

  // We have generally advanced the IP by this point.
  const int target_ip = frame.ip - 1;
  StringAppendF(&ret, AWHITE("Fault here:") "\n");
  for (int i = std::max(0, frame.ip - 8);
       i < (int)frame.insts->size() && i < frame.ip + 4;
       i++) {
    StringAppendF(&ret, "%s%s\n",
                  // U+1F4A3 BOMB EMOJI
                  // TODO: Use this again when mintty is fixed?
                  // " 💣 "
                  // " \U0001F4A3 "
                  (i == target_ip) ?
                  AFGCOLOR(255, 0, 0, " ‼ ")
                  : "   ",
                  ColorInstString((*frame.insts)[i]).c_str());
  }

 return ret;
}

// BoVeX uses BigInt for integers, but many internal functions
// just use machine words (e.g. image dimensions). This
// gets a machine-word-sized integer from the BigInt, or fails
// using 'what' to describe the caller.
static int64_t GetInt64(const char *what, const BigInt &x) {
  const std::optional<int64_t> xo = x.ToInt();
  CHECK(xo.has_value()) << "In " << what << ", integer needs to fit "
    "into 64 bits! Got: " << x.ToString();
  return xo.value();
}

Value *Execution::Bool(bool x, State *state) {
  return NewValue(&state->heap, uint64_t(x ? 1 : 0));
}

Value *Execution::Word(uint64_t w, State *state) {
  return NewValue(&state->heap, w);
}

Value *Execution::String(std::string s, State *state) {
  return NewValue(&state->heap, std::move(s));
}

Value *Execution::Float(double d, State *state) {
  return NewValue(&state->heap, d);
}

Value *Execution::Obj(map_type m, State *state) {
  return NewValue(&state->heap, std::move(m));
}

Value *Execution::Big(BigInt b, State *state) {
  return NewValue(&state->heap, std::move(b));
};


Value *Execution::DoTriop(Primop primop, Value *a, Value *b, Value *c,
                          State *state) {
  // TODO: Add this to internal errors below.
  auto Err = [state]() { return StateDump(*state); };

  switch (primop) {
  case Primop::STRING_SUBSTR: {
    const std::string *as = std::get_if<std::string>(&a->v);
    const BigInt *bi = std::get_if<BigInt>(&b->v);
    const BigInt *ci = std::get_if<BigInt>(&c->v);
    CHECK(as != nullptr && bi != nullptr && ci != nullptr) <<
      Err() << "Expected string,int,int to string-substr";
    const int64_t bb = GetInt64("substr start", *bi);
    const int64_t cc = GetInt64("substr length", *ci);
    CHECK(bb >= 0 && cc >= 0 &&
          bb <= (int64_t)as->size() &&
          bb + cc <= (int64_t)as->size())
      << Err() << "In string-substr, out of range start/length: "
      << bb << ", " << cc;

    std::string s = as->substr(bb, cc);
    return String(s, state);
  }

  case Primop::STRING_REPLACE: {
    const std::string *as = std::get_if<std::string>(&a->v);
    const std::string *bs = std::get_if<std::string>(&b->v);
    const std::string *cs = std::get_if<std::string>(&c->v);

    CHECK(as != nullptr && bs != nullptr && cs != nullptr) <<
      Err() << "Expected string,string,string to string-replace";

    return String(Util::Replace(*as, *bs, *cs), state);
  }

  case Primop::FONT_REGISTER: {
    const std::string *as = std::get_if<std::string>(&a->v);
    const std::string *bs = std::get_if<std::string>(&b->v);
    const BigInt *ci = std::get_if<BigInt>(&c->v);
    CHECK(as != nullptr && bs != nullptr && ci != nullptr) <<
      Err() << "Expected string,string,int to font-register";

    const Font *font = DocumentHook()->GetFontByName(*as);

    Document::FontDescription desc;
    desc.font_family = *bs;
    desc.font_bold = !!BigInt::BitwiseAnd(*ci, 1);
    desc.font_italic = !!BigInt::BitwiseAnd(*ci, 2);
    DocumentHook()->RegisterFont(desc, font);
    return Unit(state);
  }

  default:
    LOG(FATAL) << Err() << "Unimplemented or non-triop primop: "
               << PrimopString(primop);
  }

  return NonceValue();
}

std::tuple<int64_t, int64_t> Execution::GetPageAndFrame(const char *what,
                                                        const map_type *am) {
  const BigInt *ai = GetObjIntField(what, "page", *am);
  CHECK(ai != nullptr) << "out-layout requires a page (int field)";
  const int64_t page_idx = GetInt64("out-layout page index", *ai);
  CHECK(page_idx >= 0 && page_idx < 1'000'000'000LL) << "Page index "
    "is nonsensical!";

  int64_t frame_idx = 0;
  const BigInt *fi = GetObjIntField(what, "frame", *am);
  if (fi != nullptr) {
    frame_idx = GetInt64("out-layout frame index", *fi);
    CHECK(frame_idx >= 0 && frame_idx < 1'000'000'000LL) << "Frame index "
      "is nonsensical!";
  }

  return std::make_pair(page_idx, frame_idx);
};

Value *Execution::DoBinop(Primop primop, Value *a, Value *b,
                          State *state) {
  auto Err = [state]() { return StateDump(*state); };

  auto TwoInts = [a, b, &Err](const char *what) ->
    std::tuple<const BigInt &, const BigInt &> {
      const BigInt *abi = std::get_if<BigInt>(&a->v);
      const BigInt *bbi = std::get_if<BigInt>(&b->v);
      CHECK(abi != nullptr) << Err()
                            << "Expected int argument (lhs) to " << what;
      CHECK(bbi != nullptr) << Err()
                            << "Expected int argument (rhs) to " << what;
      return std::tie(*abi, *bbi);
    };

  auto TwoWords = [a, b, &Err](const char *what) ->
    std::tuple<uint64_t, uint64_t> {
      const uint64_t *aw = std::get_if<uint64_t>(&a->v);
      const uint64_t *bw = std::get_if<uint64_t>(&b->v);
      CHECK(aw != nullptr) << Err()
                           << "Expected word argument (lhs) to " << what;
      CHECK(bw != nullptr) << Err()
                           << "Expected word argument (rhs) to " << what;
      return std::make_pair(*aw, *bw);
    };

  auto TwoFloats = [a, b, &Err](const char *what) ->
    std::tuple<double, double> {
      const double *ad = std::get_if<double>(&a->v);
      const double *bd = std::get_if<double>(&b->v);
      CHECK(ad != nullptr) << Err()
                           << "Expected float argument (lhs) to " << what;
      CHECK(bd != nullptr) << Err()
                           << "Expected float argument (rhs) to " << what;
      return std::make_pair(*ad, *bd);
    };

  auto TwoStrings = [a, b, &Err](const char *what) ->
    std::tuple<const std::string &, const std::string &> {
    const std::string *as = std::get_if<std::string>(&a->v);
    const std::string *bs = std::get_if<std::string>(&b->v);
    CHECK(as != nullptr) << Err()
                         << "Expected string argument (lhs) to " << what;
    CHECK(bs != nullptr) << Err()
                         << "Expected string argument (rhs) to " << what;
    return std::tie(*as, *bs);
  };

  auto TwoObjs = [a, b, &Err](const char *what) ->
    std::tuple<const map_type &, const map_type &> {
    const map_type *as = std::get_if<map_type>(&a->v);
    const map_type *bs = std::get_if<map_type>(&b->v);
    CHECK(as != nullptr) << Err()
                         << "Expected obj argument (lhs) to " << what;
    CHECK(bs != nullptr) << Err()
                         << "Expected obj argument (rhs) to " << what;
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

  case Primop::INT_ANDB: {
    const auto &[aa, bb] = TwoInts("int_andb");
    return Big(BigInt::BitwiseAnd(aa, bb));
  }

  case Primop::INT_XORB: {
    const auto &[aa, bb] = TwoInts("int_xorb");
    return Big(BigInt::BitwiseXor(aa, bb));
  }

  case Primop::INT_ORB: {
    const auto &[aa, bb] = TwoInts("int_orb");
    return Big(BigInt::BitwiseOr(aa, bb));
  }

  case Primop::INT_SHL: {
    const auto &[aa, bb] = TwoInts("int_orb");
    const int64_t amount = GetInt64("left shift", bb);
    if (amount < 0) {
      InternalFail("left shift by negative amount", state);
      return NonceValue();
    }
    return Big(BigInt::LeftShift(aa, amount));
  }

  case Primop::INT_SHR: {
    const auto &[aa, bb] = TwoInts("int_orb");
    const int64_t amount = GetInt64("right shift", bb);
    if (amount < 0) {
      InternalFail("right shift by negative amount", state);
      return NonceValue();
    }
    return Big(BigInt::RightShift(aa, amount));
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
    const auto &[aa, bb] = TwoFloats("float_plus");
    return Float(aa + bb, state);
  }

  case Primop::FLOAT_MINUS: {
    const auto &[aa, bb] = TwoFloats("float_minus");
    return Float(aa - bb, state);
  }

  case Primop::FLOAT_DIV: {
    const auto &[aa, bb] = TwoFloats("float_div");
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

  case Primop::STRING_LESS: {
    const auto &[aa, bb] = TwoStrings("string_less");
    return Bool(aa < bb, state);
  }

  case Primop::STRING_GREATER: {
    const auto &[aa, bb] = TwoStrings("string_greater");
    return Bool(aa > bb, state);
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

  case Primop::WORD_EQ: {
    const auto &[aa, bb] = TwoWords("word_eq");
    return Bool(aa == bb, state);
  }

  case Primop::WORD_ANDB: {
    const auto &[aa, bb] = TwoWords("word_andb");
    return Word(aa & bb, state);
  }

  case Primop::SET_PAGE_INFO: {
    const auto &[as, bs] = TwoObjs("set-page-info");
    const auto &[page_idx, frame_idx] = GetPageAndFrame("set-page-info", &as);

    std::unordered_map<std::string, AttrVal> attrs;
    for (const auto &[k, v] : bs) {
      const auto &[kk, vv] = ValueToAttrVal(k, *v);
      attrs[kk] = vv;
    }

    DocumentHook()->SetPageInfo(page_idx, frame_idx, attrs);
    return Unit(state);
  }

  case Primop::ACHIEVEMENT: {
    const auto &[aa, bb] = TwoStrings("achievement");
    Achievements::Get().Achieve(aa, bb);
    return Unit(state);
  }

  case Primop::OBJ_MERGE: {
    const auto &[as, bs] = TwoObjs("obj-merge");
    map_type merged = as;
    for (const auto &[k, v] : bs) merged[k] = v;
    return NewValue(&state->heap, std::move(merged));
  }

  case Primop::REPHRASINGS: {
    static constexpr int VERBOSE = 1;
    const BigInt *abi = std::get_if<BigInt>(&a->v);
    CHECK(abi != nullptr) << Err() <<
      "Expected int argument (lhs) to rephrasings";

    const int64_t times = GetInt64("rephrasings", *abi);
    CHECK(times >= 0 && times < 1000000) << Err() <<
      "rephrasings: Integer is out of reasonable range!";

    DocTree doc = ValueToDocTree(b);

    Rephrasing *rephrasing = RephrasingHook();
    if (VERBOSE > 1) {
      printf(ABGCOLOR(0, 0, 180, "Rephrase input doc:") "\n");
      DebugPrintDocTree(doc);
    }
    Rephrasing::Rephrasable rep = Rephrasing::GetTextToRephrase(doc);
    if (VERBOSE > 0) {
      std::string t = (VERBOSE > 1 || rep.text.size() < 40) ? rep.text :
        rep.text.substr(0, 40) + AGREY("...");
      printf("Rephrase: [%s]\n", t.c_str());
    }
    const int already_have = rephrasing->GetNumRephrasings(rep);
    if (VERBOSE > 0 && already_have > 0) {
      printf("Already have " ACYAN("%d") " rephrasings from database!\n",
             already_have);
    }

    if (doc.IsEmpty() || rep.text.empty()) {
      if (VERBOSE > 0) {
        printf("Not rephrasing " AORANGE("empty doc") ".\n");
      }
    } else {
      // int max_attempts = std::min(20, times * 2);
      int max_attempts = times * 2;
      while (rephrasing->GetNumRephrasings(rep) < times) {
        if (rephrasing->Rephrase(rep)) {
          // printf("\nRephrased " AGREEN("OK") "\n");
        }
        max_attempts--;
        if (max_attempts < 0) break;
      }
    }

    // Now get them all.
    std::vector<std::pair<double, std::string>> reps =
      rephrasing->GetRephrasings(rep);

    std::vector<DocTree> ret;

    // Always include the original.
    DocTree orig;
    orig.SetStringAttr("display", "span");
    orig.SetDoubleAttr("loss", 0.0);
    orig.AddChild(doc);
    if (VERBOSE > 1) {
      printf("Now the doc is:\n");
      DebugPrintDocTree(orig);
    }
    ret.emplace_back(orig);

    for (const auto &[loss, text] : reps) {
      DocTree one;
      std::string error;
      if (!rephrasing->Rejoin(rep, text, &one, &error)) {
        printf(ARED("Should not happen!") " The rephrasing was supposedly "
               "valid but I couldn't rejoin it.\n"
               "Error: %s\n"
               "Text:\n%s\n",
               error.c_str(),
               text.c_str());
      } else {
        DocTree span;
        span.children = {std::make_shared<DocTree>(std::move(one))};
        span.SetStringAttr("display", "span");
        span.SetDoubleAttr("loss", loss);
        if (VERBOSE > 1) {
          DebugPrintDocTree(span);
        }
        ret.push_back(std::move(span));
      }
    }

    return DocTreeToValue(&state->heap.used, JoinDocs(std::move(ret)));
  }

  case Primop::OUT_LAYOUT: {
    const map_type *am = std::get_if<map_type>(&a->v);
    CHECK(am != nullptr) << Err() <<
      "out-layout expects an object as its first argument.";
    const auto &[page_idx, frame_idx] = GetPageAndFrame("out-layout", am);
    OutputLayoutHook((int)page_idx, (int)frame_idx, b);
    return Unit(state);
  }

  case Primop::PACK_BOXES: {
    const double *ad = std::get_if<double>(&a->v);
    CHECK(ad != nullptr) << Err() <<
      "Expected double argument (lhs) to pack-boxes";
    const map_type *arg = std::get_if<map_type>(&b->v);
    CHECK(arg != nullptr) << Err() <<
      "Expected obj argument (rhs) to pack-boxes";

    Document::Algorithm algorithm = Document::Algorithm::BEST;
    if (const std::string *algo =
        GetObjStringField("pack-boxes", "algorithm", *arg)) {
      if (*algo == "best") {
        algorithm = Document::Algorithm::BEST;
      } else if (*algo == "first") {
        algorithm = Document::Algorithm::FIRST;
      } else {
        // XXX Should be InternalFail?
        LOG(FATAL) << "pack-boxes algorithm field unknown: " << *algo;
      }
    }

    using Justification = BoxesAndGlue::Justification;
    Justification just = Justification::FULL;
    if (const std::string *j =
        GetObjStringField("pack-boxes", "justification", *arg)) {
      if (*j == "full") {
        just = Justification::FULL;
      } else if (*j == "left") {
        just = Justification::LEFT;
      } else if (*j == "center") {
        just = Justification::CENTER;
      } else if (*j == "all") {
        just = Justification::ALL;
      } else {
        // XXX Should be InternalFail?
        LOG(FATAL) << "pack-boxes justification field unknown: " << *j;
      }
    }

    const double line_width = *ad;
    // TODO: Make configurable.
    const double orphan_threshold = line_width / 3.0;

    const Value *doc_value = GetRequiredObjField("pack-boxes", "doc",
                                                 bc::ObjectFieldType::LAYOUT,
                                                 *arg);
    DocTree doc = ValueToDocTree(doc_value);

    const auto &[packdoc, badness] = DocumentHook()->PackBoxes(
        algorithm, just, orphan_threshold, line_width, doc);

    auto MakeField = [](const bc::ObjectFieldType oft,
                        const std::string &field) {
        return StringPrintf("%c%s",
                            ObjectFieldTypeTag(oft), field.c_str());
      };

    map_type obj{
        {MakeField(bc::ObjectFieldType::FLOAT, "badness"),
         Float(badness, state)},
        {MakeField(bc::ObjectFieldType::LAYOUT, "layout"),
         DocTreeToValue(&state->heap.used, packdoc)},
      };

    return NewValue(&state->heap, std::move(obj));
  }

  case Primop::LAYOUT_VEC_SUB: {
    // PERF: Could compile this away to GetVec.
    const BigInt *bb = std::get_if<BigInt>(&b->v);
    CHECK(bb != nullptr) << Err()
                         << "Expected int argument (rhs) to layout-vec-sub";
    const auto &[attrs, children] = GetNode("layout-vec-sub", a);
    const int64_t idx = GetInt64("layout-vec-sub", *bb);
    CHECK(idx >= 0 && idx < (int64_t)children.size()) << Err() <<
      "Index out of bounds in layout-vec-sub.\nIndex: " <<
      idx << "\nVector size: " << children.size();
    return children[idx];
  }

  case Primop::SET_ATTRS: {
    const map_type *aa = std::get_if<map_type>(&a->v);
    CHECK(aa != nullptr) << Err()
                         << "Expected obj first argument to set-attrs.";
    const auto &[attrs, children] = GetNodeParts("set-attrs", b);
    return Node(a, children, state);
  }

  case Primop::AUTO_DRAW: {
    const std::string *imghandle = std::get_if<std::string>(&a->v);
    CHECK(imghandle != nullptr) << Err() <<
      "Expected string first argument to internal-auto-draw.";
    const map_type *arg = std::get_if<map_type>(&b->v);
    CHECK(arg != nullptr) << Err() <<
      "Expected obj second argument to internal-auto-draw.";

    // Repeating defaults here for increased stability of BoVeX
    // documents in case I change the library's defaults.
    Animation::Options options{
      .smooth_passes = 5,
      .max_pen_radius = 14.0f,
      .min_pen_radius = 2.0f,
      .max_pen_velocity = 24.0f,
      .pen_acceleration = 0.5f,
      .adjacent_deltae_threshold = 10.0f,
      .max_fragile_piece_size = 10,
      .timesteps_per_frame = 8,
      .blend_frames = 20,
      .background_color = 0x00000000,
      .verbosity = 0,
    };

#   define SET_INT_FIELD(cpp, bovex) do {                               \
      if (const BigInt *i = GetObjIntField("auto-draw", bovex, *arg)) { \
        options. cpp = GetInt64("auto-draw " bovex "", *i);             \
      }                                                                 \
    } while (0)

#   define SET_FLOAT_FIELD(cpp, bovex) do {                             \
      if (const double *d = GetObjDoubleField("auto-draw", bovex, *arg)) { \
        options. cpp = *d;                                              \
      }                                                                 \
    } while (0)

    SET_INT_FIELD(smooth_passes, "smooth-passes");
    SET_FLOAT_FIELD(smooth_vote_threshold, "smooth-vote-threshold");
    SET_FLOAT_FIELD(max_pen_radius, "max-pen-radius");
    SET_FLOAT_FIELD(min_pen_radius, "min-pen-radius");
    SET_FLOAT_FIELD(max_pen_velocity, "max-pen-velocity");
    SET_FLOAT_FIELD(pen_acceleration, "pen-acceleration");
    SET_FLOAT_FIELD(adjacent_deltae_threshold, "adjacent-deltae-threshold");
    SET_INT_FIELD(max_fragile_piece_size, "max-fragile-piece-size");
    SET_INT_FIELD(timesteps_per_frame, "timesteps-per-frame");
    SET_INT_FIELD(blend_frames, "blend-frames");
    SET_INT_FIELD(verbosity, "verbosity");
    SET_INT_FIELD(background_color, "background-color");

    Document *doc = DocumentHook();
    const ImageRGBA *image = doc->GetImageByName(*imghandle);
    std::unique_ptr<Animation> anim(Animation::Create(*image, options));
    std::vector<ImageRGBA> frames = anim->Animate();

    // Construct the vector of image handles to return.
    vec_type ret;
    ret.reserve(frames.size());
    for (ImageRGBA &frame : frames) {
      ret.push_back(
          NewValue(
              &state->heap,
              doc->AddImage(
                  std::make_unique<ImageRGBA>(std::move(frame)))));
    }
    return NewValue(&state->heap, std::move(ret));
  }

  case Primop::IMAGE_INTEGER_SCALE: {
    const std::string *imghandle = std::get_if<std::string>(&a->v);
    CHECK(imghandle != nullptr) << Err() <<
      "Expected string first argument to image-integer-scale.";
    const BigInt *bb = std::get_if<BigInt>(&b->v);
    CHECK(bb != nullptr) << Err() <<
      "Expected int second argument to image-integer-scale.";
    int64_t scale = GetInt64("image-integer-scale", *bb);
    CHECK(scale > 0) << "image-integer-scale scale must be positive.";

    Document *doc = DocumentHook();
    const ImageRGBA *image = doc->GetImageByName(*imghandle);
    if (image == nullptr) {
      InternalFail("unknown image handle " + *imghandle, state);
      return NonceValue();
    }

    auto scaled = std::make_unique<ImageRGBA>(image->ScaleBy(scale));

    return NewValue(&state->heap, doc->AddImage(std::move(scaled)));
  }

  case Primop::REF_SET:
    LOG(FATAL) << Err()
               << "SET should have been compiled away.";
    break;
  case Primop::INVALID:
    LOG(FATAL) << Err()
               << "Tried executing INVALID primop as binop.";
    break;
  default:
    LOG(FATAL) << Err()
               << "Invalid (or non-binop) primop " << PrimopString(primop);
    break;
  }
  return NonceValue();
}

const std::string *Execution::GetObjStringField(const char *what,
                                                const std::string &field,
                                                const map_type &obj) {
  auto it = obj.find(
      StringPrintf("%c%s",
                   bc::ObjectFieldTypeTag(bc::ObjectFieldType::STRING),
                   field.c_str()));
  if (it == obj.end()) return nullptr;

  const std::string *s = std::get_if<std::string>(&it->second->v);
  CHECK(s != nullptr) << "(" << what <<
    ") Bug: Field " << field << " with string tag should have a string value!";
  return s;
}

const BigInt *Execution::GetObjIntField(const char *what,
                                        const std::string &field,
                                        const map_type &obj) {
  auto it = obj.find(
      StringPrintf("%c%s",
                   bc::ObjectFieldTypeTag(bc::ObjectFieldType::INT),
                   field.c_str()));
  if (it == obj.end()) return nullptr;

  const BigInt *i = std::get_if<BigInt>(&it->second->v);
  CHECK(i != nullptr) << "(" << what <<
    ") Bug: Field " << field << " with int tag should have an int value!";
  return i;
}

const double *Execution::GetObjDoubleField(const char *what,
                                           const std::string &field,
                                           const map_type &obj) {
  auto it = obj.find(
      StringPrintf("%c%s",
                   bc::ObjectFieldTypeTag(bc::ObjectFieldType::FLOAT),
                   field.c_str()));
  if (it == obj.end()) return nullptr;

  const double *d = std::get_if<double>(&it->second->v);
  CHECK(d != nullptr) << "(" << what <<
    ") Bug: Field " << field << " with float tag should have a float value!";
  return d;
}

const Value *Execution::GetRequiredObjField(const char *what,
                                            const std::string &field,
                                            bc::ObjectFieldType oft,
                                            const map_type &obj) {
  auto it = obj.find(
      StringPrintf("%c%s",
                   bc::ObjectFieldTypeTag(oft),
                   field.c_str()));
  CHECK(it != obj.end()) << "(" << what <<
    ") Field " << field << " was required but not found.";
  return it->second;
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
std::tuple<const Execution::map_type &, const Execution::vec_type &>
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

  auto GetVec = [a](const char *what) -> const vec_type & {
      const vec_type *v = std::get_if<vec_type>(&a->v);
      CHECK(v != nullptr) << "Expected vector argument to " << what;
      return *v;
  };

  switch (primop) {
  case Primop::INT_NEG: {
    const BigInt &bi = GetInt("int_neg");
    return Big(BigInt::Negate(bi), state);
  }

  case Primop::FLOAT_NEG: {
    const double d = GetFloat("float_neg");
    return Float(-d, state);
  }

  case Primop::FLOAT_ROUND: {
    const double d = GetFloat("float-round");
    return Big(BigInt(std::llround(d)), state);
  }

  case Primop::FLOAT_TRUNC: {
    const double d = GetFloat("float-trunc");
    return Big(BigInt(std::llround(std::trunc(d))), state);
  }

  case Primop::COS: {
    const double d = GetFloat("cos");
    return Float(cos(d), state);
  }

  case Primop::SIN: {
    const double d = GetFloat("sin");
    return Float(sin(d), state);
  }

  case Primop::INT_TO_STRING: {
    const BigInt &bi = GetInt("int_to_string");
    return String(bi.ToString(), state);
  }

  case Primop::FLOAT_TO_STRING: {
    const double d = GetFloat("float_to_string");
    return String(std::format("{:.17g}", d), state);
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
    return Big(BigInt(s.size()), state);
  }

  case Primop::STRING_FIRST_CODEPOINT: {
    const std::string &s = GetString("string-first-codepoint");
    if (s.empty()) return Obj({}, state);
    const auto &[len, cp] = UTF8::ParsePrefix(s.data(), s.size());
    std::string cp_field = StringPrintf(
        "%ccp", ObjectFieldTypeTag(ObjectFieldType::INT));
    std::string len_field = StringPrintf(
        "%clen", ObjectFieldTypeTag(ObjectFieldType::INT));
    map_type m = {
      { cp_field, Big(BigInt(cp), state) },
      { len_field, Big(BigInt(len), state) },
    };

    return Obj(std::move(m), state);
  }

  case Primop::CODEPOINT_TO_STRING: {
    const BigInt &bi = GetInt("codepoint-to-string");
    std::optional<int64_t> io = bi.ToInt();
    if (!io.has_value()) return String("", state);
    const int64_t cp = io.value();
    if (cp >= 0x1'0000'0000) return String("", state);
    return String(UTF8::Encode((uint32_t)cp), state);
  }

  case Primop::NORMALIZE_WHITESPACE: {
    const std::string &s = GetString("normalize-whitespace");
    return String(NormalizeWhitespace(s), state);
  }

  case Primop::STRING_LOWERCASE: {
    const std::string &s = GetString("string-lowercase");
    return String(Util::lcase(s), state);
  }

  case Primop::STRING_UPPERCASE: {
    const std::string &s = GetString("string-uppercase");
    return String(Util::ucase(s), state);
  }

  case Primop::EMIT_BADNESS: {
    const double d = GetFloat("emit_badness");
    EmitBadnessHook(d);
    return Unit(state);
  }

  case Primop::SET_DOC_INFO: {
    const map_type *obj = std::get_if<map_type>(&a->v);
    CHECK(obj != nullptr) << "set-doc-info takes an obj";

    std::unordered_map<std::string, AttrVal> attrs;
    for (const auto &[k, v] : *obj) {
      const auto &[kk, vv] = ValueToAttrVal(k, *v);
      attrs[kk] = vv;
    }

    DocumentHook()->SetDocumentInfo(attrs);
    return Unit(state);
  }

  case Primop::OPT: {
    const map_type *arg = std::get_if<map_type>(&a->v);
    CHECK(arg != nullptr) << "Expected obj argument internal-opt";

    const std::string *name = GetObjStringField("opt", "name", *arg);
    const double *lo = GetObjDoubleField("opt", "lo", *arg);
    const double *start = GetObjDoubleField("opt", "start", *arg);
    const double *hi = GetObjDoubleField("opt", "hi", *arg);

    CHECK(name != nullptr &&
          lo != nullptr && start != nullptr && hi != nullptr) <<
      "Expected { name, lo, start, hi } to opt";
    return Float(OptimizationHook(*name, *lo, *start, *hi), state);
  }

  case Primop::REPHRASE_ONCE: {
    Rephrasing *rephrasing = RephrasingHook();
    DocTree doc = ValueToDocTree(a);
    DebugPrintDocTree(doc);
    Rephrasing::Rephrasable rep =
      Rephrasing::GetTextToRephrase(doc);
    printf("Rephrase: [%s]\n", rep.text.c_str());
    if (rephrasing->Rephrase(rep)) {
      printf("\nRephrased " AGREEN("OK") "\n");
    }

    CHECK(!rep.text.empty()) << "Please fix this as in the REPHRASINGS case";

    // XXX: Taking the top-scoring doc is silly, as it will typically
    // be the input doc. We need to try different continuations, or
    // perhaps rephrase and pack together.
    //
    // Maybe the simplest way for this to work would be to just give
    // BoVeX code the ability to get the top X rephrasings (with
    // loss), which it can use to pack as locally or globally as it
    // likes (and then it needs access to the packing results). This
    // does not preclude global optimization!
    std::vector<std::pair<double, std::string>> reps =
      rephrasing->GetRephrasings(rep);

    if (!reps.empty()) {
      printf("Returning (score %.11g): %s\n",
             reps[0].first,
             reps[0].second.c_str());
      return DocTreeToValue(&state->heap.used, TextDoc(reps[0].second));
    }

    printf("(returning original)");
    return a;
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
    return Big(BigInt((int)children.size()), state);
  }

  case Primop::VEC_SIZE: {
    const vec_type &v = GetVec("vec-size");
    return Big(BigInt((int)v.size()), state);
  }

  case Primop::DEBUG_PRINT_DOC: {
    DocTree doc = ValueToDocTree(a);
    DebugPrintDocTree(doc);
    return Unit(state);
  }

  case Primop::FONT_LOAD_FILE: {
    const std::string filename = FindFileHook(GetString("font-load-file"));
    std::string f = DocumentHook()->LoadFontFile(filename);
    return String(std::move(f), state);
  }

  case Primop::IMAGE_LOAD_FILE: {
    const std::string filename = FindFileHook(GetString("image-load-file"));
    std::string f = DocumentHook()->LoadImageFile(filename);
    return String(std::move(f), state);
  }

  case Primop::IMAGE_PROPS: {
    const std::string img = GetString("image-props");
    const ImageRGBA *image = DocumentHook()->GetImageByName(img);
    std::string width_field =
      StringPrintf("%cwidth",
                   bc::ObjectFieldTypeTag(bc::ObjectFieldType::INT));
    std::string height_field =
      StringPrintf("%cheight",
                   bc::ObjectFieldTypeTag(bc::ObjectFieldType::INT));
    map_type obj = {
      {width_field, Big(BigInt(image->Width()), state)},
      {height_field, Big(BigInt(image->Height()), state)}
    };
    return Obj(std::move(obj), state);
  }

  case Primop::REF:
    LOG(FATAL) << "REF should have been compiled away";
    break;
  case Primop::REF_GET:
    LOG(FATAL) << "GET should have been compiled away";
    break;
  case Primop::INVALID:
    LOG(FATAL) << "Tried executing INVALID primop as unop.";
    break;
  default:
    LOG(FATAL) << "Invalid (or non-unop) primop " << PrimopString(primop);
    break;
  }
  return NonceValue();
}

void Execution::RunToCompletion(State *state) {
  // TODO: Probably better to count allocations?
  static constexpr int GC_EVERY = 10'000'000;
  for (int64_t iters = 0; !IsDone(*state); iters++) {
    Step(state);
    if ((iters + 1) % GC_EVERY == 0) {
      GC(state);
    }
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
      auto *r = std::get_if<std::vector<Value *>>(&a->v);
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

  } else if (const inst::TailCall *tail_call =
             std::get_if<inst::TailCall>(&inst)) {
    // For a tail call, we leave the stack how it is (ready to receive
    // some returned value), but replace the current topmost frame
    // with the called function's.

    const std::string &fp = LoadString(tail_call->f);
    const auto it = program.code.find(fp);
    CHECK(it != program.code.end()) << Error() <<
      "Tail Call to unknown function " << fp;
    const auto &[farg, finsts] = it->second;

    // Instead of pushing a frame, clear everything but
    // the arg.
    std::unordered_map<std::string, Value *> new_locals =
      {{farg, Load(tail_call->arg)}};

    CHECK(!state->stack.empty());
    StackFrame *frame = &state->stack.back();
    frame->insts = &finsts;
    frame->ip = 0;
    frame->locals = std::move(new_locals);

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

    const int64_t idx = GetInt64("setvec", bidx);
    CHECK(idx >= 0) << "SetVec Index cannot be less than zero: " << idx;
    while ((int64_t)vec->size() <= idx) vec->push_back(nullptr);
    (*vec)[idx] = Load(setvec->arg);

  } else if (const inst::GetVec *getvec =
             std::get_if<inst::GetVec>(&inst)) {
    std::vector<Value *> *vec = LoadVec(getvec->vec);
    const BigInt &bidx = LoadInt(getvec->idx);

    const int64_t idx = GetInt64("getvec", bidx);
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
    (void)note;
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

void Execution::GC(State *state) {
  // PERF: It's kinda slow, probably because deleting is slow. We
  // should consider the standard tricks:
  //    - major/minor heaps
  //    - placement new?
  //    - copying GC to compact heap
  static constexpr bool VERBOSE_GC = true;
  if (VERBOSE_GC) {
    fprintf(stderr, "GC:\n");
  }

  Timer gc_timer;
  // Mark-sweep collector. First we mark all the reachable nodes
  // in this set.
  std::unordered_set<Value *> reachable;

  // Manually managed queue, since we don't care what order we
  // mark in.
  std::vector<Value *> todo;
  for (auto &[s, v] : state->globals)
    todo.push_back(v);

  for (StackFrame &frame : state->stack) {
    for (auto &[s, v] : frame.locals) {
      todo.push_back(v);
    }
  }

  while (!todo.empty()) {
    Value *v = todo.back();
    todo.pop_back();
    // No cycles.
    if (!reachable.contains(v)) {
      reachable.insert(v);

      if (map_type *m = std::get_if<map_type>(&v->v)) {
        for (auto &[s, vv] : *m) {
          todo.push_back(vv);
        }
      } else if (vec_type *vec = std::get_if<vec_type>(&v->v)) {
        for (Value *vv : *vec) {
          todo.push_back(vv);
        }
      }
    }
  }

  double mark_sec = gc_timer.Seconds();
  if (VERBOSE_GC) {
    fprintf(stderr, "  Mark %zu/%zu allocs in %s\n",
            reachable.size(),
            state->heap.used.size(),
            ANSI::Time(mark_sec).c_str());
  }

  // Sweep.
  std::vector<Value *> new_used;
  int64_t collected = 0;
  for (Value *v : state->heap.used) {
    if (reachable.contains(v)) {
      new_used.push_back(v);
    } else {
      collected++;
      delete v;
    }
  }
  state->heap.used = std::move(new_used);
  new_used.clear();
  state->collected += collected;

  double total_sec = gc_timer.Seconds();
  if (VERBOSE_GC) {
    fprintf(stderr, "  Finished in %s total.\n",
            ANSI::Time(total_sec).c_str());
  }

}

}  // namespace bc
