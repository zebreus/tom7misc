
#include <string>
#include <cstdint>

#include "choppy.h"

#include "base/logging.h"
#include "half.h"
#include "bignum/big.h"
#include "ansi.h"
#include "timer.h"
#include "periodically.h"

using Choppy = ChoppyGrid<16>;
using DB = Choppy::DB;
using Allocator = Exp::Allocator;
using Table = Exp::Table;
using int64 = int64_t;

using namespace std;

static std::string AnsiTime(double seconds) {
  if (seconds < 60.0) {
    return StringPrintf(AYELLOW("%.3f") "s", seconds);
  } else if (seconds < 60.0 * 60.0) {
    int sec = std::round(seconds);
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "m" AYELLOW("%02d") "s",
                        omin, osec);
  } else {
    int sec = std::round(seconds);
    int ohour = sec / 3600;
    sec -= ohour * 3600;
    int omin = sec / 60;
    int osec = sec % 60;
    return StringPrintf(AYELLOW("%d") "h"
                        AYELLOW("%d") "m"
                        AYELLOW("%02d") "s",
                        ohour, omin, osec);
  }
}

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


static vector<string> BigConstantLines(const BigInt &x) {
  static constexpr int DIGITS_WIDE = 80;
  BigInt zero(0);
  CHECK(!BigInt::Less(x, zero));

  std::vector<string> lines;
  string xs = x.ToString();
  if (xs.size() <= DIGITS_WIDE) return {xs};
  while (xs.size() > DIGITS_WIDE) {
    lines.push_back(xs.substr(0, DIGITS_WIDE));
    xs = xs.substr(DIGITS_WIDE, string::npos);
  }
  if (!xs.empty())
    lines.push_back(std::move(xs));
  return lines;
}

static constexpr int MAX_LINES = 12;
static void CropTooManyLines(std::vector<string> *lines) {
  const int keep = MAX_LINES - 1;
  const int elided = lines->size() - keep;

  if (elided > 4) {
    std::vector<string> cropped;
    cropped.reserve(keep);
    int keepl = keep / 2;
    int keepr = keep - keepl;
    CHECK(keepl + keepr + elided == lines->size());
    for (int i = 0; i < keepl; i++)
      cropped.push_back(std::move((*lines)[i]));
    cropped.push_back(StringPrintf("\\ldots %d\\ \\mathrm{lines} \\ldots",
                                   elided));
    for (int i = lines->size() - 1 - keepr; i < lines->size(); i++) {
      CHECK(i >= 0);
      cropped.push_back(std::move((*lines)[i]));
    }
    *lines = std::move(cropped);
  }
}

static int64 IPow(int64 b, int64 e) {
  int64 res = 1;
  while (e) {
    if (e & 1)
      res *= b;
    e >>= 1;
    b *= b;
  }
  return res;
}

static string NegateSimple(const string &s) {
  for (char c : s) {
    if (c < '0' || c > '9') return StringPrintf("-(%s)", s.c_str());
  }
  return StringPrintf("-%s", s.c_str());
}

static string FactoredBigConstant(
    bool negate,
    const BigInt &x,
    const std::vector<std::pair<int64, int64>> &factors) {
  CHECK(!BigInt::Less(x, BigInt(0)));
  std::vector<string> lines = BigConstantLines(x);
  CropTooManyLines(&lines);

  // Add factors to last line.
  // XXX should have some logic if the last line is very long,
  // or the factor list is very long?
  CHECK(!lines.empty());
  string *last = &lines.back();

  for (const auto &[b, e] : factors) {
    if (e == 1) {
      StringAppendF(last, " \\times{} %lld", b);
    } else {
      if (b < 100 && e < 100 && IPow(b, e) < 9999) {
        int64 factor = IPow(b, e);
        StringAppendF(last, " \\times{} %lld", factor);
      } else {
        StringAppendF(last, " \\times{} %lld^{%lld}", b, e);
      }
    }
  }

  CHECK(!lines.empty());
  if (lines.size() == 1) {
    return negate ? NegateSimple(lines[0]) : lines[0];
  }

  // With a column for the minus sign, if any.
  string out =
    StringPrintf("\\begin{array}{r@{}l}\n");
  for (int i = 0; i < lines.size(); i++) {
    const string &line = lines[i];
    // Negate the first line if needed.
    StringAppendF(&out, "%s & %s \\\\" "\n",
                  (i == 0 && negate) ? "-" : "\\,",
                  line.c_str());
  }
  StringAppendF(&out, "\\end{array}");

  return out;
}

