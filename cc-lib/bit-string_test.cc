
#include "bit-string.h"

#include <cstdio>
#include <vector>
#include <cstdint>

#include "ansi.h"
#include "base/logging.h"

static void CreateAndDestroy() {
  BitString bb;
}

static void EZ1() {
  BitString bb;
  bb.WriteBit(true);
  bb.WriteBit(false);
  bb.WriteBit(true);

  {
    CHECK(bb.NumBits() == 3);
    std::vector<uint8_t> b = bb.GetBytes();
    CHECK(b.size() == 1);
    CHECK(b[0] == 0b10100000);
  }

  bb.WriteBits(6, 0b110011);

  {
    CHECK(bb.NumBits() == 9);
    std::vector<uint8_t> b = bb.GetBytes();
    CHECK(b.size() == 2);
    CHECK(b[0] == 0b10111001);
    CHECK(b[1] == 0b10000000);
  }

  bb.WriteBits(7, 0b00110011);
  {
    CHECK(bb.NumBits() == 16);
    std::vector<uint8_t> b = bb.GetBytes();
    CHECK(b.size() == 2);
    CHECK(b[0] == 0b10111001);
    CHECK(b[1] == 0b10110011);
  }

  bb.WriteBit(1);
  bb.WriteBit(0);
  bb.WriteBit(1);
  CHECK(bb.NumBits() == 19);

  bb.Clear(true);
  CHECK(bb.NumBits() == 19);
  for (int i = 0; i < (int)bb.NumBits(); i++) {
    CHECK(bb[i] == true);
  }

  bb.Clear(false);
  CHECK(bb.NumBits() == 19);
  for (int i = 0; i < (int)bb.NumBits(); i++) {
    CHECK(bb[i] == false);
  }
}

static void TestViews() {
  BitString bb;
  bb.WriteBits(8, 0b10100011);

  {
    BitStringView view = bb.View();
    CHECK(view.Size() == 8);
    CHECK(view.Get(0) == true);
    CHECK(view.Get(1) == false);
    CHECK(view.Get(2) == true);
    CHECK(view.Get(7) == true);

    view.RemovePrefix(2);
    CHECK(view.Size() == 6);
    CHECK(view.Get(0) == true);
    CHECK(view.Get(1) == false);

    view.RemoveSuffix(1);
    CHECK(view.Size() == 5);
  }

  {
    const BitString &cbb = bb;
    BitStringConstView cview = cbb.View();
    CHECK(cview.Size() == 8);
    CHECK(cview[0] == true);
    CHECK(cview[1] == false);

    cview.RemovePrefix(1);
    CHECK(cview.Size() == 7);
    CHECK(cview[0] == false);
    CHECK(cview[1] == true);

    cview.RemoveSuffix(2);
    CHECK(cview.Size() == 5);
  }

  {
    // Implicit conversion.
    BitStringConstView cview = bb.View();
    CHECK(cview.Size() == 8);
  }

  {
    // Test aliasing. Modifications through the
    // view should be reflected in the original
    // bb.
    BitStringView view = bb.View();
    view.RemovePrefix(2);

    view.Set(0, false);
    CHECK(bb.Get(2) == false);

    view.Set(1, true);
    CHECK(bb.Get(3) == true);

    CHECK(view.Get(0) == false);
    CHECK(view.Get(1) == true);
  }
}

static void TestSub() {
  BitString bb;
  bb.WriteBits(8, 0b11001010);
  bb.WriteBits(8, 0b01100011);

  {
    BitStringView view = bb.View();
    CHECK(view.Size() == 16);

    BitStringView sub_all = view.Sub(0);
    CHECK(sub_all.Size() == 16);
    CHECK(sub_all.Get(0) == true);
    CHECK(sub_all.Get(15) == true);

    BitStringView sub1 = view.Sub(4, 8);
    CHECK(sub1.Size() == 8);
    CHECK(sub1.Get(0) == true);
    CHECK(sub1.Get(1) == false);
    CHECK(sub1.Get(2) == true);
    CHECK(sub1.Get(3) == false);
    CHECK(sub1.Get(4) == false);
    CHECK(sub1.Get(5) == true);
    CHECK(sub1.Get(6) == true);
    CHECK(sub1.Get(7) == false);

    // With length == npos.
    BitStringView sub2 = sub1.Sub(2);
    CHECK(sub2.Size() == 6);
    CHECK(sub2.Get(0) == true);
    CHECK(sub2.Get(1) == false);
    CHECK(sub2.Get(2) == false);
    CHECK(sub2.Get(3) == true);
    CHECK(sub2.Get(4) == true);
    CHECK(sub2.Get(5) == false);
  }

  {
    const BitString &cbb = bb;
    BitStringConstView cview = cbb.View();

    BitStringConstView csub1 = cview.Sub(4, 8);
    CHECK(csub1.Size() == 8);
    CHECK(csub1[0] == true);
    CHECK(csub1[1] == false);
    CHECK(csub1[5] == true);
    CHECK(csub1[7] == false);

    BitStringConstView csub2 = csub1.Sub(2);
    CHECK(csub2.Size() == 6);
    CHECK(csub2[0] == true);
    CHECK(csub2[1] == false);
    CHECK(csub2[5] == false);
  }

  {
    BitStringView view = bb.View();
    // With a const reference, we will only be able
    // to extract the ConstView.
    const BitStringView &cview = view;

    BitStringConstView csub = cview.Sub(4, 8);
    CHECK(csub.Size() == 8);
    CHECK(csub[0] == true);
    CHECK(csub[1] == false);
  }

  {
    BitString mut_bb;
    mut_bb.WriteBits(8, 0b00000000);

    BitStringView view = mut_bb.View();
    BitStringView sub = view.Sub(2, 4);
    CHECK(sub.Size() == 4);

    // Make sure that they alias the parent.
    sub.Set(0, true);
    sub.Set(3, true);

    CHECK(mut_bb.Get(2) == true);
    CHECK(mut_bb.Get(5) == true);
    CHECK(mut_bb.Get(0) == false);
    CHECK(mut_bb.Get(7) == false);
  }
}

