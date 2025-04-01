
#include "simplification.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bignum/big-overloads.h"
#include "bignum/big.h"
#include "context.h"
#include "functional-map.h"
#include "il-pass.h"
#include "il-typed-pass.h"
#include "il-util.h"
#include "il.h"
#include "primop.h"
#include "progress.h"
#include "utf8.h"
#include "util.h"

static constexpr bool VERBOSE = false;

using ProgressRecorder = Progress<VERBOSE>;

// TODO: Can do some typed simplification, like:
//   - unit erasure
//   - flatten records

namespace il {

Simplification::Simplification(AstPool *pool) : pool(pool) {}

void Simplification::SetVerbose(int v) {
  verbose = VERBOSE ? std::max(v, 2) : v;
}

// A brief (suitable for one-liner), color version of an expression.
static std::string ExpStringShort(const Exp *e) {
  switch (e->type) {
  default:
    return StringPrintf(ABLUE("%s"), ExpTypeString(e->type));
  }
}

// True if the expression is a value and cheaper/smaller than
// a variable lookup, and so it should always be inlined.
static bool IsSmallValue(const Exp *e) {
  switch (e->type) {
  case ExpType::FLOAT:
    return true;
  case ExpType::BOOL:
    return true;
  case ExpType::INT:
    // Since we use bigint, avoid substituting huge numbers.
    // (This could probably be increased a lot without problems!)
    return e->Int() < 4'000'000ULL;
  case ExpType::WORD:
    return true;
  case ExpType::STRING:
    // PERF: Should consider inlining small strings by other means?
    return e->String().empty();
  case ExpType::VAR:
    return true;
  case ExpType::RECORD:
    return e->Record().empty();

  case ExpType::OBJECT:
    return e->Object().empty();

  // TODO: Functions that are primops or constructors.
  default:
    return false;
  }
}

static bool IsEffectless(const Exp *exp) {
  switch (exp->type) {
  case ExpType::FLOAT: return true;
  case ExpType::BOOL: return true;
  case ExpType::INT: return true;
  case ExpType::WORD: return true;
  case ExpType::STRING: return true;
  case ExpType::VAR: return true;
  case ExpType::FN: return true;

  case ExpType::RECORD: {
    for (const auto &[lab, child] : exp->Record()) {
      if (!IsEffectless(child)) return false;
    }
    return true;
  }

  case ExpType::OBJECT: {
    for (const auto &[lab, oft, child] : exp->Object()) {
      if (!IsEffectless(child)) return false;
    }
    return true;
  }

  case ExpType::PROJECT: {
    const auto &[l, t, e] = exp->Project();
    return IsEffectless(e);
  }
  case ExpType::INJECT:
    return IsEffectless(std::get<2>(exp->Inject()));

  case ExpType::ROLL:
    return IsEffectless(std::get<1>(exp->Roll()));

  case ExpType::PRIMAPP: {
    const auto &[po, ts, es] = exp->Primapp();
    if (IsPrimopTotal(po)) {
      for (const Exp *child : es) {
        if (!IsEffectless(child)) {
          return false;
        }
      }
      return true;
    }

    return false;
  }

  default:
    return false;
  }
}

static void PushSeqs(const Exp *exp, std::vector<const Exp *> *vflat) {
  switch (exp->type) {
  case ExpType::FAIL:
    vflat->push_back(exp);
    return;
  case ExpType::APP:
    // TODO: Maybe constructor applications?
    vflat->push_back(exp);
    return;

  case ExpType::FLOAT: return;
  case ExpType::BOOL: return;
  case ExpType::INT: return;
  case ExpType::WORD: return;
  case ExpType::STRING: return;
  case ExpType::VAR: return;
  case ExpType::FN: return;

  case ExpType::RECORD: {
    for (const auto &[lab, child] : exp->Record()) {
      PushSeqs(child, vflat);
    }
    return;
  }

  case ExpType::PROJECT: {
    const auto &[l, t, e] = exp->Project();
    return PushSeqs(e, vflat);
  }
  case ExpType::INJECT:
    return PushSeqs(std::get<2>(exp->Inject()), vflat);

  case ExpType::ROLL:
    return PushSeqs(std::get<1>(exp->Roll()), vflat);
  case ExpType::UNROLL:
    return PushSeqs(std::get<0>(exp->Unroll()), vflat);

  case ExpType::PRIMAPP: {
    const auto &[po, ts, es] = exp->Primapp();
    if (IsPrimopDiscardable(po)) {
      for (const Exp *child : es) {
        PushSeqs(child, vflat);
      }
    } else {
      vflat->push_back(exp);
    }
    return;
  }

    // TODO: Several more here.
  case ExpType::INTCASE: {
    // const auto &[obj, arms, def] = e->IntCase();
    // Actually, can we ever throw this away? We'd have to
    // know whether an arm is going to match. If all of them
    // are discardable (and the default), then ok.
    //
  }

  default:
    vflat->push_back(exp);
    break;
  }
}

// This is almost the same as effectless except that some primops can
// be dropped (e.g. GET) despite not being formally total. We have to
// do the whole thing instead of appealing to IsEffectless for other
// cases, since we want something like (GET, GET) to be considered
// discardable.
//
// TODO: This is probably inferior to PushSeqs, which lets us drop
// parts of the expression that are not effectful. We defer to that
// function now anyway.
static bool IsDiscardable(const Exp *e) {
  std::vector<const Exp *> tmp;
  PushSeqs(e, &tmp);
  return tmp.empty();
}

namespace {
struct PeepholePass : public il::Pass<> {
  PeepholePass(uint64_t opts, AstPool *p, ProgressRecorder *progress) :
    Pass(p),
    opts(opts),
    progress(progress) {}

  const Exp *DoUnroll(const Exp *e, const Type *mu_type,
                      const Exp *guess) override {
    e = DoExp(e);
    if ((opts & Simplification::O_REDUCE) && e->type == ExpType::ROLL) {
          const auto &[tt, ee] = e->Roll();
      Simplified("reduce unroll");
      return ee;
    }
    return pool->Unroll(e, DoType(mu_type), guess);
  }

  const Exp *DoProject(const std::string &label,
                       const Type *record_type,
                       const Exp *arg,
                       const Exp *guess) override {
    arg = DoExp(arg);
    if ((opts & Simplification::O_REDUCE) &&
        arg->type == ExpType::RECORD) {
      // Evaluate all the elements in order, so that
      // we can preserve evaluation order. We just make a
      // binding for each and then let other simplifications
      // throw them away.
      const auto &le = arg->Record();
      std::vector<std::string> vars;
      const Exp *body = nullptr;
      for (const auto &[lab, exp] : le) {
        vars.push_back(pool->NewVar(lab));
        if (lab == label) {
          CHECK(body == nullptr) << "Duplicate label " << lab;
          body = pool->Var({}, vars.back());
        }
      }
      CHECK(body != nullptr) << "Bug? Label missing when simplifying "
        "projection expression. Label: " << label;

      // Now wrap the body.
      for (int i = le.size() - 1; i >= 0; i--) {
        const auto &[lab_, exp] = le[i];
        body = pool->Let({}, vars[i], exp, body);
      }

      Simplified("reduce project(record)");
      return body;
    } else {
      return pool->Project(label, record_type, arg, guess);
    }
  }

