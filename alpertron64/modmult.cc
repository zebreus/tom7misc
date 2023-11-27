// This file is part of Alpertron Calculators.
//
// Copyright 2015-2021 Dario Alejandro Alpern
//
// Alpertron Calculators is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Alpertron Calculators is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Alpertron Calculators.  If not, see <http://www.gnu.org/licenses/>.

#include "modmult.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>

#include <bit>
#include <memory>
#include <tuple>
#include <cstdint>

#include "base/logging.h"
#include "base/stringprintf.h"

static constexpr bool VERBOSE = false;
static constexpr bool SELF_CHECK = false;

// PERF: Can do this with a loop, plus bit tricks to avoid
// division.
std::tuple<int64_t, int64_t, int64_t>
ReferenceExtendedGCD64Internal(int64_t a, int64_t b) {
  if (a == 0) return std::make_tuple(b, 0, 1);
  const auto &[gcd, x1, y1] = ReferenceExtendedGCD64Internal(b % a, a);
  return std::make_tuple(gcd, y1 - (b / a) * x1, x1);
}

std::tuple<int64_t, int64_t, int64_t>
ReferenceExtendedGCD64(int64_t a, int64_t b) {
  const auto &[gcd, x, y] = ReferenceExtendedGCD64Internal(a, b);
  if (gcd < 0) return std::make_tuple(-gcd, -x, -y);
  else return std::make_tuple(gcd, x, y);
}

std::tuple<int64_t, int64_t, int64_t>
ExtendedGCD64Internal(int64_t a, int64_t b) {
  if (VERBOSE)
    printf("gcd(%lld, %lld)\n", a, b);

  a = abs(a);
  b = abs(b);

  if (a == 0) return std::make_tuple(b, 0, 1);
  if (b == 0) return std::make_tuple(a, 1, 0);


  // Remove common factors of 2.
  const int r = std::countr_zero<uint64_t>(a | b);
  a >>= r;
  b >>= r;

  int64_t alpha = a;
  int64_t beta = b;

  if (VERBOSE) {
    printf("Alpha: %lld, Beta: %lld\n", alpha, beta);
  }

  int64_t u = 1, v = 0, s = 0, t = 1;

  if (VERBOSE) {
    printf("2Loop %lld = %lld alpha + %lld beta | "
           "%lld = %lld alpha + %lld beta\n",
           a, u, v, b, s, t);
  }

  if (SELF_CHECK) {
    CHECK(a == u * alpha + v * beta) << a << " = "
                                     << u * alpha << " + "
                                     << v * beta << " = "
                                     << (u * alpha) + (v * beta);
    CHECK(b == s * alpha + t * beta);
  }

  int azero = std::countr_zero<uint64_t>(a);
  if (azero > 0) {

    int uvzero = std::countr_zero<uint64_t>(u | v);

    // shift away all the zeroes in a.
    a >>= azero;

    int all_zero = std::min(azero, uvzero);
    u >>= all_zero;
    v >>= all_zero;

    int rzero = azero - all_zero;
    if (VERBOSE)
      printf("azero %d uvzero %d all_zero %d rzero %d\n",
             azero, uvzero, all_zero, rzero);

    for (int i = 0; i < rzero; i++) {
      // PERF: The first time through, we know we will
      // enter the top branch.
      if ((u | v) & 1) {
        u += beta;
        v -= alpha;
      }

      u >>= 1;
      v >>= 1;
    }
  }

  while (a != b) {
    if (VERBOSE) {
      printf("Loop %lld = %lld alpha + %lld beta | "
             "%lld = %lld alpha + %lld beta\n",
             a, u, v, b, s, t);
    }

    if (SELF_CHECK) {
      CHECK(a == u * alpha + v * beta) << a << " = "
                                       << u * alpha << " + "
                                       << v * beta << " = "
                                       << (u * alpha) + (v * beta);
      CHECK(b == s * alpha + t * beta);

      CHECK((a & 1) == 1);
    }

    // Loop invariant.
    // PERF: I think that this loop could still be improved.
    // I explicitly skip some of the tests that gcc couldn't
    // figure out, but it still generates explicit move
    // instructions for the swap; we could just have six
    // states and track that manually.
    if ((a & 1) == 0) __builtin_unreachable();

    // one:
    if ((b & 1) == 0) {
    one_even:
      if (SELF_CHECK) { CHECK((b & 1) == 0); }

      b >>= 1;
      if (((s | t) & 1) == 0) {
        s >>= 1;
        t >>= 1;
      } else {
        s = (s + beta) >> 1;
        t = (t - alpha) >> 1;
      }

      // could cause a to equal b
      continue;
    }

    // two:
    if (b < a) {
      // printf("Swap.\n");
      std::swap(a, b);
      std::swap(s, u);
      std::swap(t, v);

      // we know a is odd, and now a < b, so we
      // go to case three.
      goto three;
    }

  three:
    b -= a;
    s -= u;
    t -= v;
    if (SELF_CHECK) {
      // we would only have b == a here if b was 2a.
      // but this is impossible since b was odd.
      CHECK(b != a);
      // but since we had odd - odd, we b is now even.
      CHECK((b & 1) == 0);
    }
    // so we know we enter that branch next.
    goto one_even;
  }

  return std::make_tuple(a << r, s, t);
}

std::tuple<int64_t, int64_t, int64_t>
ExtendedGCD64(int64_t a, int64_t b) {
  const auto &[gcd, x, y] = ExtendedGCD64Internal(a, b);
  if (SELF_CHECK) {
    CHECK(gcd >= 0);
  }
  // Negate coefficients if they start negative.
  return std::make_tuple(gcd, a < 0 ? -x : x, b < 0 ? -y : y);
}
