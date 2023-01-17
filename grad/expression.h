
#ifndef _GRAD_EXPRESSION_H
#define _GRAD_EXPRESSION_H

#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <mutex>
#include <map>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "half.h"
#include "util.h"

using half_float::half;

// Expression of one variable.

enum ExpType {
  VAR,
  PLUS_C,
  TIMES_C,
  PLUS_E,
};

struct Exp {
  ExpType type;
  uint16_t c = 0x0000;
  // Plus_c and Times_c can be iterated.
  uint16_t iters = 1;
  const Exp *a = nullptr, *b = nullptr;

  // Thread-safe allocator. When the allocator goes out of scope,
  // all allocated expressions within are deleted.
  struct Allocator {

    Allocator() {
      var = New(VAR);
    }

    // TODO: Some way to release/copy an expression!
    ~Allocator() {
      for (Exp *e : allocations) delete e;
      allocations.clear();
    }

    const Exp *Copy(const Exp *other) {
      switch (other->type) {
      case VAR: return Var();
      case PLUS_C: return PlusC(Copy(other->a), other->c, other->iters);
      case TIMES_C: return TimesC(Copy(other->a), other->c, other->iters);
      case PLUS_E: return PlusE(Copy(other->a), Copy(other->b));
      default:
        CHECK(false) << "unknown type";
        return nullptr;
      }
    }

    // TODO: Could index variable (using c, perhaps) to allow for
    // linear functions of multiple variables.
    const Exp *Var() {
      return var;
    }

    const Exp *PlusC(const Exp *e, uint16_t c, uint16_t iters = 1) {
      Exp *ret = New(PLUS_C);
      ret->a = e;
      ret->c = c;
      ret->iters = iters;
      return ret;
    }

    const Exp *TimesC(const Exp *e, uint16_t c, uint16_t iters = 1) {
      Exp *ret = New(TIMES_C);
      ret->a = e;
      ret->c = c;
      ret->iters = iters;
      return ret;
    }

    // TODO: Verify that this is equivalent to unary negation.
    const Exp *Neg(const Exp *e) {
      Exp *ret = New(TIMES_C);
      ret->a = e;
      ret->c = 0xbc00;  // -1.0
      return ret;
    }

    const Exp *PlusE(const Exp *a, const Exp *b) {
      Exp *ret = New(PLUS_E);
      ret->a = a;
      ret->b = b;
      return ret;
    }

  private:
    inline Exp *New(ExpType t) {
      Exp *e = new Exp(t);
      {
        std::unique_lock<std::mutex> ml(m);
        allocations.push_back(e);
      }
      return e;
    }
    Exp *var = nullptr;
    std::mutex m;
    std::vector<Exp *> allocations;
  };

  static inline half GetHalf(uint16_t u) {
    half h;
    static_assert(sizeof (h) == sizeof (u));
    memcpy((void*)&h, (void*)&u, sizeof (u));
    return h;
  }

  static inline uint16_t GetU16(half h) {
    uint16_t u;
    static_assert(sizeof (h) == sizeof (u));
    memcpy((void*)&u, (void*)&h, sizeof (u));
    return u;
  }

  using Table = std::array<uint16_t, 65536>;

  struct TimesTable {
    TimesTable(uint16_t cu, int iters) {
      // PERF this can be computed by squaring, right?
      // (And we can certainly compute multiple tables at once.)
      half c = GetHalf(cu);
      for (int i = 0; i < 65536; i++) {
        uint16_t xu = i;
        half y = GetHalf(xu);
        for (int z = 0; z < iters; z++)
          y *= c;
        table[xu] = GetU16(y);
      }
    }
    Table table;
  };