  const Exp *DoPrimapp(Primop po,
                       const std::vector<const Type *> &ts,
                       const std::vector<const Exp *> &es,
                       const Exp *guess) override {
    std::vector<const Type *> tts;
    tts.reserve(ts.size());
    for (const Type *t : ts) tts.push_back(DoType(t));

    std::vector<const Exp *> ees;
    ees.reserve(es.size());
    for (const Exp *e : es) ees.push_back(DoExp(e));

    if (opts & Simplification::O_REDUCE) {
      const auto &[num_types, num_args] = PrimopArity(po);
      CHECK((int)ees.size() == num_args &&
            (int)tts.size() == num_types) << "Internal type error: Wrong "
        "number of args to primop " << PrimopString(po) << ". Wanted " <<
        num_types << " types and " << num_args << " values, but got " <<
        tts.size() << " types and " << ees.size() << " values.";

      switch (po) {
      case Primop::STRING_CONCAT:
        if (ees[0]->type == ExpType::STRING &&
            ees[1]->type == ExpType::STRING) {
          Simplified("string-concat primop");
          return pool->String(ees[0]->String() + ees[1]->String());
        }
        break;

      case Primop::STRING_EMPTY:
        if (ees[0]->type == ExpType::STRING) {
          Simplified("string-empty primop");
          return pool->Bool(ees[0]->String().empty());
        }
        break;

      case Primop::STRING_SIZE:
        if (ees[0]->type == ExpType::STRING) {
          Simplified("string-size primop");
          return pool->Int(ees[0]->String().size());
        }
        break;

      case Primop::STRING_UPPERCASE:
        if (ees[0]->type == ExpType::STRING) {
          Simplified("string-uppercase primop");
          return pool->String(Util::ucase(ees[0]->String()));
        }
        break;

      case Primop::STRING_LOWERCASE:
        if (ees[0]->type == ExpType::STRING) {
          Simplified("string-lowercase primop");
          return pool->String(Util::lcase(ees[0]->String()));
        }
        break;

      case Primop::STRING_FIND:
        if (ees[0]->type == ExpType::STRING &&
            ees[1]->type == ExpType::STRING) {
          Simplified("string-find primop");
          const std::string &a = ees[0]->String();
          const std::string &b = ees[1]->String();
          auto pos = a.find(b);
          if (pos == std::string::npos) {
            return pool->Int(-1);
          } else {
            return pool->Int(pos);
          }
        }
        break;

      case Primop::STRING_REPLACE:
        if (ees[0]->type == ExpType::STRING &&
            ees[1]->type == ExpType::STRING &&
            ees[2]->type == ExpType::STRING) {
          const std::string &a = ees[0]->String();
          const std::string &b = ees[1]->String();
          const std::string &c = ees[2]->String();

          // Only if the resulting string will not
          // grow. We could say that we are
          // reducing the number of operations and
          // get a partial order that way, but it
          // could easily be counterproductive.
          if (c.size() <= b.size() ||
              a.find(b) == std::string::npos) {
            Simplified("string-replace primop");
            return pool->String(Util::Replace(a, b, c));
          }
        }
        break;

      case Primop::INT_EQ:
      case Primop::INT_NEQ:
      case Primop::INT_LESS:
      case Primop::INT_LESSEQ:
      case Primop::INT_GREATER:
      case Primop::INT_GREATEREQ:
        if (ees[0]->type == ExpType::INT &&
            ees[1]->type == ExpType::INT) {
          Simplified("int comparison primop");
          const BigInt &lhs = ees[0]->Int();
          const BigInt &rhs = ees[1]->Int();
          const bool result = [&lhs, &rhs, po]() {
              switch (po) {
              case Primop::INT_EQ: return BigInt::Eq(lhs, rhs);
              case Primop::INT_NEQ: return !BigInt::Eq(lhs, rhs);
              case Primop::INT_LESS: return BigInt::Less(lhs, rhs);
              case Primop::INT_LESSEQ: return BigInt::LessEq(lhs, rhs);
              case Primop::INT_GREATER: return BigInt::Greater(lhs, rhs);
              case Primop::INT_GREATEREQ: return BigInt::GreaterEq(lhs, rhs);
              default:
                LOG(FATAL) << "Bug";
                return false;
              }
            }();
          return pool->Bool(result);
        }
        break;

      case Primop::FLOAT_EQ:
      case Primop::FLOAT_NEQ:
      case Primop::FLOAT_LESS:
      case Primop::FLOAT_LESSEQ:
      case Primop::FLOAT_GREATER:
      case Primop::FLOAT_GREATEREQ:
        if (ees[0]->type == ExpType::FLOAT &&
            ees[1]->type == ExpType::FLOAT) {
          Simplified("float comparison primop");
          const bool lhs = ees[0]->Float();
          const bool rhs = ees[1]->Float();
          const bool result = [&lhs, &rhs, po]() {
              switch (po) {
              case Primop::FLOAT_EQ: return lhs == rhs;
              case Primop::FLOAT_NEQ: return lhs != rhs;
              case Primop::FLOAT_LESS: return lhs < rhs;
              case Primop::FLOAT_LESSEQ: return lhs <= rhs;
              case Primop::FLOAT_GREATER: return lhs > rhs;
              case Primop::FLOAT_GREATEREQ: return lhs >= rhs;
              default:
                LOG(FATAL) << "Bug";
                return false;
              }
            }();
          return pool->Bool(result);
        }
        break;

      case Primop::STRING_EQ:
      case Primop::STRING_LESS:
      case Primop::STRING_GREATER:
        if (ees[0]->type == ExpType::STRING &&
            ees[1]->type == ExpType::STRING) {
          Simplified("string comparison primop");
          const std::string &lhs = ees[0]->String();
          const std::string &rhs = ees[1]->String();
          const bool result = [&lhs, &rhs, po]() {
              switch (po) {
              case Primop::STRING_EQ: return lhs == rhs;
              case Primop::STRING_LESS: return lhs < rhs;
              case Primop::STRING_GREATER: return lhs > rhs;
              default:
                LOG(FATAL) << "Bug";
                return false;
              }
            }();
          return pool->Bool(result);
        }

        if (ees[0]->type == ExpType::STRING &&
            ees[0]->String().empty()) {
          Simplified("string compare against empty");
          return pool->Primapp(Primop::STRING_EMPTY, {}, {ees[1]});
        }

        if (ees[1]->type == ExpType::STRING &&
            ees[1]->String().empty()) {
          Simplified("string compare against empty");
          return pool->Primapp(Primop::STRING_EMPTY, {}, {ees[0]});
        }
        break;

      case Primop::INT_TIMES:
      case Primop::INT_PLUS:
      case Primop::INT_MINUS:
      case Primop::INT_ANDB:
      case Primop::INT_XORB:
      case Primop::INT_ORB:
      case Primop::INT_SHL:
      case Primop::INT_SHR:
      case Primop::INT_DIV:
      case Primop::INT_MOD:
        if (ees[0]->type == ExpType::INT &&
            ees[1]->type == ExpType::INT) {
          Simplified("int arithmetic primop");

          const BigInt &lhs = ees[0]->Int();
          const BigInt &rhs = ees[1]->Int();
          switch (po) {
          case Primop::INT_TIMES:
            return pool->Int(BigInt::Times(lhs, rhs));
          case Primop::INT_PLUS:
            return pool->Int(BigInt::Plus(lhs, rhs));
          case Primop::INT_MINUS:
            return pool->Int(BigInt::Minus(lhs, rhs));
          case Primop::INT_ANDB:
            return pool->Int(BigInt::BitwiseAnd(lhs, rhs));
          case Primop::INT_XORB:
            return pool->Int(BigInt::BitwiseXor(lhs, rhs));
          case Primop::INT_ORB:
            return pool->Int(BigInt::BitwiseOr(lhs, rhs));

          case Primop::INT_SHL: {
            const auto io = rhs.ToInt();
            if (!io.has_value()) {
              return pool->Fail(
                  pool->String("Left shift too big (static)"),
                  pool->IntType());
            }
            if (*io < 0) {
              return pool->Fail(
                  pool->String("Left shift by negative amount (static)"),
                  pool->IntType());
            }
            return pool->Int(BigInt::LeftShift(lhs, *io));
          }
          case Primop::INT_SHR: {
            const auto io = rhs.ToInt();
            if (!io.has_value()) {
              return pool->Fail(
                  pool->String("Right shift too big (static)"),
                  pool->IntType());
            }
            if (*io < 0) {
              return pool->Fail(
                  pool->String("Right shift by negative amount (static)"),
                  pool->IntType());
            }
            return pool->Int(BigInt::RightShift(lhs, *io));
          }

          case Primop::INT_DIV:
            if (BigInt::Eq(rhs, 0)) {
              return pool->Fail(pool->String("Division by zero (static)"),
                                pool->IntType());
            }
            return pool->Int(BigInt::Div(lhs, rhs));
          case Primop::INT_MOD:
            if (BigInt::Eq(rhs, 0)) {
              return pool->Fail(pool->String("Modulo by zero (static)"),
                                pool->IntType());
            }
            return pool->Int(BigInt::CMod(lhs, rhs));
          default:
            LOG(FATAL) << "Bug";
            return nullptr;
          }
        }
        // TODO: Other reductions, like 0 - e ==> -e
        // and 0 + e ==> e
        break;

      case Primop::INT_TO_STRING:
        if (ees[0]->type == ExpType::INT) {
          Simplified("int-to-string primop");
          const BigInt &b = ees[0]->Int();
          return pool->String(b.ToString());
        }
        break;

      case Primop::FLOAT_TO_STRING:
        if (ees[0]->type == ExpType::FLOAT) {
          Simplified("float-to-string primop");
          const double d = ees[0]->Float();
          return pool->String(std::format("{:.17g}", d));
        }
        break;

      case Primop::INT_NEG:
        if (ees[0]->type == ExpType::INT) {
          Simplified("int-neg primop");
          const BigInt &b = ees[0]->Int();
          return pool->Int(BigInt::Negate(b));
        }
        break;

      case Primop::INT_TO_FLOAT:
        if (ees[0]->type == ExpType::INT) {
          const BigInt &b = ees[0]->Int();
          const auto io = b.ToInt();
          if (io.has_value() &&
              // 2^53 is supposedly the largest exactly representable;
              // let's not cut it too close
              io.value() <= int64_t{1} << 52 &&
              io.value() >= -(int64_t{1} << 52)) {
            Simplified("int-neg primop");
            return pool->Float((double)io.value());
          }
        }
        break;

      case Primop::FLOAT_TIMES:
      case Primop::FLOAT_PLUS:
      case Primop::FLOAT_MINUS:
      case Primop::FLOAT_DIV:
        if (ees[0]->type == ExpType::FLOAT &&
            ees[1]->type == ExpType::FLOAT) {
          Simplified("float arithmetic primop");

          const double lhs = ees[0]->Float();
          const double rhs = ees[1]->Float();

          if (VERBOSE > 1) {
            printf("Simplifying float math %.3f and %.3f\n", lhs, rhs);
          }
          switch (po) {
          case Primop::FLOAT_TIMES: {
            if (VERBOSE > 1) {
              printf("Reducing %.3f * %.3f -> %.3f\n", lhs, rhs, lhs * rhs);
            }
            return pool->Float(lhs * rhs);
          }
          case Primop::FLOAT_PLUS:
            return pool->Float(lhs + rhs);
          case Primop::FLOAT_MINUS:
            return pool->Float(lhs - rhs);
          case Primop::FLOAT_DIV:
            return pool->Float(lhs / rhs);
          default:
            LOG(FATAL) << "Bug";
            return nullptr;
          }
        }
        break;

      case Primop::FLOAT_NEG:
        if (ees[0]->type == ExpType::FLOAT) {
          Simplified("float-neg primop");
          const double d = ees[0]->Float();
          return pool->Float(-d);
        }
        break;

      case Primop::FLOAT_ROUND:
        if (ees[0]->type == ExpType::FLOAT) {
          const double d = ees[0]->Float();
          // We can also support values outside this range, by
          // just making sure we have the same behavior as execution.
          // But currently we are a bit sloppy in execution, too.
          if (d > -1e52 && d < 1e52) {
            Simplified("float-round primop");
            return pool->Int(std::llround(d));
          }
        }
        break;

      case Primop::FLOAT_TRUNC:
        if (ees[0]->type == ExpType::FLOAT) {
          const double d = ees[0]->Float();
          // We can also support values outside this range, by
          // just making sure we have the same behavior as execution.
          // But currently we are a bit sloppy in execution, too.
          if (d > -1e52 && d < 1e52) {
            Simplified("float-trunc primop");
            return pool->Int(std::llround(std::trunc(d)));
          }
        }
        break;

      case Primop::COS:
        if (ees[0]->type == ExpType::FLOAT) {
          const double d = ees[0]->Float();
          Simplified("cos");
          return pool->Float(cos(d));
        }
        break;

      case Primop::SIN:
        if (ees[0]->type == ExpType::FLOAT) {
          const double d = ees[0]->Float();
          Simplified("sin");
          return pool->Float(sin(d));
        }
        break;

      case Primop::WORD_EQ:
        if (ees[0]->type == ExpType::WORD &&
            ees[1]->type == ExpType::WORD) {
          Simplified("word-eq");
          return pool->Bool(ees[0]->Word() == ees[1]->Word());
        }
        break;

      case Primop::WORD_ANDB:
        if (ees[0]->type == ExpType::WORD &&
            ees[1]->type == ExpType::WORD) {
          Simplified("word-andb");
          return pool->Word(ees[0]->Word() & ees[1]->Word());
        }
        break;

      case Primop::OBJ_EMPTY:
        if (ees[0]->type == ExpType::OBJECT) {
          const std::vector<
            std::tuple<std::string, ObjFieldType, const Exp *>> &fields =
            ees[0]->Object();
          return pool->Bool(fields.empty());
        }
        break;

      case Primop::CODEPOINT_TO_STRING:
        if (ees[0]->type == ExpType::INT) {
          const BigInt &bi = ees[0]->Int();
          auto io = bi.ToInt();
          // Codepoints that are out of range result in empty string.
          if (!io.has_value()) return pool->String("");
          uint64_t cp = io.value();
          if (cp >= 0x1'0000'0000) return pool->String("");
          return pool->String(UTF8::Encode(cp));
        }
        break;

      case Primop::STRING_SUBSTR:
        // TODO
        break;
      case Primop::INT_DIV_TO_FLOAT:
        // TODO
        break;
      case Primop::STRING_FIRST_CODEPOINT:
        // TODO
        break;
      case Primop::NORMALIZE_WHITESPACE:
        // TODO
        break;

      case Primop::STRING_TO_LAYOUT:
        // TODO!
        break;

      case Primop::OBJ_MERGE:
        // TODO
        break;

      case Primop::IS_TEXT:
        // TODO
        break;

      case Primop::GET_TEXT:
        // TODO
        break;

      case Primop::GET_ATTRS:
        if (ees[0]->type == ExpType::NODE) {
          const auto &[attrs, children] = ees[0]->Node();
          Simplified("get-attrs primop");
          // Maintain evaluation order.
          std::string v = pool->NewVar("attrs");
          pool->Let({}, v, attrs,
                    pool->Seq(children, pool->Var({}, v)));
        }
        break;

      case Primop::SET_ATTRS:
        // TODO
        break;

      case Primop::LAYOUT_VEC_SUB:
        // TODO
        break;

      case Primop::LAYOUT_VEC_SIZE:
        // TODO
        break;


      case Primop::REF_GET:
      case Primop::REF_SET:
        // In principle, if the argument is a call to ref,
        // we know we have the only copy of it. But why
        // would this code exist?
        break;

      case Primop::VEC_SUB:
      case Primop::VEC_SIZE:
        // As above.
        break;

      case Primop::VEC:
      case Primop::VEC_EMPTY:
      case Primop::VEC_UPDATE:
      case Primop::OUT_STRING:
      case Primop::OUT_LAYOUT:
      case Primop::EMIT_BADNESS:
      case Primop::SET_DOC_INFO:
      case Primop::SET_PAGE_INFO:
      case Primop::REF:
      case Primop::FONT_LOAD_FILE:
      case Primop::FONT_REGISTER:
      case Primop::IMAGE_LOAD_FILE:
      case Primop::IMAGE_PROPS:
      case Primop::IMAGE_INTEGER_SCALE:
      case Primop::REPHRASE_ONCE:
      case Primop::REPHRASINGS:
      case Primop::GET_BOXES:
      case Primop::PACK_BOXES:
      case Primop::AUTO_DRAW:
      case Primop::ACHIEVEMENT:
      case Primop::DEBUG_PRINT_DOC:
      case Primop::OPT:
        // No simplification, even with known args.
        break;

      case Primop::INVALID:
        LOG(FATAL) << "Saw invalid primop";
        break;
      }
    }

    return pool->Primapp(po, tts, ees, guess);
  }

