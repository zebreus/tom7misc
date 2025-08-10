
#include <limits>
#include <string>
#include <utility>
#define BIG_USE_GMP 1

#include "wrap-gmp.h"

#include <gmp.h>
#include <cstdio>

#include "ansi.h"
#include "base/do-not-optimize.h"
#include "base/logging.h"

// Create a rep with an alloc, even for small integers.
static GmpRep CreateLarge(const char *s) {
  GmpRep rep;
  mpz_init_set_str(rep.Mpz(), s, 10);
  return rep;
}

#define CHECK_SMALL(rep_, expected_) do {                             \
    auto ch_rep(rep_);                                                \
    int64_t expected = (expected_);                                   \
    CHECK(ch_rep.IsSmall() && ch_rep.GetSmall() == expected) <<       \
      "Expected small " << expected << " but got " <<                 \
      ch_rep.GetSmall();                                              \
  } while (0)

#define CHECK_LARGE(rep_, expected_str) do {                            \
    auto ch_rep(rep_);                                                  \
    GmpRep expected = CreateLarge(expected_str);                        \
    CHECK(!expected.IsSmall()) << "Bug in test!";                       \
    CHECK(!ch_rep.IsSmall()) << "Expected large " << expected_str <<    \
      " but was small: " << ch_rep.GetSmall();                          \
    int c = mpz_cmp(ch_rep.ConstMpz(), expected.ConstMpz());            \
    CHECK(c == 0) << "Expected " << expected_str << " cmp = " << c;     \
  } while (0)


// Same idea, for rationals.
static GmpRepRat CreateLargeRat(const std::string &numer,
                                const std::string &denom = "1") {
  GmpRepRat rep;
  CHECK(rep.Mpq() != nullptr);
  mpz_set_str(mpq_numref(rep.Mpq()), numer.c_str(), 10);
  mpz_set_str(mpq_denref(rep.Mpq()), denom.c_str(), 10);
  mpq_canonicalize(rep.Mpq());
  return rep;
}

#define CHECK_SMALL_RAT(rep_, n_, d_) do {                              \
    auto ch_rep((rep_));                                                \
    int64_t n = (n_);                                                   \
    int64_t d = (d_);                                                   \
    CHECK(ch_rep.IsSmall()) << "Expected small " << n << "/" << d;      \
    CHECK(ch_rep.SmallNumer() == n && ch_rep.SmallDenom() == d) <<      \
      "Expected " << n << "/" << d << " but got " <<                    \
      ch_rep.SmallNumer() << "/" << ch_rep.SmallDenom();                \
  } while (0)

#define CHECK_LARGE_RAT(rep_, expected_numer_, expected_denom_) do {    \
    auto ch_rep((rep_));                                                \
    GmpRepRat expected =                                                \
      CreateLargeRat(expected_numer_, expected_denom_);                 \
    CHECK(!expected.IsSmall()) << "Bug in test!";                       \
    CHECK(!ch_rep.IsSmall()) << "Expected large " <<                    \
      expected_numer_ << " / " << expected_denom_ <<                    \
      ". but was small: " << ch_rep.SmallNumer() << "/" <<              \
      ch_rep.SmallDenom();                                              \
    int c = mpq_cmp(ch_rep.ConstMpq(), expected.ConstMpq());            \
    CHECK(c == 0) << "Expected " << expected_numer_ << " / " <<         \
      expected_denom_ << ". cmp = " << c;                               \
  } while (0)

