
#include <string>
#include "choppy.h"

#include "base/logging.h"
#include "half.h"
#include "bignum/big.h"

using Choppy = ChoppyGrid<16>;
using DB = Choppy::DB;
using Allocator = Exp::Allocator;
using Table = Exp::Table;

using namespace std;

// Represents a decoded IEEE half number:
//   (negative ? -1 : 1) * 2^exponent *
//      ((one_plus ? 1 : 0) + significand / 2^10).
struct Decoded {
  bool negative = false;
  // If true, only the negative field is meaningful.
  bool is_inf = false;
  // If true, no other fields are meaningful.
  bool is_nan = false;
  // True if the number is normalized; a leading 1 is implied.
  bool one_plus = false;
  // For finite values.
  int exponent = 0;
  uint32_t significand = 0;
};

static Decoded Decode(half h) {
  uint64_t u = Exp::GetU16(h);

  Decoded res;

  res.negative = !!(u & 0x8000);
  const uint8_t exp_bits = (u >> 10) & 0b11111;
  const uint8_t frac_bits = u & 0b1111111111;

  if (exp_bits == 0) {
    // zero or denormal
    // Note that we represent zero as 2^-14 * 0/1024
    res.exponent = -14;
    res.one_plus = false;
    res.significand = frac_bits;
    return res;
  }

  if (exp_bits == 0b11111) {
    res.is_inf = frac_bits == 0;
    res.is_nan = frac_bits != 0;
    return res;
  }

  // Otherwise, a normal.
  res.exponent = exp_bits - 15;
  res.one_plus = true;
  res.significand = frac_bits;
  return res;
}

// For finite values.
static BigRat ToRational(half h) {
  Decoded d = Decode(h);
  CHECK(!d.is_inf && !d.is_nan);

  const BigInt powtwo = BigInt::Pow(BigInt(2), abs(d.exponent));

  // 2^n or 1/2^n
  const BigRat base =
    (d.exponent >= 0) ? BigRat(powtwo, BigInt(1)) :
    BigRat(BigInt(1), powtwo);

  // apply sign
  const BigRat sbase = d.negative ? BigRat::Negate(base) : base;

  // now scale by significand.
  const BigRat sig = BigRat(d.significand, 1024);

  const BigRat scale =
    d.one_plus ?
    BigRat::Plus(BigRat(1LL), sig) :
    sig;

  return BigRat::Times(sbase, scale);
}

#if 0
// Might be better to do a "debugging" version, but
// there are various simplifications possible that
// mean we don't want try to enumerate cases like this.
// For example, 2^15 * (1 + 1023/1024) is an integer.
// A single half, exactly represented (LaTeX).
static string Literal(half h) {
  Decoded d = Decode(h);

  auto MaybeNegate = [&d](const string &s) {
      if (d.negative) return "-" + s;
      else return s;
    };

  if (d.is_nan) return "{\\sf NaN}";

  if (d.is_inf) {
    return MaybeNegate("\\infty{}");
  }

  if (d.significand == 0) {
    if (!d.one_plus) {
      // zero
      return MaybeNegate("0");
    } else {
      // Powers of two.
      if (d.exp >= 0 && d.exp <= 9) {
        return MaybeNegate(StringPrintf("%d", 1 << d.exp));
      } else {
        if (d.negative) {
          return StringPrintf("-(2^{%d})", d.exp);
        } else {
          return StringPrintf("2^{%d}", d.exp);
        }
      }
    }
  }
}
#endif

// Simplifies to an equivalent (pretending the IEEE operations
// are the mathematical operations with the same name) linear
// expression of the form ax + b.
std::pair<BigRat, BigRat> GetLinear(const Exp *exp) {
  switch (exp->type) {
  case VAR:
    // 1x + 0
    return make_pair(BigRat(1), BigRat(0));
  case PLUS_C: {
    BigRat a, b;
    std::tie(a, b) = GetLinear(exp->a);
    BigRat c = ToRational(Exp::GetHalf(exp->c));
    BigRat iters(BigInt(exp->iters));
    b = BigRat::Plus(b, BigRat::Times(c, iters));
    /*
    for (int i = 0; i < exp->iters; i++) {
      b = BigRat::Plus(b, c);
    }
    */
    return make_pair(a, b);
  }
  case TIMES_C: {
    BigRat a, b;
    std::tie(a, b) = GetLinear(exp->a);
    BigRat c = ToRational(Exp::GetHalf(exp->c));
    BigRat iters(BigInt(exp->iters));
    a = BigRat::Times(a, BigRat::Pow(c, exp->iters));
    b = BigRat::Times(b, BigRat::Pow(c, exp->iters));
    /*
    for (int i = 0; i < exp->iters; i++) {
      a = BigRat::Times(a, c);
      b = BigRat::Times(b, c);
    }
    */
    return make_pair(a, b);
  }
  case PLUS_E: {
    const auto [a0, b0] = GetLinear(exp->a);
    const auto [a1, b1] = GetLinear(exp->b);
    return make_pair(BigRat::Plus(a0, a1),
                     BigRat::Plus(b0, b1));
  }
  default:
    CHECK(false) << "bad exp";
  }
}