  // For fn expressions, if the function's self variable is not used,
  // it is not actually recursive.
  const Exp *DoFn(const std::string &self,
                  const std::string &x,
                  const Type *arrow_type,
                  const Exp *body,
                  const Exp *guess) override {
    if ((opts & Simplification::O_MAKE_NONRECURSIVE) &&
        !self.empty() && !ILUtil::IsExpVarFree(body, self)) {
      Simplified("remove recursive fn var");
      if (VERBOSE) {
        printf("Removed var is " APURPLE("%s") "\n", self.c_str());
      }
      return pool->Fn("", x, DoType(arrow_type), DoExp(body), guess);
    }

    return pool->Fn(self, x, DoType(arrow_type), DoExp(body), guess);
  }

  const Exp *DoNode(const Exp *attrs,
                    const std::vector<const Exp *> &v,
                    const Exp *guess) override {
    if (opts & Simplification::O_SIMPLIFY_LAYOUT) {

      auto GetLayoutString = [](const Exp *e) -> const std::string * {
          if (e->type != ExpType::PRIMAPP) return nullptr;
          const auto &[po, ts, es] = e->Primapp();
          if (po != Primop::STRING_TO_LAYOUT)
            return nullptr;
          CHECK(ts.empty() && es.size() == 1);
          if (es[0]->type != ExpType::STRING)
            return nullptr;
          return &es[0]->String();
        };

      std::vector<const Exp *> vv;
      std::function<void(const Exp *)> Rec =
        [this, &GetLayoutString, &vv, &Rec](const Exp *ee) {
          const Exp *e = DoExp(ee);
          if (const std::string *s = GetLayoutString(e)) {
            const std::string *prevs = vv.empty() ? nullptr :
              GetLayoutString(vv.back());
            if (prevs == nullptr) {
              vv.push_back(e);
            } else {
              vv.pop_back();
              Simplified("concat string layout");
              vv.push_back(
                  pool->Primapp(Primop::STRING_TO_LAYOUT,
                                {},
                                {pool->String(*prevs + *s)}));
            }
          } else if (e->type == ExpType::NODE) {
            const auto &[ca, cc] = e->Node();
            if (ca->type == ExpType::OBJECT &&
                ca->Object().empty()) {
              Simplified("flatten layout child");
              for (const Exp *child : cc) {
                Rec(child);
              }
            } else {
              vv.push_back(e);
            }
          } else {
            vv.push_back(e);
          }
        };

      for (const Exp *e : v) Rec(e);

      const Exp *aa = DoExp(attrs);
      // If there are no attributes and one child, this is equivalent
      // to the one child.
      if (aa->type == ExpType::OBJECT) {
        const auto &obj = attrs->Object();
        if (obj.empty() && v.size() == 1) {
          Simplified("remove singleton node");
          return vv[0];
        }
      }

      return pool->Node(aa, vv, guess);

    } else {
      // TODO: Nodes that just concatenate children
      return Pass::DoNode(attrs, v, guess);
    }
  }

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs,
                   const Exp *body,
                   const Exp *guess) override {
    if ((opts & Simplification::O_ETA_CONTRACT) &&
        body->type == ExpType::VAR) {
      const auto &[vtv, xx] = body->Var();
      if (x == xx) {
        // let (a, b, ...) x = rhs in x<t1, t2, ...> end -->
        // [t1/a][t2/b]rhs
        CHECK(tyvars.size() == vtv.size());

        for (int i = 0; i < (int)tyvars.size(); i++) {
          // make sure the bound tyvar is fresh (not appearing in any
          // t1..tn).
          const auto &[a, nrhs] =
            ILUtil::AlphaVaryTypeInExp(pool, tyvars[i], rhs);
          rhs = nrhs;
          // and substitute for it
          rhs = ILUtil::SubstTypeInExp(pool, vtv[i], a, rhs);
        }

        Simplified("eta-contracted let x = e in x");
        return DoExp(rhs);
      } else {
        // This is handled by the below since x is not free in xx.
      }
    }

    int count = ILUtil::ExpVarCount(body, x);

    if ((opts & Simplification::O_DEAD_VARS) && count == 0) {
      Simplified("remove unused binding");
      if (VERBOSE) {
        printf("  Unused var is " APURPLE("%s") "\n", x.c_str());
      }

      // Substitute away any tyvars, since they will no longer be
      // bound. We can use anything since the generalized symbol
      // was never used.
      if (!tyvars.empty()) {
        const Type *ovoid = pool->SumType({});
        for (const std::string &alpha : tyvars) {
          rhs = ILUtil::SubstTypeInExp(pool, ovoid, alpha, rhs);
        }
      }

      return pool->Seq({DoExp(rhs)}, DoExp(body));
    }

    const bool small_value = IsSmallValue(rhs);
    const bool effectless = small_value || IsEffectless(rhs);

    if ((opts & Simplification::O_INLINE_EXP) && count <= 1 && effectless) {
      // Inline any effectless expression that occurs just once,
      // regardless of its size.
      Simplified("inlined single-use binding");
      if (VERBOSE) {
        std::string ts = tyvars.empty() ? "" :
          StringPrintf(", tyvars (" ABLUE("%s") ")",
                       Util::Join(tyvars, ",").c_str());
        printf("  Inlined var is " APURPLE("%s") "%s\n",
               x.c_str(), ts.c_str());
      }
      return ILUtil::SubstPolyExp(pool, tyvars, DoExp(rhs), x, DoExp(body));
    }

    // TODO: support inlining of polymorphic values.
    if ((opts & Simplification::O_INLINE_EXP) &&
        small_value && tyvars.empty()) {
      Simplified("inlined small value");
      const Exp *value = DoExp(rhs);
      if (VERBOSE) {
        printf("  Inlined var is " APURPLE("%s") " = %s\n",
               x.c_str(), ExpStringShort(value).c_str());
      }
      return ILUtil::SubstExp(pool, value, x, DoExp(body));
    }

    return pool->Let(tyvars, x, DoExp(rhs), DoExp(body), guess);
  }