static void Lifetime() {
  // Default constructor means zero.
  {
    GmpRep rep;
    CHECK(rep.IsSmall());
    CHECK(rep.GetSmall() == 0);
  }

  {
    GmpRep rep(12345);
    CHECK_SMALL(rep, 12345);
  }

  {
    GmpRep rep(-12345);
    CHECK_SMALL(rep, -12345);
  }

  const char *large_val = "123456789012345678901234567890";

  // Copy constructor
  {
    GmpRep s1(999);
    GmpRep s2 = s1;
    CHECK_SMALL(s2, 999);
    CHECK_SMALL(s1, 999);

    GmpRep l1 = CreateLarge(large_val);
    GmpRep l2 = l1;
    CHECK_LARGE(l2, large_val);
    CHECK_LARGE(l1, large_val);
  }

  // Move constructor
  {
    GmpRep s1(999);
    GmpRep s2 = std::move(s1);
    CHECK(s2.IsSmall());
    CHECK_SMALL(s2, 999);
    // Must be safe to assign over the value.
    s1 = GmpRep(1234);
    CHECK_SMALL(s1, 1234);
  }

  {
    GmpRep s1(999);
    GmpRep s2 = std::move(s1);
    // Also must be safe to destroy s1.
  }

  {
    GmpRep s1(999);
    GmpRep s2 = std::move(s1);
    // Also must be safe to move over s1.
    GmpRep s3 = CreateLarge("1827398237591873495871234");
    s1 = std::move(s3);
  }

  // Same, but where the original object is large.
  {
    GmpRep s1 = CreateLarge("999999999999999999999991");
    GmpRep s2 = std::move(s1);
    CHECK_LARGE(s2, "999999999999999999999991");
    s1 = GmpRep(1234);
    CHECK_SMALL(s1, 1234);
  }

  {
    GmpRep s1 = CreateLarge("1928374981273491872349817");
    GmpRep s2 = std::move(s1);
    // Also must be safe to destroy s1.
  }

  {
    GmpRep s1 = CreateLarge("7777777777777777777777777777733");
    GmpRep s2 = std::move(s1);
    // Also must be safe to move over s1.
    GmpRep s3 = CreateLarge("1827398237591873495871234");
    s1 = std::move(s3);
    CHECK_LARGE(s2, "7777777777777777777777777777733");
    CHECK_LARGE(s1, "1827398237591873495871234");
  }
}

static void Assignment() {
  const char *large1_val = "1111111111111111111111111111111";
  const char *large2_val = "2222222222222222222222222222222";

  // Assignment
  {
    GmpRep s1(123), s2(456);
    GmpRep l1 = CreateLarge(large1_val);
    GmpRep l2 = CreateLarge(large2_val);

    s1 = s2;
    CHECK_SMALL(s1, 456);

    l1 = l2;
    CHECK_LARGE(l1, large2_val);

    l1 = s1;
    CHECK_SMALL(l1, 456);

    s1 = l2;
    CHECK_LARGE(s1, large2_val);
  }

  // Move assignment
  {
    GmpRep s1(123);
    GmpRep s2(456);
    s1 = std::move(s2);
    CHECK_SMALL(s1, 456);

    // large = small
    GmpRep l1 = CreateLarge(large1_val);
    s1 = GmpRep(789);
    l1 = std::move(s1);
    CHECK_SMALL(l1, 789);

    // small = large
    s1 = GmpRep(111);
    l1 = CreateLarge(large1_val);
    s1 = std::move(l1);
    CHECK_LARGE(s1, large1_val);

    // large = large
    l1 = CreateLarge(large1_val);
    GmpRep l2 = CreateLarge(large2_val);
    l1 = std::move(l2);
    CHECK_LARGE(l1, large2_val);

    // Self move via alias to suppress warning.
    {
      GmpRep &alias = s2;
      s2 = std::move(alias);
      s2 = s1;
    }

    {
      GmpRep &alias = s2;
      s2 = std::move(alias);
    }

    {
      GmpRep l2 = CreateLarge(large2_val);
      GmpRep backup = l2;
      CHECK(!backup.IsSmall());
      CHECK(!l2.IsSmall());

      GmpRep &alias = l2;
      // Should do nothing.
      DoNotOptimize(alias);
      l2 = std::move(alias);

      CHECK(!backup.IsSmall());
      CHECK(!l2.IsSmall());
      CHECK(mpz_cmp(backup.ConstMpz(), l2.ConstMpz()) == 0);
      // Check that it can be assigned over.
      l2 = l1;
    }

    {
      GmpRep l2 = CreateLarge(large2_val);
      GmpRep &alias = l2;
      DoNotOptimize(alias);
      l2 = std::move(alias);
      // Check that it can be destroyed.
    }
  }
}

static void Swaps() {
  const char *large1_val = "1111111111555551111111111111111";

  // Swap
  {
    GmpRep s(123), l = CreateLarge(large1_val);
    GmpRep s_orig(s), l_orig(l);

    s.Swap(&l);
    CHECK_LARGE(s, large1_val);
    CHECK_SMALL(l, 123);

    s.Swap(&l);
    CHECK_SMALL(s, 123);
    CHECK_LARGE(l, large1_val);
  }
}