  static uint16_t CachedTimes(uint16_t lhs, uint16_t c, int iters) {
    static std::mutex times_table_m;
    static std::map<uint16_t, std::pair<const TimesTable *,
                                        const TimesTable *>> times_tables;

    const TimesTable *table100 = nullptr, *table10 = nullptr;
    {
      std::unique_lock<std::mutex> ml(times_table_m);
      auto it = times_tables.find(c);
      if (it != times_tables.end()) {
        std::tie(table100, table10) = it->second;
      } else {
        // PERF could relinquish lock for this slow init
        table100 = new TimesTable(c, 100);
        table10 = new TimesTable(c, 10);
        times_tables[c] = make_pair(table100, table10);
      }
    }

    while (iters > 100) {
      lhs = table100->table[lhs];
      iters -= 100;
    }

    while (iters > 10) {
      lhs = table10->table[lhs];
      iters -= 10;
    }

    half y = GetHalf(lhs);
    const half hc = GetHalf(c);
    for (int z = 0; z < iters; z++)
      y *= hc;
    return GetU16(y);
  }

  // Non-recursive version that explicitly manages its stack,
  // but it takes twice as long, at least for smaller
  // expressions. :(
  static uint16_t EvaluateOnExplicit(const Exp *exp, uint16_t x) {

    enum FrameType : uint8_t {
      FPLUSC,
      FTIMESC,
      FPLUSE1,
      FPLUSE2,
    };

    enum Mode {
      // with v = return value.
      MODE_RETURN,
      // with e = expression to evaluate
      MODE_EVAL,
    };

    // 0-65535 are values
    struct Frame {
      union {
        struct {
          uint16_t c;
          uint16_t iters;
        } pt;
        const Exp *other;
      } u;
      FrameType type;
    };

    // state
    Mode mode = MODE_EVAL;
    std::vector<Frame> stack;
    const Exp *e = exp;
    uint16_t v = 0;

    for (;;) {
      switch (mode) {

      case MODE_EVAL:
        switch (e->type) {
        case VAR:
          v = x;
          mode = MODE_RETURN;
          break;

        case PLUS_C: {
          Frame f;
          f.type = FPLUSC;
          f.u.pt.c = e->c;
          f.u.pt.iters = e->iters;
          stack.push_back(f);
          e = e->a;
          mode = MODE_EVAL;
          break;
        }

        case TIMES_C: {
          Frame f;
          f.type = FTIMESC;
          f.u.pt.c = e->c;
          f.u.pt.iters = e->iters;
          stack.push_back(f);
          e = e->a;
          mode = MODE_EVAL;
          break;
        }

        case PLUS_E: {
          Frame f;
          f.type = FPLUSE1;
          f.u.other = e->b;
          stack.push_back(f);
          e = e->a;
          mode = MODE_EVAL;
          break;
        }

        default:
          CHECK(false) << "bad exp type";
          return 0;
        }
        break;

      case MODE_RETURN: {
        if (stack.empty())
          return v;

        Frame &f = stack.back();
        switch (f.type) {
        case FPLUSC: {
          half res = GetHalf(v);
          half rhs = GetHalf(f.u.pt.c);
          for (int i = 0; i < f.u.pt.iters; i++)
            res += rhs;
          v = GetU16(res);
          stack.pop_back();
          mode = MODE_RETURN;
          break;
        }

        case FTIMESC: {
          if (f.u.pt.iters >= 10) {
            v = CachedTimes(v, f.u.pt.c, f.u.pt.iters);
          } else {
            half res = GetHalf(v);
            half rhs = GetHalf(f.u.pt.c);
            for (int i = 0; i < f.u.pt.iters; i++)
              res *= rhs;
            v = GetU16(res);
          }
          stack.pop_back();
          mode = MODE_RETURN;
          break;
        }

        case FPLUSE1: {
          e = f.u.other;
          f.type = FPLUSE2;
          f.u.pt.c = v;
          mode = MODE_EVAL;
          break;
        }

        case FPLUSE2: {
          half lhs = GetHalf(f.u.pt.c);
          half rhs = GetHalf(v);
          v = GetU16(lhs + rhs);
          stack.pop_back();
          mode = MODE_RETURN;
          break;
        }

        default:
          CHECK(false) << "bad frame";
          return 0;
        }
      }

      }
    }
  }