static string RatToString(const BigRat &q) {
  const auto [numer, denom] = q.Parts();
  // TODO: Force denominator positive, if that's not
  // already guaranteed
  if (BigInt::Eq(denom, BigInt(1))) {
    return numer.ToString();
  } else {
    return StringPrintf("\\frac{%s}{%s}",
                        numer.ToString().c_str(),
                        denom.ToString().c_str());
  }
}

static string Literal(half h) {
  if (isnan(h)) {
    return "{\\sf NaN}";
  } else if (isinf(h)) {
    return h < (half)0 ? "-\\infty{}" : "\\infty{}";
  }

  BigRat q = ToRational(h);
  // XXX make nicer
  return RatToString(q);
}

// Render expressions as their mathematical counterparts.

static string Literally(const string &var, const Exp *exp) {
  // This is the literal version of the expression, i.e., the
  // exact sequence of IEEE half operations we do.

  switch (exp->type) {
  case VAR:
    return var;
  case PLUS_C: {
    string lhs = Literally(var, exp->a);
    half hc = Exp::GetHalf(exp->c);
    if (exp->iters > 1) {
      string c = Literal(hc);
      return StringPrintf("%s +^{\\scriptscriptstyle %d} %s",
                          lhs.c_str(), exp->iters, c.c_str());
    } else {
      // As a simplification, adding a negative value is exactly
      // the same as subtracting a positive one, and results in
      // slightly better LaTeX layout.
      if (hc < (half)0) {
        string c = Literal(-hc);
        return StringPrintf("%s - %s", lhs.c_str(), c.c_str());
      } else {
        string c = Literal(hc);
        return StringPrintf("%s + %s", lhs.c_str(), c.c_str());
      }
    }
    break;
  }
  case TIMES_C: {
    string lhs = Literally(var, exp->a);
    string c = Literal(Exp::GetHalf(exp->c));
    if (exp->iters > 1) {
      return StringPrintf("%s \\times^{\\scriptscriptstyle %d} %s",
                          lhs.c_str(), exp->iters, c.c_str());
    } else {
      return StringPrintf("%s \\times{} %s", lhs.c_str(), c.c_str());
    }
    break;
  }
  case PLUS_E: {
    string lhs = Literally(var, exp->a);
    string rhs = Literally(var, exp->b);
    return StringPrintf("(%s) + (%s)", lhs.c_str(), rhs.c_str());
  }
  default:
    CHECK(false) << "bad exp";
    return "";
  }
}

static string Linearly(const string &var, const Exp *exp) {
  auto [a, b] = GetLinear(exp);

  // XXX simplify when a is 1, b is 0, etc.
  // XXX put x in denominator
  // XXX if b is < 0, subtract
  return StringPrintf("%s%s + %s",
                      RatToString(a).c_str(), var.c_str(),
                      RatToString(b).c_str());
}

int main(int argc, char **argv) {
  DB db;
  db.LoadFile("basis.txt");

  printf(R"!(
\documentclass{article}
\usepackage[top=0.75in, left=0.65in, right=0.65in, bottom=0.6in]{geometry}
\usepackage{float}
\usepackage[pdftex]{hyperref}
\usepackage{listings}
\usepackage{latexsym}
\usepackage{amsmath}
\usepackage{amssymb}
\usepackage{graphicx}
\begin{document}
)!");

  std::map<DB::key_type, const Exp *> sorted;
  for (const auto &[k, v] : db.fns) sorted[k] = v;

  for (const auto &[k, v] : sorted) {
    printf("%% %s:\n",
           DB::KeyString(k).c_str());
    printf("{\\bf %s} \\\\\n", DB::KeyString(k).c_str());
    string lit = Literally("x", v);
    printf("$ %s $ \\\\\n", lit.c_str());
    // This will terminate in a few minutes, but the numbers it
    // generates are usually way too big to be interesting!~
    string lin = Linearly("x", v);
    printf("$ = %s $\n\n", lin.c_str());
    fprintf(stderr, "%s\n", DB::KeyString(k).c_str());
  }

  printf("\\end{document}\n");
}