#define CHECK_HAS_INTERIOR_PTR(obj) do {                                \
    const void *ptr = obj.ConstMpz();                                   \
    CHECK(ptr != nullptr);                                              \
    CHECK(ptr >= (void*)&obj);                                          \
    CHECK(ptr < (void*)((const char *)&obj +                            \
                        sizeof (GmpRep::Lease))) <<                     \
      "mpz pointer should point into lease.";                           \
  } while (0)

static void Leases() {
  // Lease from small.
  {
    const GmpRep s(12345);
    GmpRep::Lease lease(s);
    CHECK(mpz_cmp_si(lease.ConstMpz(), 12345) == 0);
    // Lease should have its own allocation, not point into s
    CHECK_HAS_INTERIOR_PTR(lease);
  }

  // Lease from large.
  {
    const GmpRep l = CreateLarge("54321");
    GmpRep::Lease lease(l);
    CHECK(mpz_cmp_si(lease.ConstMpz(), 54321) == 0);
    // Lease should point into l's allocation
    CHECK(lease.ConstMpz() == l.ConstMpz());
  }

  // Move lease from small.
  {
    const GmpRep s(12345);
    GmpRep::Lease l1(s);

    GmpRep::Lease l2(std::move(l1));
    CHECK(l2.ConstMpz() != nullptr);
    CHECK(mpz_cmp_si(l2.ConstMpz(), 12345) == 0);
    // The new lease should point to its own object, not the old one.
    CHECK_HAS_INTERIOR_PTR(l2);
  }

  // Move-assign lease.
  {
    const GmpRep s1(123), s2(456);
    const GmpRep l1 = CreateLarge("11188888888888800000001");
    const GmpRep l2 = CreateLarge("222");

    GmpRep::Lease lease_l(l1);
    GmpRep::Lease lease_s(s1);
    lease_l = std::move(lease_s);
    CHECK(mpz_cmp_si(lease_l.ConstMpz(), 123) == 0);

    GmpRep::Lease lease_s2(s2);
    GmpRep::Lease lease_l2(l2);
    lease_s2 = std::move(lease_l2);
    CHECK(mpz_cmp_si(lease_s2.ConstMpz(), 222) == 0);
    CHECK(lease_s2.ConstMpz() == l2.ConstMpz()) << "Should borrow pointer";
  }

  {
    const GmpRep l1 = CreateLarge("11188888888888800000001");
    GmpRep::Lease lease_l(l1);
    // self-move via alias to suppress warning.
    GmpRep::Lease &tmp = lease_l;
    DoNotOptimize(tmp);
    lease_l = std::move(tmp);
    CHECK(mpz_cmp_si(lease_l.ConstMpz(), 12345) == 1);
    CHECK(!l1.IsSmall());
    CHECK(mpz_cmp_si(l1.ConstMpz(), 12345) == 1);
  }

  {
    const GmpRep s1(12345);
    GmpRep::Lease lease_s(s1);
    // self-move via alias to suppress warning.
    GmpRep::Lease &tmp = lease_s;
    DoNotOptimize(tmp);
    lease_s = std::move(tmp);
    CHECK(mpz_cmp_si(lease_s.ConstMpz(), 12345) == 0);
    CHECK(s1.IsSmall());
    CHECK(s1.GetSmall() == 12345);
  }
}

static void RatAssignment() {
  const char *large_n = "1111111111111111111111111111111";
  const char *large_d = "1111111111111111111111111111113";
  const char *large2_n = "2222222222222222222222222222222";
  const char *large2_d = "2222222222222222222222222222223";

  // Copy assignment
  {
    GmpRepRat s1(1, 2), s2(3, 4);
    GmpRepRat l1 = CreateLargeRat(large_n, large_d);
    GmpRepRat l2 = CreateLargeRat(large2_n, large2_d);

    s1 = s2;
    CHECK_SMALL_RAT(s1, 3, 4);

    l1 = l2;
    CHECK_LARGE_RAT(l1, large2_n, large2_d);

    l1 = s1;
    CHECK_SMALL_RAT(l1, 3, 4);

    s1 = l2;
    CHECK_LARGE_RAT(s1, large2_n, large2_d);
  }

  // Move assignment
  {
    GmpRepRat s1(1, 2);
    s1 = GmpRepRat(3, 4);
    CHECK_SMALL_RAT(s1, 3, 4);

    GmpRepRat l1 = CreateLargeRat(large_n, large_d);
    l1 = GmpRepRat(5, 6);
    CHECK_SMALL_RAT(l1, 5, 6);

    GmpRepRat s2(7, 8);
    s2 = CreateLargeRat(large2_n, large2_d);
    CHECK_LARGE_RAT(s2, large2_n, large2_d);

    GmpRepRat l2 = CreateLargeRat(large_n, large_d);
    l2 = CreateLargeRat(large2_n, large2_d);
    CHECK_LARGE_RAT(l2, large2_n, large2_d);
  }
}