  static uint16_t EvaluateOn(const Exp *e, uint16_t x) {
    switch (e->type) {
    case VAR: return x;
    case PLUS_C: {
      half res = GetHalf(EvaluateOn(e->a, x));
      half rhs = GetHalf(e->c);
      // TODO PERF: Cache plus table too?
      for (int i = 0; i < e->iters; i++)
        res += rhs;
      return GetU16(res);
    }
    case TIMES_C: {
      uint16_t lhs = EvaluateOn(e->a, x);

      if (e->iters >= 10) {
        return CachedTimes(lhs, e->c, e->iters);
      }

      half res = GetHalf(lhs);
      half rhs = GetHalf(e->c);
      for (int i = 0; i < e->iters; i++)
        res *= rhs;
      return GetU16(res);
    }
    case PLUS_E:
      return GetU16(GetHalf(EvaluateOn(e->a, x)) +
                    GetHalf(EvaluateOn(e->b, x)));
    default:
      CHECK(false) << "Unknown expression type";
      return 0;
    }
  }

  static Table TabulateExpression(const Exp *e) {
    Table ret;
    // PERF: Can skip running on nans.
    for (int x = 0; x < 65536; x++) {
      uint16_t y = EvaluateOn(e, x);
      ret[x] = y;
    }
    return ret;
  }

  // Always towards more positive numbers.
  // Unlike nextafter, this includes -0 and 0, in that order.
  // (Supposedly std::nextafter raises FE_INEXACT and FE_UNDERFLOW
  // if the result is subnormal or zero??)
  //
  // Remember to test pos16 != high16, not pos16 < high16 (the u16s
  // are not ordered like floats).
  //
  // Don't use this on nans, but what it actually does today is
  // consider all the different NaNs to be signed, ordered and larger
  // in magnitude than the corresponding infinities. This means that
  // if you don't stop, you eventually get positive infinity and then
  // "positive" nans, before wrapping around to "negative" nans.
  static inline NextAfter16(uint16_t pos) {
    // Zero comes immediately after -0.
    if (pos == 0x8000) return 0x0000;
    else if (pos > 0x8000) return pos - 1;
    else return pos + 1;
  }

  // Only fills the table in the range [low, high].
  static Table TabulateExpressionIn(const Exp *e,
                                    half low, half high) {
    Table ret;
    CHECK(low < high);
    uint16_t ulow = GetU16(low);
    uint16_t uhigh = GetU16(high);
    for (uint16 upos = ulow; upos != uhigh; upos = NextAfter16(upos)) {
      ret[upos] = EvaluateOn(e, upos);
    }
    // include endpoint.
    ret[uhigh] = EvaluateOn(e, uhigh);

    return ret;
  }

  static Table MakeTableFromFn(const std::function<half(half)> &f) {
    Table table;
    for (int i = 0; i < 65536; i++) {
      half x = Exp::GetHalf((uint16)i);
      half y = f(x);
      table[i] = Exp::GetU16(y);
    }
    return table;
  }

  // Returns true if the expressions are syntactically equal.
  static bool Eq(const Exp *a, const Exp *b) {
    if (a->type != b->type) return false;
    switch (a->type) {
    case VAR: return true;
    case PLUS_C:
    case TIMES_C:
      return a->c == b->c && a->iters == b->iters && Eq(a->a, b->a);
    case PLUS_E:
      return Eq(a->a, b->a) && Eq(a->b, b->b);
    default:
      CHECK(false) << "bad exp type";
      return false;
    }
  }

  // [xe/x]e
  static const Exp *Subst(Allocator *alloc,
                          const Exp *e, const Exp *xe) {
    // Note that variables are the only leaves, so there's
    // currently no point in trying to avoid reallocations.
    std::function<const Exp *(const Exp *)> Rec =
      std::function<const Exp *(const Exp *)>(
          [alloc, xe, &Rec](const Exp *e) -> const Exp * {
          switch (e->type) {
          case VAR:
            return xe;
          case PLUS_C:
            return alloc->PlusC(Rec(e->a),
                                e->c,
                                e->iters);
          case TIMES_C:
            return alloc->TimesC(Rec(e->a),
                                 e->c,
                                 e->iters);
          case PLUS_E:
            return alloc->PlusE(Rec(e->a), Rec(e->b));
          default:
            CHECK(false) << "Unknown expression type";
            return e;
          }
        });
    return Rec(e);
  }