  const Exp *DoIf(
      const Exp *cond,
      const Exp *true_branch,
      const Exp *false_branch,
      const Exp *guess) override {
    if ((opts & Simplification::O_REDUCE) && cond->type == ExpType::BOOL) {
      Simplified("reduced if");
      return DoExp(cond->Bool() ? true_branch : false_branch);
    } else {
      // TODO: if true and false branches are syntactically equal
      // TODO: if condition is a negation
      return Pass::DoIf(cond, true_branch, false_branch, guess);
    }
  }

  const Exp *DoIntCase(
      const Exp *obj,
      const std::vector<std::pair<BigInt, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if ((opts & Simplification::O_REDUCE) &&
        obj->type == ExpType::INT) {
      Simplified("reduce intcase");
      for (const auto &[bi, arm] : arms) {
        if (bi == obj->Int()) {
          return DoExp(arm);
        }
      }
      // None matched, so it's the default.
      return DoExp(def);
    } else {
      return Pass::DoIntCase(obj, arms, def, guess);
    }
  }

  const Exp *DoWordCase(
      const Exp *obj,
      const std::vector<std::pair<uint64_t, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if ((opts & Simplification::O_REDUCE) &&
        obj->type == ExpType::WORD) {
      Simplified("reduce wordcase");
      for (const auto &[w, arm] : arms) {
        if (w == obj->Word()) {
          return DoExp(arm);
        }
      }
      // None matched, so it's the default.
      return DoExp(def);
    } else {
      return Pass::DoWordCase(obj, arms, def, guess);
    }
  }

  const Exp *DoStringCase(
      const Exp *obj,
      const std::vector<std::pair<std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if ((opts & Simplification::O_REDUCE) &&
        obj->type == ExpType::STRING) {
      Simplified("reduce stringcase");
      for (const auto &[s, arm] : arms) {
        if (s == obj->String()) {
          return DoExp(arm);
        }
      }
      // None matched, so it's the default.
      return DoExp(def);
    } else {
      return Pass::DoStringCase(obj, arms, def, guess);
    }
  }

  const Exp *DoSumCase(
      const Exp *obj,
      const std::vector<
          std::tuple<std::string, std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if ((opts & Simplification::O_REDUCE) &&
        obj->type == ExpType::INJECT) {
      Simplified("reduce sumcase");
      const auto &[label, sum_type, e] = obj->Inject();
      for (const auto &[lab, v, arm] : arms) {
        if (label == lab) {
          return pool->Let({}, v, DoExp(e), arm);
        }
      }
      // No match, so it is the default.
      // Here the body of the inject could still have effects,
      // so sequence that.
      return pool->Seq({DoExp(e)}, DoExp(def));
    } else {
      // TODO: If it's exhaustive, we can replace the default with
      // one of the arms (unless we need to bind an inner object).
      return Pass::DoSumCase(obj, arms, def, guess);
    }
  }

  // If we have App(fn x => body, arg), with the function not recursive,
  // then this is equivalent to
  // let x = arg in body
  // The let sometimes allows for futher simplification.
  const Exp *DoApp(const Exp *f, const Exp *arg,
                   const Exp *guess) override {
    f = DoExp(f);
    arg = DoExp(arg);

    if ((opts & Simplification::O_REDUCE) &&
        f->type == ExpType::FN) {
      const auto &[self, x, t, body] = f->Fn();
      if (self.empty()) {
        Simplified("reduce app");
        return pool->Let({}, x, arg, DoExp(body));
      }
    }
    return pool->App(f, arg, guess);
  }

  const Exp *DoSeq(const std::vector<const Exp *> &v,
                   const Exp *body,
                   const Exp *guess) override {
    // First process them all recursively, so that they are flat.
    std::vector<const Exp *> vv;
    vv.reserve(v.size());
    for (const Exp *c : v) vv.push_back(DoExp(c));
    std::vector<const Exp *> vflat;
    for (const Exp *c : vv) {
      // printf("IsEffectless %s?\n", ExpString(c).c_str());
      if ((opts & Simplification::O_DEAD_CODE) &&
          IsDiscardable(c)) {
        Simplified("dropped effectless seq");
        if (VERBOSE) {
          printf("  Exp was %s\n", ExpStringShort(c).c_str());
        }
      } else {
        if ((opts & Simplification::O_FLATTEN) &&
            c->type == ExpType::SEQ) {
          Simplified("flattened nested seq");
          const auto &[ces, cbody] = c->Seq();
          for (const Exp *cc : ces) {
            PushSeqs(cc, &vflat);
          }
          PushSeqs(cbody, &vflat);
        } else {
          PushSeqs(c, &vflat);
        }
      }
    }

    if ((opts & Simplification::O_FLATTEN) &&
        vflat.empty()) {
      Simplified("empty seq");
      return DoExp(body);
    } else {
      // The body could also be a Seq; then append the
      // sequences.
      const Exp *bbody = DoExp(body);
      if ((opts & Simplification::O_FLATTEN) &&
          bbody->type == ExpType::SEQ) {
        const auto &[fff, bbb] = bbody->Seq();
        for (const Exp *f : fff) {
          vflat.push_back(f);
        }
        return pool->Seq(vflat, bbb, guess);
      } else {
        return pool->Seq(vflat, bbody, guess);
      }
    }
  }

