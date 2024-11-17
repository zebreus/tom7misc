
#include "modeling.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "interval-cover.h"
#include "randutil.h"

static std::string ByteSetString(const ByteSet &s) {
  IntervalCover<int> cover(0);
  std::string ret;
  for (uint8_t b : s) {
    cover.SetPoint(b, 1);
    StringAppendF(&ret, " %02x", b);
  }
  StringAppendF(&ret, " (which is:");
  for (uint64_t pt = cover.First(); !cover.IsAfterLast(pt);
       pt = cover.Next(pt)) {
    IntervalCover<int>::Span s = cover.GetPoint(pt);
    if (s.data > 0) {
      StringAppendF(&ret, " %02x" AGREY("-") "%02x",
                    (int)s.start, (int)s.end - 1);
    }
  }
  StringAppendF(&ret, ")");

  return ret;
}

static std::string ByteSet64String(const ByteSet64 &bs) {
  switch (bs.type) {
  case ByteSet64::EMPTY: {
    std::string ret = "EMPTY ";
    for (int i = 0; i < 7; i++) {
      StringAppendF(&ret, "%02x", bs.payload[i]);
    }
    return ret;
  }
  case ByteSet64::VALUES: {
    std::string ret = "VALUES ";
    for (int i = 0; i < 7; i++) {
      StringAppendF(&ret, " %02x", bs.payload[i]);
    }
    return ret;
  }
  case ByteSet64::RANGES: {
    std::string ret = "RANGES ";
    for (int i = 0; i < 6; i += 2) {
      StringAppendF(&ret, " %02x" AGREY("×") "%02x",
                    bs.payload[i],
                    bs.payload[i + 1]);
    }
    StringAppendF(&ret, " (%02x)", bs.payload[6]);
    return ret;
  }
  default:
    return "INVALID";
  }
}

#define CHECK_EQUAL_SETS(s, s64) do {               \
    for (int x = 0; x < 256; x++) {                 \
      CHECK(s.Contains(x) == s64.Contains(x)) <<    \
        "Wrong on " << x << ":\nSet:\n" <<          \
        ByteSetString(s) << "\nS64:\n" <<           \
        ByteSet64String(s64);                       \
    }                                               \
    CHECK(s.Size() == s64.Size());                  \
  } while (0)

// s <= s64
#define CHECK_SUPERSET(s, s64) do {                 \
    for (int x = 0; x < 256; x++) {                 \
      if (s.Contains(x)) {                          \
      CHECK(s64.Contains(x)) << "Wrong on " <<      \
        x << ":\nSet:\n" << ByteSetString(s) <<     \
        "\nS64:\n" << ByteSet64String(s64);         \
      }                                             \
    }                                               \
  } while (0)


static void TestByteSetBasic() {
  {
    ByteSet s;
    CHECK(s.Empty());
    for (int i = 0; i < 256; i++) {
      CHECK(!s.Contains(i));
    }
    CHECK(s.begin() == s.end());

    s.Add(42);
    CHECK(!s.Empty());
    for (int i = 0; i < 256; i++) {
      if (s.Contains(i)) {
        CHECK(i == 42) << i;
      }
    }
    CHECK(s.begin() != s.end());
    CHECK(*s.begin() == 42);
    auto it = s.begin();
    ++it;
    CHECK(it == s.end());
  }
}

static void TestByteSetMultiple() {
  ByteSet s;
  s.Add(10);
  CHECK_EQ(s.Size(), 1);
  s.Add(20);
  CHECK_EQ(s.Size(), 2);
  s.Add(255);
  CHECK_EQ(s.Size(), 3);
  CHECK(s.Contains(10));
  CHECK(s.Contains(20));
  CHECK(s.Contains(255));
  CHECK(!s.Contains(15));

  auto it = s.begin();
  CHECK(it != s.end());
  CHECK(*it == 10);

  ++it;
  CHECK(it != s.end());
  CHECK(*it == 20);

  ++it;
  CHECK(it != s.end());
  CHECK(*it == 255);

  ++it;
  CHECK(it == s.end());
}

static void TestByteSet64Empty() {
  ByteSet empty_set;
  ByteSet64 bs64(empty_set);
  CHECK_EQ(bs64.type, ByteSet64::EMPTY);
}

static void TestByteSet64Singleton() {
  for (int b = 0; b < 256; b++) {
    ByteSet singleton_set;
    singleton_set.Add(b);
    ByteSet64 bs64(singleton_set);
    // Should be able to represent all singletons exactly.
    CHECK_EQ(bs64.type, ByteSet64::VALUES);
    CHECK(bs64.Contains(b));
    for (int i = 0; i < 256; i++) {
      if (bs64.Contains(i)) {
        CHECK(i == b);
      }
    }
  }
}

static void TestByteSet64OneMissing() {
  for (int b = 0; b < 256; b++) {
    ByteSet almost_set;
    for (int i = 0; i < 256; i++) {
      if (i != b) almost_set.Add(i);
    }
    ByteSet64 bs64(almost_set);

    auto Error = [&]() {
        return StringPrintf("\nb %02x.\n"
                            "set:\n%s\n"
                            "set64:\n%s\n",
                            b,
                            ByteSetString(almost_set).c_str(),
                            ByteSet64String(bs64).c_str());
      };

    // Should be able to represent these exactly using ranges.
    CHECK_EQ(bs64.type, ByteSet64::RANGES) << Error();
    CHECK_EQUAL_SETS(almost_set, bs64);
  }
}

static void TestByteSet64Pair() {
  for (uint8_t b1 : { 0, 1, 2, 42, 99, 254, 255 }) {
    for (int b2 = 0; b2 < 256; b2++) {
      ByteSet s;
      s.Add(b1);
      // note b2 may equal b1.
      s.Add(b2);
      ByteSet64 bs64(s);
      // Should be able to represent all pairs exactly.
      CHECK_EQ(bs64.type, ByteSet64::VALUES);
      auto Error = [&]() {
          return StringPrintf("\nb1 %02x b2 %02x.\n"
                              "set:\n%s\n"
                              "set64:\n%s\n",
                              b1, b2,
                              ByteSetString(s).c_str(),
                              ByteSet64String(bs64).c_str());
        };
      CHECK(bs64.Contains(b1)) << b1 << Error();
      CHECK(bs64.Contains(b2)) << b2 << Error();
      for (int i = 0; i < 256; i++) {
        if (bs64.Contains(i)) {
          CHECK(i == b1 || i == b2);
        }
      }
    }
  }
}

static void TestByteSet64Universal() {
  ByteSet top;
  for (int i = 0; i < 256; i++) top.Add(i);
  ByteSet64 top64(top);
  CHECK(top64.Size() == 256);
}

static void TestByteSet64Intervals() {
  ArcFour rc("ivals");

  for (int i = 0; i < 100000; i++) {
    int n = 1 + RandTo(&rc, 5);

    ByteSet s;
    while (n--) {
      uint8_t start = rc.Byte();
      uint8_t len = rc.Byte();
      for (int b = 0; b < len; b++) {
        s.Add((start + b) & 0xFF);
      }
    }

    ByteSet64 s64(s);
    CHECK_SUPERSET(s, s64);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestByteSetBasic();
  TestByteSetMultiple();

  TestByteSet64Empty();
  TestByteSet64Singleton();
  TestByteSet64Pair();
  TestByteSet64Universal();
  TestByteSet64OneMissing();
  TestByteSet64Intervals();

  printf("OK\n");
  return 0;
}