  static std::string Serialize(const Exp *e) {
    std::string out;
    std::function<void(const Exp *)> Rec =
      std::function<void(const Exp *)>(
        [&out, &Rec](const Exp *e) {
          CHECK(e != nullptr);
          switch (e->type) {
          case VAR:
            StringAppendF(&out, " V");
            return;

          case PLUS_C:
            Rec(e->a);
            StringAppendF(&out, " P%04x%d", e->c, e->iters);
            return;

          case TIMES_C:
            Rec(e->a);
            StringAppendF(&out, " T%04x%d", e->c, e->iters);
            return;

          case PLUS_E: {
            Rec(e->a);
            Rec(e->b);
            StringAppendF(&out, " E");
            return;
          }

          default:
            CHECK(false) << "Unknown expression type";
          }
        });
    Rec(e);
    return out;
  }

  static const Exp *Deserialize(Allocator *alloc,
                                std::string s,
                                std::string *err = nullptr) {
    std::vector<const Exp *> stack;
    const Exp *v = alloc->Var();
    s = Util::NormalizeWhitespace(s);

    for (;;) {
      string tok = Util::chop(s);
      if (tok.empty()) {
        if (stack.size() != 1) {
          if (err != nullptr) {
            *err = StringPrintf("Stack at end had %d elts\n",
                                (int)stack.size());
          }
          return nullptr;
        }
        return stack[0];
      }

      if (tok == "V") stack.push_back(v);
      else if (tok == "E") {
        if (stack.size() < 2) {
          if (err != nullptr) {
            *err = "Not enough items on stack for E.";
          }
          return nullptr;
        }

        const Exp *b = stack.back();
        stack.pop_back();
        const Exp *a = stack.back();
        stack.pop_back();
        stack.push_back(alloc->PlusE(a, b));
      } else {
        if (!(tok[0] == 'P' || tok[0] == 'T') ||
            tok.size() < 6) {
          if (err != nullptr) {
            *err = StringPrintf("Uknown token starting with %c\n",
                                tok[0]);
          }
          return nullptr;
        }

        uint16_t c = 0;
        for (int i = 1; i < 5; i++) {
          c <<= 4;
          c |= Util::HexDigitValue(tok[i]);
        }

        int iters = atoi(tok.c_str() + 5);
        if (iters <= 0) {
          if (err != nullptr) {
            *err = StringPrintf("Bogus iter count %d in %s\n",
                                iters, tok.c_str());
          }
          return nullptr;
        }

        if (stack.empty()) {
          if (err != nullptr) {
            *err = StringPrintf("Not enough items on stack for %c.",
                                tok[0]);
          }
          return nullptr;
        }

        const Exp *a = stack.back();
        stack.pop_back();

        stack.push_back(
            tok[0] == 'P' ?
            alloc->PlusC(a, c, iters) :
            alloc->TimesC(a, c, iters));
      }
    }
  }

  // Compute the size/cost of the expression. Each iteration in a
  // PlusC or TimesC counts as though it were its own node.
  static int ExpSize(const Exp *e) {
    switch (e->type) {
    case VAR: return 1;
    case PLUS_C:
    case TIMES_C:
      return 1 + (e->iters - 1) + ExpSize(e->a);
    case PLUS_E:
      return 1 + ExpSize(e->a) + ExpSize(e->b);
    default:
      CHECK(false) << "Unknown expression type";
      return 0;
    }
  }

  static std::string ExpString(const Exp *e) {
    switch (e->type) {
    case VAR: return "V";
    case PLUS_C: {
      std::string lhs = ExpString(e->a);
      return StringPrintf("P(%s,0x%04x,%d)",
                          lhs.c_str(), e->c, e->iters);
    }
    case TIMES_C: {
      std::string lhs = ExpString(e->a);
      return StringPrintf("T(%s,0x%04x,%d)",
                          lhs.c_str(), e->c, e->iters);
    }
    case PLUS_E: {
      std::string lhs = ExpString(e->a);
      std::string rhs = ExpString(e->b);
      return StringPrintf("E(%s,%s)", lhs.c_str(), rhs.c_str());
    }
    default:
      CHECK(false) << "Unknown expression type";
      return "";
    }
  }

private:
  Exp(ExpType t) : type(t) {}
};


#endif