static void CreateAndDestroy() {
  GmpRep rep;
}

static void RatCreateAndDestroy() {
  GmpRepRat rep;
}


static void RatLifetime() {
  // Default constructor is 0.
  {
    GmpRepRat rep;
    CHECK(rep.IsSmall());
    CHECK(rep.SmallNumer() == 0);
    // Denominator can be anything when numerator is zero.
  }

  // From small int.
  {
    GmpRepRat rep(123);
    CHECK_SMALL_RAT(rep, 123, 1);
  }

  {
    GmpRepRat rep(-123);
    CHECK_SMALL_RAT(rep, -123, 1);
  }

  // From small num/den.
  {
    GmpRepRat rep(3, 7);
    CHECK_SMALL_RAT(rep, 3, 7);
    // Canonicalization
    GmpRepRat rep2(6, 14);
    CHECK_SMALL_RAT(rep2, 3, 7);
    GmpRepRat rep3(3, -7);
    CHECK_SMALL_RAT(rep3, -3, 7);
    GmpRepRat rep4(-6, -14);
    CHECK_SMALL_RAT(rep4, 3, 7);
  }

  const char *large_num = "12345678901234567890";
  const char *large_den = "12345678901234567891";

  // Promote.
  {
    GmpRepRat rep(3, 7);
    rep.Promote();
    CHECK(!rep.IsSmall());
    CHECK(rep.ConstMpq() != nullptr);
    CHECK(rep.Mpq() != nullptr);
  }

  {
    GmpRepRat rep = CreateLargeRat(large_num, large_den);
    CHECK(!rep.IsSmall());
    CHECK(rep.ConstMpq() != nullptr);
    CHECK(rep.Mpq() != nullptr);
  }

  // Edge cases for small num/den
  {
    const int64_t low = std::numeric_limits<int64_t>::lowest();
    // These must promote.
    GmpRepRat r1(1, low);
    CHECK_LARGE_RAT(r1, "1", "-9223372036854775808");
    GmpRepRat r2(-1, low);
    CHECK_LARGE_RAT(r2, "1", "9223372036854775808");
    GmpRepRat r3(low, 1);
    CHECK_LARGE_RAT(r3, "-9223372036854775808", "1");
    GmpRepRat r4(low, -1);
    CHECK_LARGE_RAT(r4, "9223372036854775808", "1");

  }

  // Copy constructor
  {
    GmpRepRat s1(1, 2);
    GmpRepRat s2 = s1;
    CHECK_SMALL_RAT(s1, 1, 2);
    CHECK_SMALL_RAT(s2, 1, 2);

    GmpRepRat l1 = CreateLargeRat(large_num, large_den);
    GmpRepRat l2 = l1;
    CHECK_LARGE_RAT(l1, large_num, large_den);
    CHECK_LARGE_RAT(l2, large_num, large_den);
  }

  // Move constructor
  {
    GmpRepRat s1(3, 4);
    GmpRepRat s2 = std::move(s1);
    CHECK_SMALL_RAT(s2, 3, 4);
    s1 = GmpRepRat(5, 6);
    CHECK_SMALL_RAT(s1, 5, 6);
  }

  {
    GmpRepRat l1 = CreateLargeRat(large_num, large_den);
    GmpRepRat l2 = std::move(l1);
    CHECK_LARGE_RAT(l2, large_num, large_den);
    GmpRepRat s1(5, 6);
    l1 = s1;
    CHECK_SMALL_RAT(l1, 5, 6);
  }

  {
    GmpRepRat l1 = CreateLargeRat(large_num, large_den);
    GmpRepRat &alias = l1;
    DoNotOptimize(alias);
    l1 = std::move(alias);
    CHECK_LARGE_RAT(l1, large_num, large_den);
  }
}

static void RatSwaps() {
  const char *large_num = "11000011111111555551111111111111111";
  const char *large_den = "11111111115555511110000111111111117";

  // Swap
  {
    GmpRepRat s(123);
    GmpRepRat l = CreateLargeRat(large_num, large_den);

    s.Swap(&l);
    CHECK_LARGE_RAT(s, large_num, large_den);
    CHECK_SMALL_RAT(l, 123, 1);

    s.Swap(&l);
    CHECK_SMALL_RAT(s, 123, 1);
    CHECK_LARGE_RAT(l, large_num, large_den);
  }
}