  void Simplified(const char *msg) {
    progress->Record(msg);
  }

private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
};

struct Knowledge {
  const Exp *value = nullptr;
  // In some cases (records) we need to do setup work to know the
  // value, so we only want to do that work if the value will be
  // used. This bit is modified in place when the knowledge is used.
  bool was_used = false;
};
using Known = FunctionalMap<std::string, std::shared_ptr<Knowledge>>;

// TODO: Could be useful to record known size of vectors.

// TODO: This could be a general 'known' pass.
struct KnownPass : public il::Pass<Known> {
  KnownPass(uint64_t opts, AstPool *p, ProgressRecorder *progress) :
    Pass(p),
    opts(opts),
    progress(progress) {}

  // This pass is designed to find code like this:
  // let val r = {lab1 = exp1, lab2 = exp2, ...}
  // in
  //   ... #lab1 r ...   ... #lab2 r ...
  // end
  //
  // and replace it with
  //
  // let
  //    val r_lab1 = exp1
  //    val r_lab2 = exp2
  // in
  //     r_lab1     ...   r_lab2
  // end
  //
  // This is particularly common with binops whose wrapper functions
  // have been inlined.

  const Exp *DoLet(const std::vector<std::string> &tyvars,
                   const std::string &x,
                   const Exp *rhs_in,
                   const Exp *body,
                   const Exp *guess,
                   Known known) override {
    // Is this a tuple binding?
    const Exp *rhs = DoExp(rhs_in, known);
    if ((opts & Simplification::O_EXPLODE_RECORDS) &&
        tyvars.empty() && rhs->type == ExpType::RECORD) {
      const auto &fields = rhs->Record();
      // If any of the record's fields are not valuable, pull them
      // out as intermediate variables. That way when processing the
      // body we can reduce #lab x without moving any effects.
      //
      // However, we need to avoid doing this transformation if
      // we're not actually going to use the record's fields (e.g.
      // if it only escapes). Otherwise we can get into infinite
      // loops where inlining will try to put them back. So we
      // provisionally create the knowledge, translate the body,
      // and then only actually do this if the knowledge was marked
      // as was_used.
      std::vector<std::pair<std::string, const Exp *>> bindings;
      std::vector<std::pair<std::string, const Exp *>> new_fields;
      for (const auto &[lab, exp] : fields) {
        if (IsSmallValue(exp)) {
          // Already okay
          new_fields.emplace_back(lab, exp);
        } else {
          std::string tmp = pool->NewVar(pool->BaseVar(x) + "_" +
                                         pool->BaseVar(lab));
          bindings.emplace_back(tmp, exp);
          new_fields.emplace_back(lab, pool->Var({}, tmp));
        }
      }

      const Exp *new_rhs = pool->Record(new_fields, rhs);

      auto knowledge = std::make_shared<Knowledge>(Knowledge{
          .value = new_rhs, .was_used = false});
      known = known.Insert(x, knowledge);

      // This sets knowledge->was_used if we need the fields to be
      // pulled out. Otherwise, the original rhs will suffice.
      const Exp *new_body = DoExp(body, known);

      if (knowledge->was_used) {
        // Here we use the new exploded record.
        const Exp *ret =
          pool->Let(tyvars, x, new_rhs, DoExp(body, known), guess);

        // And wrap with bindings, preserving evaluation order.
        for (int i = bindings.size() - 1; i >= 0; i--) {
          const auto &[var, arm_exp] = bindings[i];
          if (VERBOSE) {
            printf("  serialized record field " APURPLE("%s") " = ...\n",
                   var.c_str());
          }
          CHECK(tyvars.empty());
          ret = pool->Let({}, var, arm_exp, ret);
        }
        return ret;
      } else {
        // Don't use new_rhs, and so the bindings are also not
        // to be generated.
        return pool->Let(tyvars, x, rhs, new_body, guess);
      }

    } else {
      return pool->Let(tyvars, x, rhs, DoExp(body, known), guess);
    }
  }

  const Exp *DoProject(const std::string &s,
                       const Type *record_type,
                       const Exp *e_in,
                       const Exp *guess,
                       Known known) override {
    const Exp *e = DoExp(e_in, known);
    if ((opts & Simplification::O_EXPLODE_RECORDS) &&
        e->type == ExpType::VAR) {
      const auto &[tyvars, x] = e->Var();
      if (tyvars.empty()) {
        /*
        printf("Check #" ABLUE("%s") " " ACYAN("%s") "?\n",
               s.c_str(),
               x.c_str());
        */
        if (std::shared_ptr<Knowledge> *kv = known.FindPtr(x)) {
          const Exp *rec = (*kv)->value;
          CHECK(rec->type == ExpType::RECORD) << "Projection from a "
            "known value, but it's not a record! Field " << s <<
            " projected from var " << x << " but known exp:\n" <<
            ExpString(rec);

          // Always use the known value instead.
          // There may be cases where it's better to just
          // project from the record (this is cheap), but
          // it's probably a wash anyway. The hope here is
          // that we replace all of the uses of the record
          // and we don't have to allocate it at all.
          for (const auto &[lab, ee] : rec->Record()) {
            if (lab == s) {
              // Must mark it so that the requisite bindings are
              // generated at the site of the let!
              (*kv)->was_used = true;
              progress->Record("known record field");
              return ee;
            }
          }
          LOG(FATAL) << "Projection of field " << s << " from var " <<
            x << ". The value of x is known, and it is "
            "a record, but it doesn't have that field. Exp:\n" <<
            ExpString(rec);
          return nullptr;
        }
      }
    }
    return pool->Project(s, record_type, e, guess);
  }

 private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
};

// Inlines globals, or drops unused ones.
struct GlobalInlining {
  GlobalInlining(uint64_t opts, AstPool *pool, ProgressRecorder *progress) :
    opts(opts),
    pool(pool),
    progress(progress) {}

  Program Run(const Program &program) {
    Program out = program;
    // one for each global in the original program, and then one for
    // the body (empty key).
    std::unordered_map<std::string,
                       std::unordered_map<std::string, int>> mentioned;
    for (const Global &global : program.globals) {
      mentioned[global.sym] = ILUtil::LabelCounts(global.exp);
    }
    mentioned[""] = ILUtil::LabelCounts(program.body);

    for (int global_idx = 0;
         global_idx < (int)out.globals.size();
         /* in loop */) {
      const Global &global = out.globals[global_idx];
      // Get the total count of occurrences outside the symbol itself.
      int total_count = 0;
      for (const auto &[sym_, sym_count] : mentioned) {
        auto it = sym_count.find(global.sym);
        if (it != sym_count.end()) {
          total_count += it->second;
        }
      }

      // Note: O_GLOBAL_INLINING is not enabled after closure conversion.
      // We could support it, but we need to avoid substituting functions
      // since CALL takes a literal label.

      // TODO: Or if a small value?
      if (((opts & Simplification::O_GLOBAL_INLINING) && total_count == 1) ||
          ((opts & Simplification::O_GLOBAL_DEAD) && total_count == 0)) {
        progress->Record("drop/inline global");
        if (VERBOSE) {
          printf("  There were " AYELLOW("%d") " occurrences.\n",
                 total_count);
          printf("  Inlined sym is " APURPLE("%s") " = %s\n",
                 global.sym.c_str(), ExpStringShort(global.exp).c_str());
        }

        if (total_count > 0) {
          // Update all the globals in place.
          for (int i = 0; i < (int)out.globals.size(); i++) {
            if (i != global_idx) {
              out.globals[i].exp = ILUtil::SubstPolyExpForLabel(
                  pool, global.tyvars, global.exp, global.sym,
                  out.globals[i].exp);
            }
          }

          // And the body.
          out.body = ILUtil::SubstPolyExpForLabel(
              pool, global.tyvars, global.exp, global.sym,
              out.body);
        }

        // Now this global is unused.
        out.globals.erase(out.globals.begin() + global_idx);
        // And don't increment.
      } else {
        global_idx++;
      }
    }
    return out;
  }