static void TestConstructor() {
  {
    BitString bb(8, true);
    CHECK(bb.NumBits() == 8);
    CHECK(bb.Get(0) == true);
    CHECK(bb.Get(7) == true);
  }

  {
    BitString bb(9, false);
    CHECK(bb.NumBits() == 9);
    CHECK(bb.Get(0) == false);
    CHECK(bb.Get(8) == false);
  }
}

static void TestComparisons() {
  BitString bb1;
  bb1.WriteBits(4, 0b1010);
  BitStringView view1 = bb1.View();

  BitString bb2;
  bb2.WriteBits(4, 0b1010);
  BitStringConstView cview2 = bb2.View();

  BitString bb3;
  bb3.WriteBits(4, 0b1100);

  BitString bb4;
  bb4.WriteBits(5, 0b10100);

  BitString bb5;
  bb5.WriteBits(3, 0b110);

  // Compare a BitString with a BitStringView.
  CHECK(bb2 == view1);
  CHECK(view1 == bb2);
  CHECK(bb3 != view1);
  CHECK(bb3 > view1);
  CHECK(bb4 > view1);
  CHECK(bb5 > view1);
  CHECK(view1 < bb5);

  // Compare a BitStringView with a BitStringConstView.
  CHECK(view1 == cview2);
  CHECK(cview2 == view1);

  BitStringView view3 = bb3.View();
  CHECK(view3 != cview2);
  CHECK(view3 > cview2);

  BitStringView view4 = bb4.View();
  CHECK(view4 > cview2);

  BitStringView view5 = bb5.View();
  CHECK(view5 > cview2);
}

static void TestASCII() {
  auto check_round_trip = [](const BitString &bb) {
    std::string ascii = bb.ToASCII();
    std::optional<BitString> parsed = BitString::FromASCII(ascii);
    CHECK(parsed.has_value());
    CHECK(parsed.value() == bb);
  };

  {
    BitString empty;
    check_round_trip(empty);
    CHECK(empty.ToASCII() == "0.");
  }

  for (int len = 1; len < 32; len++) {
    BitString zeros(len, false);
    check_round_trip(zeros);

    BitString ones(len, true);
    check_round_trip(ones);

    BitString alternating;
    for (int i = 0; i < len; i++) {
      alternating.WriteBit((i % 2) == 0);
    }
    check_round_trip(alternating);
  }

  {
    BitString large;
    for (int i = 0; i < 1000; i++) {
      large.WriteBit((i % 3) == 0);
    }
    check_round_trip(large);
  }

  {
    // Test that canonical representation is enforced.
    // 'h' is 33 (100001 in binary). If length is 1, only the first bit
    // is used, so the last bit being 1 makes it non-canonical.
    std::optional<BitString> parsed = BitString::FromASCII("1.h");
    CHECK(!parsed.has_value());
  }

  {
    // Missing dot.
    std::optional<BitString> parsed = BitString::FromASCII("1");
    CHECK(!parsed.has_value());

    // Invalid character.
    parsed = BitString::FromASCII("1.?");
    CHECK(!parsed.has_value());

    // Incorrect length of base64 data.
    parsed = BitString::FromASCII("1.AA");
    CHECK(!parsed.has_value());
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  CreateAndDestroy();

  EZ1();
  TestViews();
  TestSub();
  TestConstructor();
  TestComparisons();
  TestASCII();

  printf("OK\n");
  return 0;
}
