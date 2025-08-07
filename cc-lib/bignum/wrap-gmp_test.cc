
#include <utility>
#define BIG_USE_GMP 1

#include "wrap-gmp.h"

#include <gmp.h>
#include <cstdio>

#include "ansi.h"
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
    lease_l = std::move(tmp);
    CHECK(mpz_cmp_si(lease_l.ConstMpz(), 12345) == 1);
  }

  {
    const GmpRep s1(12345);
    GmpRep::Lease lease_s(s1);
    // self-move via alias to suppress warning.
    GmpRep::Lease &tmp = lease_s;
    lease_s = std::move(tmp);
    CHECK(mpz_cmp_si(lease_s.ConstMpz(), 12345) == 0);
  }

}

static void CreateAndDestroy() {
  GmpRep rep;
}

int main(int argc, char **argv) {
  ANSI::Init();

  CreateAndDestroy();
  Lifetime();
  Assignment();
  Swaps();
  Leases();

  printf("OK\n");
  return 0;
}