 private:
  const uint64_t opts = 0;
  AstPool *pool = nullptr;
  ProgressRecorder *progress = nullptr;
};

struct FlattenLetPass : public il::Pass<> {
  FlattenLetPass(uint64_t opts, AstPool *p, ProgressRecorder *progress) :
    Pass(p),
    opts(opts),
    progress(progress) {
  }

  virtual const Exp *DoLet(const std::vector<std::string> &tyvars,
                           const std::string &x,
                           const Exp *rhs_in,
                           const Exp *body_in,
                           const Exp *guess) {
    const Exp *rhs = DoExp(rhs_in);
    const Exp *body = DoExp(body_in);

    // Transform
    //   let val x = let val y = e1 in e2 end
    //   in body
    //   end
    // into
    //   let val y' = e1
    //   in let val x = [y'/y]e2
    //      in body
    //      end
    //   end
    // This is more idiomatic and better because we can apply
    // optimizations to the binding of x now (like known-value).
    if ((opts & Simplification::O_FLATTEN_LET) &&
        tyvars.empty() &&
        rhs->type == ExpType::LET) {
      const auto &[ytyvars, y, e1, e2] = rhs->Let();
      // Need to alpha-vary y in e2, in case the name y is also used
      // in 'body'.
      const auto &[new_y, new_e2] =
          ILUtil::AlphaVaryExp(pool, (int)ytyvars.size(), y, e2);
      progress->Record("flatten let");
      if (VERBOSE) {
        printf("  on " APURPLE("%s") " and "
               APURPLE("%s") " -> " APURPLE("%s") "\n",
               x.c_str(), y.c_str(), new_y.c_str());
      }
      return pool->Let(ytyvars, new_y, e1,
                       pool->Let(tyvars, x, new_e2, body));
    }
    return pool->Let(tyvars, x, rhs, body, guess);
  }

 private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
};


// There is just one value of type unit, and it has no elimination
// form. So we never need to store this type in records, for
// example. One very common occurrence is in environments generated
// by closure conversion.
//
// FIXME: This doesn't work correctly. There may be a simple bug,
// but I think there's also a deeper problem here, where (due to
// polymorphism) we can't necessarily get a handle on all of the
// unit fields in records globally. We could apply this locally
// (not as useful) or we could monomorphize the program. That would
// be nice for other transformations, but it is an ordeal!
struct EraseUnitPass : public il::TypedPass<> {
  EraseUnitPass(uint64_t opts, AstPool *p, ProgressRecorder *progress) :
    TypedPass(p),
    opts(opts),
    progress(progress) {
  }

  static bool IsUnit(const Type *t) {
    return t->type == TypeType::RECORD && t->Record().empty();
  }

  const Type *DoRecordType(
      Context G,
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess) override {
    std::vector<std::pair<std::string, const Type *>> vv;
    vv.reserve(v.size());
    for (const auto &[lab, t] : v) {
      const Type *tt = DoType(G, tt);
      if (IsUnit(tt)) {
        progress->Record("removed unit from record type");
      } else {
        vv.emplace_back(lab, tt);
      }
    }
    return pool->RecordType(vv, guess);
  }

  // Now the introduction and elimination forms for records.
  virtual std::pair<const Exp *, const Type *>
  DoRecord(
      Context G,
      const std::vector<std::pair<std::string, const Exp *>> &lv,
      const Exp *guess) override {
    std::vector<std::pair<std::string, const Exp *>> lvv;
    lvv.reserve(lv.size());
    std::vector<std::pair<std::string, const Type *>> ts;

    std::vector<const Exp *> pending_seq;

    for (const auto &[l, e] : lv) {
      const auto &[ee, tt] = DoExp(G, e);
      if (IsUnit(tt)) {
        progress->Record("removed unit-type field from record exp");
        if (VERBOSE) {
          printf("  label was " APURPLE("%s") "\n", l.c_str());
        }
        // We still need to sequence the expression in case it
        // has effects, though.
        pending_seq.push_back(ee);
      } else {

        // If we removed an earlier field because it's unit type,
        // we emit it at the next opportunity, which is the next
        // field we process (or afterwards, below).
        if (pending_seq.empty()) {
          lvv.emplace_back(l, ee);
        } else {
          lvv.emplace_back(l, pool->Seq(pending_seq, ee));
          pending_seq.clear();
          ts.emplace_back(l, tt);
        }
      }
    }

    // If the tail of the record is unit-type expressions, then we
    // still need to execute them, but we didn't have another expression
    // to sequence them before. So we generate
    // let tmp = { .. record exp .. }
    // in seq (pending, tmp)
    // end

    if (pending_seq.empty()) {
      return {pool->Record(lvv, guess), pool->RecordType(ts)};
    } else {
      std::string tmp = pool->NewVar("tmp");
      return {
        pool->Let({}, tmp, pool->Record(lvv, guess),
                  pool->Seq(pending_seq, pool->Var({}, tmp))),
        pool->RecordType(ts),
      };
    }
  }

  std::pair<const Exp *, const Type *>
  DoProject(Context G,
            const std::string &s, const Type *record_type, const Exp *e,
            const Exp *guess) override {
    // Find the label in the original type.
    CHECK(record_type->type == TypeType::RECORD);
    for (const auto &[l, t] : record_type->Record()) {
      if (l == s) {
        if (IsUnit(t)) {
          // Then don't actually use the record since we know what the
          // value is. Still execute the projection expression for
          // effect though.
          const auto &[ee, tt] = DoExp(G, e);
          CHECK(tt->type == TypeType::RECORD);
          for (const auto &[ll, tt_] : tt->Record()) {
            CHECK(ll != l) << "Expected the field to be dropped from "
              "the record, since it has unit type.";
          }

          return {pool->Seq({e}, pool->Record({})), pool->RecordType({})};
        } else {
          return TypedPass::DoProject(G, s, record_type, e, guess);
        }
      }
    }

    LOG(FATAL) << "Internal type error: Label " << s << " not found in " <<
      TypeString(record_type);
    return {nullptr, nullptr};
  }


  Program DoProgram(Context G, const Program &program) override {
    if (!(opts & Simplification::O_ERASE_UNIT)) return program;
    return TypedPass::DoProgram(G, program);
  }

 private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
};


// A datatype declaration that is just of the form A | B | C ...
// is an "enum." We recognize these and represent them as
// words. This is pretty easy with the typed pass.
struct RepresentEnumsPass : public il::TypedPass<> {
  RepresentEnumsPass(uint64_t opts, AstPool *p, ProgressRecorder *progress) :
    TypedPass(p),
    opts(opts),
    progress(progress) {
  }

  // Types of the form
  // (μ α. [A: unit, B: unit, C: unit, ...])
  static std::optional<std::vector<std::string>> GetEnum(const Type *t) {
    t = ILUtil::GetKnownType("represent-enums", t);
    if (t->type != TypeType::MU) return std::nullopt;
    const auto &[idx, bundles] = t->Mu();
    // If we want to support this for mutually recursive datatypes,
    // start by breaking them into singletons (they are not really
    // recursive if they're enums!).
    if (bundles.size() != 1) return std::nullopt;

    const auto &[a, ttb] = bundles[0];
    const Type *tt = ILUtil::GetKnownType("represent-enums-bundle", ttb);
    if (tt->type != TypeType::SUM)
      return std::nullopt;

    std::vector<std::string> labs;
    for (const auto &[lab, u] : tt->Sum()) {
      const Type *uu = ILUtil::GetKnownType("represent-enums-record", u);
      if (uu->type != TypeType::RECORD)
        return std::nullopt;
      if (!uu->Record().empty())
        return std::nullopt;
      labs.push_back(lab);
    }

    std::sort(labs.begin(), labs.end());

    return labs;
  }

  const Type *DoMu(
      Context G,
      int idx,
      const std::vector<std::pair<std::string, const Type *>> &v,
      const Type *guess) override {

    const Type *mu = TypedPass::DoMu(G, idx, v, guess);

    if (GetEnum(mu).has_value()) {
      progress->Record("enum: rewrote type");
      if (VERBOSE) {
        printf("   type rewritten from %s -> word\n",
               TypeString(mu).c_str());
      }
      return pool->WordType();
    } else {
      return mu;
    }
  }

