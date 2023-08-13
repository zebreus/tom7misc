
#ifndef _POLYNOMIAL_H
#define _POLYNOMIAL_H

#include <string>
#include <map>

#include "base/stringprintf.h"
#include "base/logging.h"

// In a polynomial like 3x^2y - 4y + 7, this represents one of the
// "x^2y", "y", or "" (multiplicative unit).
// Each variable in the term is mapped to its power (> 0).
// The empty term is unit, used to represent a constant term like 7
// above.
struct Term {
  Term() {}
  // returns "" for the unit term.
  std::string ToString() const {
    string ret;
    for (const auto &[x, e] : product) {
      if (e == 1) ret += x;
      else StringAppendF(&ret, "%s^%d", x.c_str(), e);
    }
    return ret;
  }
  std::map<std::string, int> product;
};

static inline operator ==(const Term &a, const Term &b) {
  return a.product == b.product;
}

static inline operator <(const Term &a, const Term &b) {
  return a.product < b.product;
}

// Represents an integer polynomial. Open domain of variables, just
// represented as strings.
struct Polynomial {
  // zero polynomial
  Polynomial() {}

  explicit Polynomial(int a) {
    if (a != 0) {
      // empty set of variables
      Term constant;
      sum = {{constant, a}};
    }
  }

  explicit Polynomial(const std::string &x, int exponent = 1) {
    Term one;
    one.product = {{x, exponent}};
    sum = {{one, 1}};
  }

  explicit Polynomial(const Term &t, int c) {
    if (c != 0) {
      sum = {{t, c}};
    }
  }

  std::string ToString() const {
    if (sum.empty()) return "0";
    // normally these would be sorted by descending power; we could
    // do that here?
    std::string ret;
    for (const auto &[t, c] : sum) {
      if (!ret.empty()) ret += " + ";
      if (c == 1) {
        string ts = t.ToString();
        // Term can be empty; need to explicitly represent the unit.
        if (ts.empty()) ret += "1";
        ret += ts;
      } else {
        StringAppendF(&ret, "%d%s", c, t.ToString().c_str());
      }
    }
    return ret;
  }

  static Polynomial PartialDerivative(const Polynomial &p,
                                      const std::string &x) {
    Polynomial r;
    for (const auto &[t, c] : p.sum) {
      auto it = t.product.find(x);
      if (it == t.product.end()) {
        // a term that doesn't depend on x; so it is dropped from
        // the derivative
      } else {
        int exponent = it->second;
        Term rt = t;
        // reduce its exponent
        if (exponent == 1) {
          rt.product.erase(x);
        } else {
          rt.product[x] = exponent - 1;
        }
        // e.g. 3x^7 yields (7 * 3)x^6
        r.sum[rt] = c * exponent;
      }
    }

    return r;
  }

  // This belongs in polynomial-util or needs to be made more
  // general.
  static std::string ToCode(const std::string &type, const Polynomial &p);

  // Each term mapped to its integer coefficient. The coefficient
  // may not be zero.
  std::map<Term, int> sum;
};

inline Polynomial operator -(const Polynomial &p) {
  Polynomial np = p;
  for (auto &tc : np.sum)
    tc.second = -tc.second;
  return np;
}

inline Polynomial operator +(const Polynomial &a, const Polynomial &b) {
  Polynomial r;

  auto ait = a.sum.begin();
  auto bit = b.sum.begin();

  while (ait != a.sum.end() && bit != b.sum.end()) {
    if (ait->first == bit->first) {
      // Polynomials share a term; add the coefficients. They could
      // completely cancel.
      int coeff = ait->second + bit->second;
      if (coeff != 0) {
        r.sum[ait->first] = coeff;
      }
      ++ait;
      ++bit;
    } else if (ait->first < bit->first) {
      r.sum[ait->first] = ait->second;
      ++ait;
    } else {
      r.sum[bit->first] = bit->second;
      ++bit;
    }
  }

  // Add any leftovers.
  while (ait != a.sum.end()) {
    r.sum[ait->first] = ait->second;
    ++ait;
  }

  while (bit != b.sum.end()) {
    r.sum[bit->first] = bit->second;
    ++bit;
  }

  return r;
}

inline Term operator *(const Term &a, const Term &b) {
  // Same kind of thing as adding polynomials. Find common terms and
  // add their exponents.
  Term r;

  auto ait = a.product.begin();
  auto bit = b.product.begin();

  while (ait != a.product.end() && bit != b.product.end()) {
    if (ait->first == bit->first) {
      // Terms share a variable. Add the exponents. They might
      // completely cancel.
      int new_e = ait->second + bit->second;
      if (new_e != 0) {
        r.product[ait->first] = ait->second + bit->second;
      }
      ++ait;
      ++bit;
    } else if (ait->first < bit->first) {
      r.product[ait->first] = ait->second;
      ++ait;
    } else {
      r.product[bit->first] = bit->second;
      ++bit;
    }
  }

  // Add any leftovers.
  while (ait != a.product.end()) {
    r.product[ait->first] = ait->second;
    ++ait;
  }

  while (bit != b.product.end()) {
    r.product[bit->first] = bit->second;
    ++bit;
  }

  return r;
}

inline Polynomial operator *(const Polynomial &a, const Polynomial &b) {
  Polynomial r;

  for (const auto &[t1, c1] : a.sum) {
    for (const auto &[t2, c2] : b.sum) {
      Term rt = t1 * t2;

      // PERF we could have an in-place addition of a single term.
      r = r + Polynomial(rt, c1 * c2);
    }
  }

  return r;
}

inline Polynomial operator -(const Polynomial &a, const Polynomial &b) {
  return a + -b;
}


inline Polynomial operator ""_p(unsigned long long a) {
  // XXX check bounds
  return Polynomial((int)a);
}

inline Polynomial operator ""_p(const char *s, size_t sz) {
  // XXX allow leading ints in the string?
  // XXX allow parsing ^2 etc.?
  return Polynomial(std::string(s, sz));
}


#endif