#define CHECK_HAS_INTERIOR_PTR_RAT(obj) do {                            \
    const void *ptr = obj.ConstMpq();                                   \
    CHECK(ptr != nullptr);                                              \
    CHECK(ptr >= (void*)&obj);                                          \
    CHECK(ptr < (void*)((const char *)&obj +                            \
                        sizeof (GmpRepRat::Lease))) <<                  \
      "mpq pointer should point into lease.";                           \
  } while (0)

static void RatLeases() {
  // Lease from small.
  {
    const GmpRepRat s(3, 5);
    GmpRepRat::Lease lease(s);
    CHECK(mpq_cmp_si(lease.ConstMpq(), 3, 5) == 0);
    // Lease should have its own allocation, not point into s.
    CHECK_HAS_INTERIOR_PTR_RAT(lease);
  }

  // Lease from large.
  {
    const GmpRepRat l = CreateLargeRat("54321", "12345");
    GmpRepRat::Lease lease(l);
    CHECK(mpq_cmp(lease.ConstMpq(), l.ConstMpq()) == 0);
    // Lease should point into l's allocation.
    CHECK(lease.ConstMpq() == l.ConstMpq());
  }

  // Move lease from small.
  {
    const GmpRepRat s(7, 8);
    GmpRepRat::Lease l1(s);

    GmpRepRat::Lease l2(std::move(l1));
    CHECK(l2.ConstMpq() != nullptr);
    CHECK(mpq_cmp_si(l2.ConstMpq(), 7, 8) == 0);
    // The new lease should point to its own object, not the old one.
    CHECK_HAS_INTERIOR_PTR_RAT(l2);
  }

  // Move lease from large.
  {
    const GmpRepRat l = CreateLargeRat("888", "999");
    GmpRepRat::Lease l1(l);

    GmpRepRat::Lease l2(std::move(l1));
    CHECK(l2.ConstMpq() != nullptr);
    CHECK(mpq_cmp(l2.ConstMpq(), l.ConstMpq()) == 0);
    // The new lease should point into the original large rep.
    CHECK(l2.ConstMpq() == l.ConstMpq());
  }

  // Move-assign lease.
  {
    const GmpRepRat s1(1, 2), s2(3, 4);
    const GmpRepRat l1 = CreateLargeRat("11111111", "22222223");
    const GmpRepRat l2 = CreateLargeRat("33333333", "44444445");

    GmpRepRat::Lease lease_l(l1);
    GmpRepRat::Lease lease_s(s1);
    lease_l = std::move(lease_s);
    CHECK(mpq_cmp_si(lease_l.ConstMpq(), 1, 2) == 0);
    CHECK_HAS_INTERIOR_PTR_RAT(lease_l); // now has its own alloc

    GmpRepRat::Lease lease_s2(s2);
    GmpRepRat::Lease lease_l2(l2);
    lease_s2 = std::move(lease_l2);
    CHECK(mpq_cmp(lease_s2.ConstMpq(), l2.ConstMpq()) == 0);
    CHECK(lease_s2.ConstMpq() == l2.ConstMpq()) << "Should borrow pointer";
  }

  // Self-move assignment
  {
    const GmpRepRat l1 = CreateLargeRat("1", "2");
    GmpRepRat::Lease lease_l(l1);
    // Self-move via alias to suppress warning.
    GmpRepRat::Lease &tmp = lease_l;
    DoNotOptimize(tmp);
    lease_l = std::move(tmp);
    CHECK(mpq_cmp(lease_l.ConstMpq(), l1.ConstMpq()) == 0);
  }

  {
    const GmpRepRat s1(41, 152); // canonical for 123/456
    GmpRepRat::Lease lease_s(s1);
    // Self-move via alias to suppress warning.
    GmpRepRat::Lease &tmp = lease_s;
    DoNotOptimize(tmp);
    lease_s = std::move(tmp);
    CHECK(mpq_cmp_si(lease_s.ConstMpq(), 41, 152) == 0);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  CreateAndDestroy();
  Lifetime();
  Assignment();
  Swaps();
  Leases();

  RatCreateAndDestroy();
  RatLifetime();
  RatAssignment();
  RatSwaps();
  RatLeases();

  printf("OK\n");
  return 0;
}