  // Now need to handle the introduction and elimination forms
  // for the mu.
  //
  // we recognize the expected form, which is
  //  roll<μ α. [A: unit, B: unit, C: unit, ...]>(inject A = {})
  //  =>
  //  (word constant representing A)
  // but in pathological cases where the roll and sum don't
  // appear together, we need to transform it into a word. So
  // we can do a sumcase:
  //  roll<μ α. [A: unit, B: unit, C: unit, ...]>(exp)
  //  =>
  //  sumcase exp of
  //     A => (word constant for A)
  //   | B => (word constant for B)
  //   | C => (word constant for C)
  //
  // and in fact we can just always perform that transformation
  // and let sumcase reduction fix it for us when exp is an inject.

  std::pair<const Exp *, const Type *>
  DoRoll(Context G,
         const Type *t,
         const Exp *e,
         const Exp *guess) override {
    if (const auto &olabs = GetEnum(t)) {
      // We don't actually need to do this (word is a leaf), but
      // something is amiss if it doesn't get translated the way we
      // expect!
      const Type *wt = DoType(G, t);
      CHECK(wt->type == TypeType::WORD);

      const std::vector<std::string> &labs = olabs.value();

      std::vector<std::tuple<std::string, std::string, const Exp *>> arms;
      arms.reserve(labs.size());

      // Variable is always bound to (), so unused.
      std::string v = "unused";
      for (int i = 0; i < (int)labs.size(); i ++) {
        arms.emplace_back(labs[i], v, pool->Word(i));
      }

      const auto &[ee, tt] = DoExp(G, e);
      CHECK(tt->type == TypeType::SUM) << "Since we matched an enum "
        "type for the roll, the translated expression must be a sum "
        "(or else the roll would have been ill-formed).";

      const Exp *def = pool->Fail(
          pool->String("Impossible! Optimization opportunity!"),
          pool->WordType());

      progress->Record("enum: rewrote roll");
      return {pool->SumCase(ee, arms, def), pool->WordType()};

    } else {
      return TypedPass::DoRoll(G, t, e, guess);
    }
  }


  const Type *SumType(const std::vector<std::string> &labs) {
    std::vector<std::pair<std::string, const Type *>> arms;
    arms.reserve(labs.size());
    for (const std::string &lab : labs) {
      arms.emplace_back(lab, pool->RecordType({}));
    }
    return pool->SumType(std::move(arms));
  }

  // This is unroll (e) where e may have an enum type.
  // The result of the expression was an unrolled sum,
  // so we just need to produce that from the word (using
  // wordcase) if so.

  std::pair<const Exp *, const Type *>
  DoUnroll(Context G,
           const Exp *e,
           const Type *mu_type,
           const Exp *guess) override {
    // This is why we have an annotation on the unroll: So that
    // we can detect that we have transformed the body (yet
    // recover the labels).
    if (const std::optional<std::vector<std::string>> olabs =
        GetEnum(mu_type)) {

      const auto &[ee, tt] = DoExp(G, e);
      const Type *mu_tt = DoType(G, mu_type);
      // So the type must be translated as .
      CHECK(tt->type == TypeType::WORD &&
            mu_tt->type == TypeType::WORD) << "Exp was:\n" <<
        ExpString(e) << "\nwhich became\n" << ExpString(ee) <<
        "\nof type\n" << TypeString(tt) <<
        "\nType annotation was:\n" <<
        TypeString(mu_type) << "\nwhich became\n" <<
        TypeString(mu_tt);

      const auto &labs = olabs.value();
      const Type *sum_type = SumType(labs);
      std::vector<std::pair<uint64_t, const Exp *>> arms;
      arms.reserve(labs.size());
      for (int i = 0; i < (int)labs.size(); i++) {
        arms.emplace_back(
            (uint64_t)i, pool->Inject(labs[i], sum_type, pool->Record({})));
      }
      const Exp *def = pool->Fail(
          pool->String("Impossible! Optimization opportunity!"),
          sum_type);

      progress->Record("enum: rewrote unroll");
      if (VERBOSE) {
        printf("(simplification:DoUnroll) returning %s\n",
               TypeString(sum_type).c_str());
      }

      return {pool->WordCase(ee, arms, def), sum_type};

    } else {
      return TypedPass::DoUnroll(G, e, mu_type, guess);
    }
  }

  Program DoProgram(Context G, const Program &program) override {
    if (!(opts & Simplification::O_REPRESENT_ENUMS)) return program;

    // This optimization only needs to run once, since it will find
    // all such types in the program and translate them in one pass.
    // If we add transformations that somehow introduce new (mu-sum)
    // enums, then it would be fine to run this multiple times.
    has_run = true;
    return TypedPass::DoProgram(G, program);
  }

  bool HasRun() const {
    return has_run;
  }

 private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
  bool has_run = false;
};



struct DecomposePass : public il::Pass<> {
  DecomposePass(uint64_t opts, AstPool *p, ProgressRecorder *progress) :
    Pass(p),
    opts(opts),
    progress(progress) {
  }

  // Decompose an intcase into a series of primop tests.
  const Exp *DoIntCase(
      const Exp *obj,
      const std::vector<std::pair<BigInt, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if (!(opts & Simplification::O_DECOMPOSE_INTCASE))
      return Pass::DoIntCase(obj, arms, def, guess);

    std::string objvar = pool->NewVar("intcase_obj");
    const Exp *objvarexp = pool->Var({}, objvar);

    const Exp *body = DoExp(def);
    // PERF: If there are many cases, we should at least do
    // binary search.
    for (const auto &[bi, e] : arms) {
      body = pool->If(pool->Primapp(Primop::INT_EQ,
                                    {}, {pool->Int(bi), objvarexp}),
                      DoExp(e),
                      body);
    }

    progress->Record("decompose intcase");
    return pool->Let({}, objvar, DoExp(obj),
                     body);
  }

  // Same, for wordcase.
  const Exp *DoWordCase(
      const Exp *obj,
      const std::vector<std::pair<uint64_t, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if (!(opts & Simplification::O_DECOMPOSE_WORDCASE))
      return Pass::DoWordCase(obj, arms, def, guess);

    std::string objvar = pool->NewVar("wordcase_obj");
    const Exp *objvarexp = pool->Var({}, objvar);

    const Exp *body = DoExp(def);
    // PERF: If there are many cases, we should at least do
    // binary search. Jump tables would be nice too, but then
    // we should probably just persist the wordcase construct!
    for (const auto &[w, e] : arms) {
      body = pool->If(pool->Primapp(Primop::WORD_EQ,
                                    {}, {pool->Word(w), objvarexp}),
                      DoExp(e),
                      body);
    }

    progress->Record("decompose wordcase");
    return pool->Let({}, objvar, DoExp(obj),
                     body);
  }

  // Same, for StringCase.
  const Exp *DoStringCase(
      const Exp *obj,
      const std::vector<std::pair<std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {
    if (!(opts & Simplification::O_DECOMPOSE_STRINGCASE))
      return Pass::DoStringCase(obj, arms, def, guess);

    std::string objvar = pool->NewVar("strcase_obj");
    const Exp *objvarexp = pool->Var({}, objvar);

    const Exp *body = DoExp(def);
    // PERF: If there are many cases, we should at least do
    // binary search. For stringcase there could be many tricks,
    // like checking specific informative characters in the
    // strings.
    for (const auto &[str, e] : arms) {
      body = pool->If(pool->Primapp(Primop::STRING_EQ,
                                    {}, {pool->String(str), objvarexp}),
                      DoExp(e),
                      body);
    }

    progress->Record("decompose stringcase");
    return pool->Let({}, objvar, DoExp(obj),
                     body);
  }

 private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
};

/*
  Transform situations like the following:

  case (case e of A => C ea | B => D eb | w => def1) of
     C x => ec
   | D y => ed
   | z => def2

   into

  case e of
    A => let x = ea in ec end
  | B => let y = eb in ed end
  | w => let z = def1 in def2 end

  The main purpose is to find such an immediate reduction after
  rewriting a case analysis on enums, which produce this pattern.
  But it makes sense in general. The main problem is that there
  are many different case constructs to combine! We specifically
  look for sumcase (wordcase ...).

*/

struct CaseOfCasePass : public il::TypedPass<> {
  CaseOfCasePass(uint64_t opts, AstPool *p, ProgressRecorder *progress) :
    TypedPass(p),
    opts(opts),
    progress(progress) {
  }

  const Type *DoType(Context G, const Type *t) override {
    // Types are not changed.
    return t;
  }

