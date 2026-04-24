
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


int main(int argc, char **argv) {
  ANSI::Init();

  CreateAndDestroy();

  EZ1();
  TestViews();
  TestSub();
  TestConstructor();

  printf("OK\n");
  return 0;
}