static string NiceIntToString(BigInt x) {
  const BigInt zero(0);
  // We manually place the minus sign.
  bool negated = BigInt::Less(x, zero);
  if (negated) x = BigInt::Negate(x);

  // Don't try to factor zero!
  if (BigInt::Less(x, zero)) return "0";

  // And don't bother with small constants.
  if (BigInt::Less(x, BigInt(99999)))
    return StringPrintf("%s%s",
                        negated ? "-" : "",
                        x.ToString().c_str());

  static constexpr int64_t MAX_FACTOR = 1000000;
  std::vector<std::pair<BigInt, int>> big_factors =
    BigInt::PrimeFactorization(x, MAX_FACTOR);

  // Since these are in ascending order, the last one
  // is the big composite number. But if we managed
  // to completely factor, then make x just be 1.
  CHECK(!big_factors.empty());
  if (big_factors.back().second == 1) {
    x = big_factors.back().first;
    big_factors.pop_back();
  } else {
    x = BigInt(1);
  }

  std::vector<std::pair<int64, int64>> factors;
  for (const auto &[b, e] : big_factors) {
    auto bo = b.ToInt();
    CHECK(bo.has_value()) << "We shouldn't be able to find any "
      "factors greater than " << MAX_FACTOR << " this way??";
    factors.emplace_back(bo.value(), (int64)e);
  }

  return FactoredBigConstant(negated, x, factors);
}

static string NiceRatToString(const BigRat &q) {
  const auto [numer, denom] = q.Parts();

  if (BigInt::Eq(numer, BigInt(0))) {
    return "0";
  } else if (BigInt::Eq(denom, BigInt(1))) {
    return NiceIntToString(numer);
  } else {
    return StringPrintf("\\frac{%s}{%s}",
                        NiceIntToString(numer).c_str(),
                        NiceIntToString(denom).c_str());
  }
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
    return make_pair(BigRat(0), BigRat(0));
  }
}

static string Literal(half h) {
  if (isnan(h)) {
    return "{\\sf NaN}";
  } else if (isinf(h)) {
    return h < (half)0 ? "-\\infty{}" : "\\infty{}";
  }

  BigRat q = ToRational(h);
  return NiceRatToString(q);
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
  string mul = NiceRatToString(a);
  string add = NiceRatToString(b);

  // If neither constant is stacked, avoid the array.
  if (mul.find("array") == string::npos &&
      add.find("array") == string::npos) {
    return StringPrintf("%s\\scalebox{2}{$%s\\,+$}%s",
                        mul.c_str(), var.c_str(), add.c_str());
  }

  return StringPrintf("\\begin{array}{l}\n"
                      "%s\\scalebox{2}{$%s\\,+$} \\\\[1em]\n"
                      "%s \\\\" "\n"
                      "\\end{array}",
                      mul.c_str(),
                      var.c_str(),
                      add.c_str());
}

int main(int argc, char **argv) {
  AnsiInit();

  CHECK(argc == 3) << "\n\nrender.exe basis.txt output.tex";
  const string dbfile = argv[1];
  const string outfile = argv[2];

  Timer total_time;

  DB db;
  db.LoadFile("basis.txt");

  string out = R"!(
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
)!";

  std::map<DB::key_type, const Exp *> sorted;
  for (const auto &[k, v] : db.fns) sorted[k] = v;

  for (const auto &[k, v] : sorted) {
    Timer timer;
    string key = DB::KeyString(k);
    printf(AGREY("%s:") "\n", key.c_str());
    if (false &&
        (key == " 0  0  0  0  0  0  0  0  1  0  0  0  0  0  0  0" ||
         key == " 0  0  0  0  0  0  0  1  0  0  0  0  0  0  0  0")) {
      printf(ARED("skipped") "\n");
      continue;
    }
    StringAppendF(&out, "\n\\bigskip \n\\bigskip \n");
    StringAppendF(&out, "{\\bf %s} \\\\\n", key.c_str());
    string lit = Literally("x", v);
    StringAppendF(&out, "$ %s $ \\\\\n", lit.c_str());

    StringAppendF(&out, "\n\\scalebox{2}{$$=$$}\n");
    // This will terminate in a few minutes, but the numbers it
    // generates are usually way too big to be interesting!
    string lin = Linearly("x", v);
    StringAppendF(&out, "\\[\n%s\n\\]\n\n", lin.c_str());
    printf("... " ABLUE("%d") " lit "
           APURPLE("%d") " lin. "
           "Done in %s\n", (int)lit.size(), (int)lin.size(),
           AnsiTime(timer.Seconds()).c_str());
  }

  StringAppendF(&out, "\\end{document}\n");
  Util::WriteFile(outfile, out);
  printf("Wrote " AGREEN("%s") "\n", outfile.c_str());
  printf("Total time %s\n", AnsiTime(total_time.Seconds()).c_str());
  return 0;
}