  std::pair<const Exp *, const Type *>
  DoSumCase(
      Context G,
      const Exp *obj,
      const std::vector<
          std::tuple<std::string, std::string, const Exp *>> &arms,
      const Exp *def,
      const Exp *guess) override {

    if ((opts & Simplification::O_CASE_OF_CASE) &&
        obj->type == ExpType::WORDCASE) {

      // We require that the arms are all INJECT or FAIL, since
      // for sure these will reduce. There are lots of other
      // things we could do here; for example if the body is
      // let ... in INJECT _ end we can also reduce it.
      auto AllSuitable = [&]() {
          const auto &[wobj_, warms, wdef] = obj->WordCase();
          for (const auto &[w, e] : warms) {
            if (e->type != ExpType::INJECT && e->type != ExpType::FAIL) {
              return false;
            }
          }
          return wdef->type == ExpType::INJECT || wdef->type == ExpType::FAIL;
        };

      if (AllSuitable()) {
        // Then we can transform it. The inner case, with its original arms,
        // becomes the outer case.

        // Translate the wordcase. We need its type to extract the sum
        // type.
        const auto &[oo, oot] = DoExp(G, obj);
        CHECK(oo->type == ExpType::WORDCASE) << "This pass shouldn't "
          "transform a wordcase into anything else.";
        // (Similarly, the INJECT and FAIL expressions in the arms
        // should be translated to the same.)
        CHECK(oot->type == TypeType::SUM) << "Internal type error: "
          "sumcase on a non-sum! " << TypeString(oot);
        std::unordered_map<std::string, const Type *> arm_types;
        for (const auto &[lab, t] : oot->Sum()) {
          arm_types[lab] = t;
        }

        const auto &[wobj, warms, wdef] = obj->WordCase();

        // Since sumcase arms could be duplicated, we hoist them as
        // functions.
        // hoisted fn name, arg var, arrow type, body type
        struct Hoist {
          int used = 0;
          std::string name;
          std::string x;
          const Type *arrow_type;
          const Exp *body;
        };

        // Translate the sumcase's default, which we use to get the
        // return type of the sumcase.
        const auto &[dd, sumcase_return_type] = DoExp(G, def);

        Hoist def_hoist;
        def_hoist.name = pool->NewVar("def");
        def_hoist.x = pool->NewVar("unused");
        def_hoist.arrow_type = pool->Arrow(pool->RecordType({}),
                                           sumcase_return_type);
        def_hoist.body = dd;

        std::map<std::string, Hoist> hoists;
        for (const auto &[lab, arg, body] : arms) {
          CHECK(arm_types.contains(lab)) << "Internal type error: "
            "Label " << lab << " missing in sum type? " << TypeString(oot);
          const Type *arm_type = arm_types[lab];
          Hoist *h = &hoists[lab];
          h->name = pool->NewVar(lab);
          h->x = arg;
          const auto &[bb, bbt] =
            DoExp(G.Insert(arg, PolyType{{}, arm_type}), body);
          h->body = bb;
          h->arrow_type = pool->Arrow(arm_type, bbt);
        }

        auto ArmExp = [this, &hoists, &def_hoist](const Exp *wexp) {
            if (wexp->type == ExpType::FAIL) {
              // TODO: Now that we are using a typed pass, it would
              // not be hard to change the type annotation on the fail
              // here.
              // If the arm fails, the original outer sumcase is never
              // reached. So we'd like to just leave this alone. But
              // the fail will need to have its return type annotation
              // changed, so we do a faithful rewrite that sequences
              // the fail with the default of the sumcase (could use
              // any arm). We rely on other simplifications to throw
              // away the code after the fail and change its type.
              def_hoist.used++;
              return pool->Seq({wexp},
                               pool->App(pool->Var({}, def_hoist.name),
                                         pool->Record({})));
            } else {
              CHECK(wexp->type == ExpType::INJECT) << "Checked above.";
              const auto &[lab, t_, earg] = wexp->Inject();
              // Find the corresponding arm in the outer sumcase.
              auto it = hoists.find(lab);
              if (it != hoists.end()) {
                Hoist &hoist = it->second;
                // Call the hoisted function. Typically it will get
                // inlined if it's just a single use.
                hoist.used++;
                return pool->App(pool->Var({}, hoist.name), earg);
              }

              // If we didn't find an explicit match, then this is the
              // default.
              def_hoist.used++;
              return pool->App(pool->Var({}, def_hoist.name),
                               pool->Record({}));
            }
          };

        std::vector<std::pair<uint64_t, const Exp *>> new_warms;
        for (const auto &[w, e] : warms) {
          const auto &[ww, tt] = DoExp(G, e);
          new_warms.emplace_back(w, ArmExp(ww));
        }

        const auto &[dww, dtt] = DoExp(G, wdef);
        const Exp *new_wdef = ArmExp(dww);

        const auto &[new_wobj, new_wobjt] = DoExp(G, wobj);

        progress->Record("Rewrote sumcase(wordcase()).");

        const Exp *ret = pool->WordCase(new_wobj,
                                        std::move(new_warms),
                                        new_wdef);

        // And we must wrap it with the hoisted functions (if used).
        auto WrapHoist = [this](const Hoist &hoist, const Exp *exp) {
            if (hoist.used > 0) {
              return pool->Let({}, hoist.name,
                               pool->Fn("", hoist.x, hoist.arrow_type,
                                        hoist.body),
                               exp);
            } else {
              return exp;
            }
          };

        ret = WrapHoist(def_hoist, ret);
        for (const auto &[lab_, hoist] : hoists) {
          ret = WrapHoist(hoist, ret);
        }

        // Wrapping is just adding let bindings, which don't
        // change the return type.
        return std::make_pair(ret, sumcase_return_type);
      }

      // Otherwise, fall through.
    }

    return TypedPass::DoSumCase(G, obj, arms, def, guess);
  }


 private:
  const uint64_t opts = 0;
  ProgressRecorder *progress = nullptr;
};

}  // namespace

Program Simplification::Simplify(const Program &program_in,
                                 uint64_t opts) {
  ProgressRecorder progress;
  Program program = program_in;
  DecomposePass decompose(opts, pool, &progress);
  PeepholePass peephole(opts, pool, &progress);
  KnownPass known(opts, pool, &progress);
  FlattenLetPass flatten_let(opts, pool, &progress);
  GlobalInlining global_inlining(opts, pool, &progress);
  RepresentEnumsPass represent_enums(opts, pool, &progress);
  CaseOfCasePass case_of_case(opts, pool, &progress);
  EraseUnitPass erase_unit(opts, pool, &progress);

  // Optimizations that are disabled due to bugs, etc.
  constexpr uint64_t DISABLED_OPTIMIZATIONS = O_ERASE_UNIT;

  opts &= ~DISABLED_OPTIMIZATIONS;

  // Do decomposition first if enabled. This only needs
  // to be done once, since other passes should not reintroduce
  // these (we might want to be explicit about that?).
  constexpr uint64_t ANY_DECOMPOSE =
    O_DECOMPOSE_INTCASE | O_DECOMPOSE_WORDCASE | O_DECOMPOSE_STRINGCASE;

  if (opts & ANY_DECOMPOSE) {
    program = decompose.DoProgram(program);
  }

  do {
    progress.Reset();
    if (VERBOSE) printf(AWHITE("Peephole") ".\n");
    program = peephole.DoProgram(program);

    constexpr uint64_t ANY_KNOWN =
      O_EXPLODE_RECORDS;

    if (opts & ANY_KNOWN) {
      if (VERBOSE) printf(AWHITE("Known") ".\n");
      program = known.DoProgram(program, Known());
    }

    if ((opts & O_REPRESENT_ENUMS) != 0 && !represent_enums.HasRun()) {
      if (VERBOSE) {
        printf(AWHITE("Represent enums") ".\n");
        // printf("%s\n", ProgramString(program).c_str());
      }

      Context G;
      program = represent_enums.DoProgram(G, program);
    }

    if ((opts & O_ERASE_UNIT) != 0) {
      if (VERBOSE) {
        printf(AWHITE("Erase unit") ".\n");
        // printf("%s\n", ProgramString(program).c_str());
      }

      Context G;
      program = erase_unit.DoProgram(G, program);
    }

    if (opts & O_CASE_OF_CASE) {
      if (VERBOSE) {
        printf(AWHITE("Case of case") ".\n");
        printf("%s\n", ProgramString(program).c_str());
      }
      Context G;
      program = case_of_case.DoProgram(G, program);
    }

    if (opts & O_FLATTEN_LET) {
      if (VERBOSE) printf(AWHITE("Flatten let") ".\n");
      program = flatten_let.DoProgram(program);
    }

    constexpr uint64_t ANY_GLOBAL =
      O_GLOBAL_INLINING |
      O_GLOBAL_DEAD;

    if (opts & ANY_GLOBAL) {
      if (VERBOSE) printf(AWHITE("Global") ".\n");
      program = global_inlining.Run(program);
    }

    if (VERBOSE) {
      printf("\n" AYELLOW("After simplification:\n"));
      printf("%s\n", ProgramString(program).c_str());
    }

  } while (progress.MadeProgress());

  return program;
}

}  // namespace il
