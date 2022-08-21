
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

#if 0
// TODO:
// Represents a rational in a factored form.
struct PolyRat {
  bool negative = false;
  // Factored form.
  std::vector<std::pair<BigNum, int>> numer;
  std::vector<std::pair<BigNum, int>> denom;
};
#endif

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

static string FactorizedString(const BigInt &z) {
  // XXX just factor and negate, but PrimeFactorization
  // only works on positive inputs.
  if (BigInt::Less(z, BigInt(0))) {
    return z.ToString();
  }

  return z.ToString();
#if 0
  // XXX I think this is freezing because there are a lot of
  //

  // Would be nice to use a time limit instead of factor limit.
  auto factors = BigInt::PrimeFactorization(z, 16);
  string out;
  for (const auto &[b, e] : factors) {
    if (!out.empty()) StringAppendF(&out, " \\cdot ");
    if (e == 1) {
      StringAppendF(&out, "%s", b.ToString().c_str());
    } else {
      StringAppendF(&out, "%s^{%d}", b.ToString().c_str(), e);
    }
  }
  return out;
#endif
}

static string RatToString(const BigRat &q) {
  const auto [numer, denom] = q.Parts();
  // TODO: Force denominator positive, if that's not
  // already guaranteed
  if (BigInt::Eq(denom, BigInt(1))) {
    return FactorizedString(numer);
  } else if (BigInt::Eq(numer, BigInt(0))) {
    return "0";
  } else {
    string ns = FactorizedString(numer);
    string ds = FactorizedString(denom);

    return StringPrintf("\\frac{%s}{%s}",
                        ns.c_str(),
                        ds.c_str());
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

static constexpr array<uint16_t, 1000> PRIMES = {
  2,3,5,7,11,13,17,19,23,29,
  31,37,41,43,47,53,59,61,67,71,
  73,79,83,89,97,101,103,107,109,113,
  127,131,137,139,149,151,157,163,167,173,
  179,181,191,193,197,199,211,223,227,229,
  233,239,241,251,257,263,269,271,277,281,
  283,293,307,311,313,317,331,337,347,349,
  353,359,367,373,379,383,389,397,401,409,
  419,421,431,433,439,443,449,457,461,463,
  467,479,487,491,499,503,509,521,523,541,
  547,557,563,569,571,577,587,593,599,601,
  607,613,617,619,631,641,643,647,653,659,
  661,673,677,683,691,701,709,719,727,733,
  739,743,751,757,761,769,773,787,797,809,
  811,821,823,827,829,839,853,857,859,863,
  877,881,883,887,907,911,919,929,937,941,
  947,953,967,971,977,983,991,997,1009,1013,
  1019,1021,1031,1033,1039,1049,1051,1061,1063,1069,
  1087,1091,1093,1097,1103,1109,1117,1123,1129,1151,
  1153,1163,1171,1181,1187,1193,1201,1213,1217,1223,
  1229,1231,1237,1249,1259,1277,1279,1283,1289,1291,
  1297,1301,1303,1307,1319,1321,1327,1361,1367,1373,
  1381,1399,1409,1423,1427,1429,1433,1439,1447,1451,
  1453,1459,1471,1481,1483,1487,1489,1493,1499,1511,
  1523,1531,1543,1549,1553,1559,1567,1571,1579,1583,
  1597,1601,1607,1609,1613,1619,1621,1627,1637,1657,
  1663,1667,1669,1693,1697,1699,1709,1721,1723,1733,
  1741,1747,1753,1759,1777,1783,1787,1789,1801,1811,
  1823,1831,1847,1861,1867,1871,1873,1877,1879,1889,
  1901,1907,1913,1931,1933,1949,1951,1973,1979,1987,
  1993,1997,1999,2003,2011,2017,2027,2029,2039,2053,
  2063,2069,2081,2083,2087,2089,2099,2111,2113,2129,
  2131,2137,2141,2143,2153,2161,2179,2203,2207,2213,
  2221,2237,2239,2243,2251,2267,2269,2273,2281,2287,
  2293,2297,2309,2311,2333,2339,2341,2347,2351,2357,
  2371,2377,2381,2383,2389,2393,2399,2411,2417,2423,
  2437,2441,2447,2459,2467,2473,2477,2503,2521,2531,
  2539,2543,2549,2551,2557,2579,2591,2593,2609,2617,
  2621,2633,2647,2657,2659,2663,2671,2677,2683,2687,
  2689,2693,2699,2707,2711,2713,2719,2729,2731,2741,
  2749,2753,2767,2777,2789,2791,2797,2801,2803,2819,
  2833,2837,2843,2851,2857,2861,2879,2887,2897,2903,
  2909,2917,2927,2939,2953,2957,2963,2969,2971,2999,
  3001,3011,3019,3023,3037,3041,3049,3061,3067,3079,
  3083,3089,3109,3119,3121,3137,3163,3167,3169,3181,
  3187,3191,3203,3209,3217,3221,3229,3251,3253,3257,
  3259,3271,3299,3301,3307,3313,3319,3323,3329,3331,
  3343,3347,3359,3361,3371,3373,3389,3391,3407,3413,
  3433,3449,3457,3461,3463,3467,3469,3491,3499,3511,
  3517,3527,3529,3533,3539,3541,3547,3557,3559,3571,
  3581,3583,3593,3607,3613,3617,3623,3631,3637,3643,
  3659,3671,3673,3677,3691,3697,3701,3709,3719,3727,
  3733,3739,3761,3767,3769,3779,3793,3797,3803,3821,
  3823,3833,3847,3851,3853,3863,3877,3881,3889,3907,
  3911,3917,3919,3923,3929,3931,3943,3947,3967,3989,
  4001,4003,4007,4013,4019,4021,4027,4049,4051,4057,
  4073,4079,4091,4093,4099,4111,4127,4129,4133,4139,
  4153,4157,4159,4177,4201,4211,4217,4219,4229,4231,
  4241,4243,4253,4259,4261,4271,4273,4283,4289,4297,
  4327,4337,4339,4349,4357,4363,4373,4391,4397,4409,
  4421,4423,4441,4447,4451,4457,4463,4481,4483,4493,
  4507,4513,4517,4519,4523,4547,4549,4561,4567,4583,
  4591,4597,4603,4621,4637,4639,4643,4649,4651,4657,
  4663,4673,4679,4691,4703,4721,4723,4729,4733,4751,
  4759,4783,4787,4789,4793,4799,4801,4813,4817,4831,
  4861,4871,4877,4889,4903,4909,4919,4931,4933,4937,
  4943,4951,4957,4967,4969,4973,4987,4993,4999,5003,
  5009,5011,5021,5023,5039,5051,5059,5077,5081,5087,
  5099,5101,5107,5113,5119,5147,5153,5167,5171,5179,
  5189,5197,5209,5227,5231,5233,5237,5261,5273,5279,
  5281,5297,5303,5309,5323,5333,5347,5351,5381,5387,
  5393,5399,5407,5413,5417,5419,5431,5437,5441,5443,
  5449,5471,5477,5479,5483,5501,5503,5507,5519,5521,
  5527,5531,5557,5563,5569,5573,5581,5591,5623,5639,
  5641,5647,5651,5653,5657,5659,5669,5683,5689,5693,
  5701,5711,5717,5737,5741,5743,5749,5779,5783,5791,
  5801,5807,5813,5821,5827,5839,5843,5849,5851,5857,
  5861,5867,5869,5879,5881,5897,5903,5923,5927,5939,
  5953,5981,5987,6007,6011,6029,6037,6043,6047,6053,
  6067,6073,6079,6089,6091,6101,6113,6121,6131,6133,
  6143,6151,6163,6173,6197,6199,6203,6211,6217,6221,
  6229,6247,6257,6263,6269,6271,6277,6287,6299,6301,
  6311,6317,6323,6329,6337,6343,6353,6359,6361,6367,
  6373,6379,6389,6397,6421,6427,6449,6451,6469,6473,
  6481,6491,6521,6529,6547,6551,6553,6563,6569,6571,
  6577,6581,6599,6607,6619,6637,6653,6659,6661,6673,
  6679,6689,6691,6701,6703,6709,6719,6733,6737,6761,
  6763,6779,6781,6791,6793,6803,6823,6827,6829,6833,
  6841,6857,6863,6869,6871,6883,6899,6907,6911,6917,
  6947,6949,6959,6961,6967,6971,6977,6983,6991,6997,
  7001,7013,7019,7027,7039,7043,7057,7069,7079,7103,
  7109,7121,7127,7129,7151,7159,7177,7187,7193,7207,
  7211,7213,7219,7229,7237,7243,7247,7253,7283,7297,
  7307,7309,7321,7331,7333,7349,7351,7369,7393,7411,
  7417,7433,7451,7457,7459,7477,7481,7487,7489,7499,
  7507,7517,7523,7529,7537,7541,7547,7549,7559,7561,
  7573,7577,7583,7589,7591,7603,7607,7621,7639,7643,
  7649,7669,7673,7681,7687,7691,7699,7703,7717,7723,
  7727,7741,7753,7757,7759,7789,7793,7817,7823,7829,
  7841,7853,7867,7873,7877,7879,7883,7901,7907,7919,
};

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

[[maybe_unused]]
static string BigConstant(const BigInt &x) {
  std::vector<string> lines = BigConstantLines(x);
  CropTooManyLines(&lines);

  string out =
    StringPrintf("\\begin{array}{l}\n");
  for (const string &line : lines)
    StringAppendF(&out, "%s \\\\" "\n",
                  line.c_str());
  StringAppendF(&out, "\\end{array}");

  return out;
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

// TODO: Handle negation.
static string FactoredBigConstant(
    bool negate,
    const BigInt &x,
    const std::vector<std::pair<int64, int64>> &factors) {
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

  // TODO: Can just use BigInt::PrimeFactorization now, I think.
  Timer timer;
  Periodically progress_per(5);
  progress_per.ShouldRun(); // skip first output.
  std::vector<std::pair<int64, int64>> factors;
  for (int64 small_factor : PRIMES) {
    BigInt factor(small_factor);
    int64 exponent = 0;
    /* 1024 * 1024 * 1024, 1024 * 1024, */
    std::vector<int64> powers = {1};
    if (small_factor == 2)
      powers = {1024 * 1024, 1024, 64, 1};

    for (int64 power : powers) {
      BigInt dpow = BigInt::Pow(factor, power);

      /*
      printf("Trying %lld^%lld = %s\n",
             small_factor, power, dpow.ToString().c_str());
      */

      bool more = false;
      do {
        more = false;
        // printf("Quotrem: %lld %lld\n", small_factor, power);
        auto [q, r] = BigInt::QuotRem(x, dpow);
        // printf("Done.\n");
        if (BigInt::Eq(r, zero)) {
          // printf("Divisible.\n");
          exponent += power;
          x = q;
          more = true;
          if (progress_per.ShouldRun()) {
            printf("%s:", AnsiTime(timer.Seconds()).c_str());
            for (const auto &[b, e] : factors) {
              printf("  " ABLUE("%lld") "^" APURPLE("%lld"), b, e);
            }

            printf("  " AWHITE("%lld") "^" AGREEN("%lld") " "
                   AGREY("[power %lld]") "\n",
                   small_factor, exponent, power);

            /*
            string xs = x.ToString();
            printf(" %lld digits = %s\n",
                   (int64_t)xs.size(), xs.c_str());
            */
          }
        }
      } while (more);
      // printf("Do loop ends\n");
    }
    if (exponent > 0) {
      factors.emplace_back(small_factor, exponent);
    }
  }
  double sec = timer.Seconds();
  if (sec > 5.0) {
    printf("Done in %s\n", AnsiTime(timer.Seconds()).c_str());
  }

  return FactoredBigConstant(negated, x, factors);
}

static string NiceRatToString(const BigRat &q) {
  const auto [numer, denom] = q.Parts();

  if (BigInt::Eq(denom, BigInt(1))) {
    return FactorizedString(numer);
  } else if (BigInt::Eq(numer, BigInt(0))) {
    return "0";
  } else {
    return StringPrintf("\\frac{%s}{%s}",
                        NiceIntToString(numer).c_str(),
                        NiceIntToString(denom).c_str());
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
