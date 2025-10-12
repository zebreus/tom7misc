
#include "modeling.h"

#include <format>

#include "ansi.h"
#include "base/print.h"
#include "byte-set.h"

#define N_FLAG 0x80
#define V_FLAG 0x40
#define U_FLAG 0x20
#define B_FLAG 0x10
#define D_FLAG 0x08
#define I_FLAG 0x04
#define Z_FLAG 0x02
#define C_FLAG 0x01

static constexpr uint8_t CAN_BE_SET = 0b01;
static constexpr uint8_t CAN_BE_CLEAR = 0b10;

#define ASSERT_FLAG(reg_P, flag, props) do {                        \
    bool found_set = false, found_clear = false;                    \
    for (uint8_t f : (reg_P)) {                                     \
      if (f & (flag)) found_set = true;                             \
      else found_clear = true;                                      \
    }                                                               \
    bool can_be_set = (props) & CAN_BE_SET;                         \
    bool can_be_clear = (props) & CAN_BE_CLEAR;                     \
    CHECK(!(reg_P).Empty() &&                                       \
          found_set == can_be_set &&                                \
          found_clear == can_be_clear) <<                           \
      std::format(("\n"                                             \
                   ARED("FAILED") "\n"                              \
                   "At: " AWHITE("{}") ":" AYELLOW("{}") " ({})\n"  \
                   "Flags: {} ({})\n"                               \
                   "found: {}{}\n"                                  \
                   "want: {}{}\n"),                                 \
                  __FILE__, __LINE__, __func__,                     \
                  (reg_P).DebugString(), (reg_P).Size(),            \
                  found_set ? "1" : "", found_clear ? "0" : "",     \
                  can_be_set ? "1" : "", can_be_clear ? "0" : "");  \
  } while (0)

static void TestADC() {
  {
    // 3 + 4 + carry=1
    State state;
    state.A = ByteSet::Singleton(0x03);
    state.P = ByteSet::Singleton(C_FLAG | V_FLAG | N_FLAG | Z_FLAG);
    ByteSet values = ByteSet::Singleton(0x04);
    Modeling::AddWithCarry(&state, values);
    ASSERT_FLAG(state.P, V_FLAG, CAN_BE_CLEAR);
    // Consumes the carry.
    ASSERT_FLAG(state.P, C_FLAG, CAN_BE_CLEAR);
    // Not negative, nor zero.
    ASSERT_FLAG(state.P, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, Z_FLAG, CAN_BE_CLEAR);
    CHECK(state.A.Size() == 1 &&
          state.A.GetSingleton() == 0x08);
  }

  {
    // Overflows by one (positive).
    State state;
    state.A = ByteSet::Singleton(0x7F);
    state.P = ByteSet::Singleton(0x00);
    ByteSet values = ByteSet::Singleton(0x01);
    Modeling::AddWithCarry(&state, values);
    ASSERT_FLAG(state.P, Z_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, N_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, C_FLAG, CAN_BE_CLEAR);
    CHECK(state.A.Size() == 1 &&
          state.A.GetSingleton() == 0x80);
  }

  {
    // Overflows by one (negative). Check with and without carry.
    State state;
    state.A = ByteSet::Singleton(0x80);
    state.P.Add(0x00);
    state.P.Add(C_FLAG);
    ByteSet values = ByteSet::Singleton(0x80);
    Modeling::AddWithCarry(&state, values);

    ASSERT_FLAG(state.P, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, Z_FLAG, CAN_BE_SET | CAN_BE_CLEAR);
    CHECK(state.A.Size() == 2);
    CHECK(state.A.Contains(0x00));
    CHECK(state.A.Contains(0x01));
  }

  {
    // Subtle: If we have A=0x7F with carry, then A+C is 0x80,
    // which is negative. We need to detect carry in terms of the
    // original sign of A, but an earlier version of AddWithCarry
    // would look at the sign of A+C.
    State state;
    state.A = ByteSet::Singleton(0x7F);
    state.P = ByteSet::Singleton(C_FLAG);
    ByteSet values = ByteSet::Singleton(0x03);
    Modeling::AddWithCarry(&state, values);

    ASSERT_FLAG(state.P, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, C_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, N_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, Z_FLAG, CAN_BE_CLEAR);
    CHECK(state.A.Size() == 1 &&
          state.A.GetSingleton() == 0x83);
  }
}

static void TestSBC() {
  {
    // 5 - 2 with no borrow (C=1).
    State state;
    state.A = ByteSet::Singleton(0x05);
    state.P = ByteSet::Singleton(C_FLAG);
    ByteSet values = ByteSet::Singleton(0x02);
    Modeling::SubtractWithCarry(&state, values);

    // 5 - 2 = 3. No overflow. No borrow needed.
    ASSERT_FLAG(state.P, V_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, Z_FLAG, CAN_BE_CLEAR);
    CHECK(state.A.Size() == 1 &&
          state.A.GetSingleton() == 0x03);
  }

  {
    // 2 - 5 with no borrow (C=1).
    State state;
    state.A = ByteSet::Singleton(0x02);
    state.P = ByteSet::Singleton(C_FLAG);
    ByteSet values = ByteSet::Singleton(0x05);
    Modeling::SubtractWithCarry(&state, values);

    // 2 - 5 = -3 (0xFD). No overflow. Borrow was needed.
    ASSERT_FLAG(state.P, V_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, C_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, N_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, Z_FLAG, CAN_BE_CLEAR);
    CHECK(state.A.Size() == 1 &&
          state.A.GetSingleton() == 0xFD);
  }

  {
    // Overflow: positive - negative.
    // 127 - (-1) with no borrow (C=1).
    // 0x7F - 0xFF = 0x80 (-128). Positive - negative gave negative.
    State state;
    state.A = ByteSet::Singleton(0x7F);
    state.P = ByteSet::Singleton(C_FLAG);
    ByteSet values = ByteSet::Singleton(0xFF);
    Modeling::SubtractWithCarry(&state, values);

    ASSERT_FLAG(state.P, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, C_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, N_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, Z_FLAG, CAN_BE_CLEAR);
    CHECK(state.A.Size() == 1 &&
          state.A.GetSingleton() == 0x80);
  }

  {
    // Overflow: negative - positive.
    // -128 - 1 with no borrow (C=1).
    // 0x80 - 0x01 = 0x7F (127). Negative - positive gave positive.
    State state;
    state.A = ByteSet::Singleton(0x80);
    state.P = ByteSet::Singleton(C_FLAG);
    ByteSet values = ByteSet::Singleton(0x01);
    Modeling::SubtractWithCarry(&state, values);

    ASSERT_FLAG(state.P, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, Z_FLAG, CAN_BE_CLEAR);
    CHECK(state.A.Size() == 1 &&
          state.A.GetSingleton() == 0x7F);
  }

  {
    // Subtle overflow bug case, like in ADC.
    // A=-128 (0x80), M=1 (0x01), C=0 (borrow).
    // The operation is A - M - 1, which is -128 - 1 - 1 = -130.
    State state;
    state.A = ByteSet::Singleton(0x80);
    state.P = ByteSet::Singleton(0x00);
    ByteSet values = ByteSet::Singleton(0x01);
    Modeling::SubtractWithCarry(&state, values);

    ASSERT_FLAG(state.P, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state.P, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state.P, Z_FLAG, CAN_BE_CLEAR);
    CHECK(state.A.Size() == 1 &&
          state.A.GetSingleton() == 0x7E);
  }
}

static void TestRotateRight() {
  // TODO: Use ASSERT_FLAG here.


  // Simple right shift, no carry in/out.
  {
    State state;
    state.P = ByteSet::Singleton(0x00);
    ByteSet src = ByteSet::Singleton(0b00000010);
    auto [res, flags] = Modeling::RotateRight(state, src);
    CHECK(res == ByteSet::Singleton(0b00000001));
    // C=0, Z=0, N=0
    CHECK(flags == ByteSet::Singleton(0x00));
  }

  // Carry out, no carry in.
  {
    State state;
    state.P = ByteSet::Singleton(0x00);
    ByteSet src = ByteSet::Singleton(0b00000011);
    auto [res, flags] = Modeling::RotateRight(state, src);
    CHECK(res == ByteSet::Singleton(0b00000001));
    // C=1, Z=0, N=0
    CHECK(flags == ByteSet::Singleton(C_FLAG));
  }

  // Carry in, no carry out.
  {
    State state;
    state.P = ByteSet::Singleton(C_FLAG);
    ByteSet src = ByteSet::Singleton(0b00000010);
    auto [res, flags] = Modeling::RotateRight(state, src);
    CHECK(res == ByteSet::Singleton(0b10000001));
    // C=0, Z=0, N=1
    CHECK(flags == ByteSet::Singleton(N_FLAG));
  }

  // Zero result.
  {
    State state;
    state.P = ByteSet::Singleton(0x00); // Carry clear
    ByteSet src = ByteSet::Singleton(0b00000001);
    auto [res, flags] = Modeling::RotateRight(state, src);
    CHECK(res == ByteSet::Singleton(0x00));
    // C=1, Z=1, N=0
    CHECK(flags == ByteSet::Singleton(C_FLAG | Z_FLAG));
  }

  // Indeterminate carry.
  {
    State state;
    state.P = ByteSet({0x00, C_FLAG});
    ByteSet src = ByteSet::Singleton(0b00000010);
    auto [res, flags] = Modeling::RotateRight(state, src);
    CHECK(res == ByteSet({0b00000001, 0b10000001}));
    // Case C=0: C_out=0, N=0, Z=0. flags=0x00
    // Case C=1: C_out=0, N=1, Z=0. flags=N_FLAG
    CHECK(flags == ByteSet({0x00, N_FLAG}));
  }

  // Indeterminate source.
  {
    State state;
    state.P = ByteSet::Singleton(0x00);
    ByteSet src = ByteSet({0b10, 0b11});
    auto [res, flags] = Modeling::RotateRight(state, src);
    CHECK(res == ByteSet::Singleton(0b01));
    // Case src=0b10: C_out=0, N=0, Z=0. flags=0x00
    // Case src=0b11: C_out=1, N=0, Z=0. flags=C_FLAG
    CHECK(flags == ByteSet({0x00, C_FLAG}));
  }

  // Indeterminate source and carry.
  {
    State state;
    state.P = ByteSet({0x00, C_FLAG});
    ByteSet src = ByteSet({0x00, 0x01});
    auto [res, flags] = Modeling::RotateRight(state, src);
    CHECK(res == ByteSet({0x00, 0x80}));
    // ROR 0x00, C_in=0 -> res=0x00, flags={C=0,Z=1,N=0} -> Z_FLAG
    // ROR 0x00, C_in=1 -> res=0x80, flags={C=0,Z=0,N=1} -> N_FLAG
    // ROR 0x01, C_in=0 -> res=0x00, flags={C=1,Z=1,N=0} -> C_FLAG|Z_FLAG
    // ROR 0x01, C_in=1 -> res=0x80, flags={C=1,Z=0,N=1} -> C_FLAG|N_FLAG
    CHECK(flags == ByteSet({Z_FLAG, N_FLAG,
                            C_FLAG | Z_FLAG, C_FLAG | N_FLAG}));
  }
}

static void TestRotateLeft() {
  // Simple left shift, no carry in/out.
  {
    State state;
    state.P = ByteSet::Singleton(0x00); // Carry clear
    auto [res, flags] =
      Modeling::RotateLeft(state, ByteSet::Singleton(0b01000000));
    CHECK(res == ByteSet::Singleton(0b10000000));
    ASSERT_FLAG(flags, C_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(flags, Z_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(flags, N_FLAG, CAN_BE_SET);
  }

  // Carry out, no carry in.
  {
    State state;
    state.P = ByteSet::Singleton(0x00); // Carry clear
    auto [res, flags] =
      Modeling::RotateLeft(state, ByteSet::Singleton(0b10000000));
    CHECK(res == ByteSet::Singleton(0x00));
    ASSERT_FLAG(flags, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(flags, Z_FLAG, CAN_BE_SET);
    ASSERT_FLAG(flags, N_FLAG, CAN_BE_CLEAR);
  }

  // Carry in, no carry out.
  {
    State state;
    state.P = ByteSet::Singleton(C_FLAG); // Carry set
    auto [res, flags] =
      Modeling::RotateLeft(state, ByteSet::Singleton(0b01000000));
    CHECK(res == ByteSet::Singleton(0b10000001));
    ASSERT_FLAG(flags, C_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(flags, Z_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(flags, N_FLAG, CAN_BE_SET);
  }

  // Indeterminate zero flag because carry flag is indeterminate.
  {
    State state;
    state.P = ByteSet({0x00, C_FLAG});
    auto [res, flags] =
      Modeling::RotateLeft(state, ByteSet::Singleton(0b10000000));

    CHECK(res == ByteSet({0x00, 0x01}));

    // Carry always becomes the 1 bit shifted out.
    ASSERT_FLAG(flags, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(flags, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(flags, Z_FLAG, CAN_BE_SET | CAN_BE_CLEAR);
  }

  {
    State state;
    state.P = ByteSet({0x00, C_FLAG});
    auto [res, flags] =
      Modeling::RotateLeft(state, ByteSet({0x00, 0x80}));

    CHECK(res == ByteSet({0x00, 0x01}));

    ASSERT_FLAG(flags, C_FLAG, CAN_BE_SET | CAN_BE_CLEAR);
    ASSERT_FLAG(flags, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(flags, Z_FLAG, CAN_BE_SET | CAN_BE_CLEAR);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestADC();
  TestSBC();
  TestRotateRight();
  TestRotateLeft();

  Print("OK\n");
  return 0;
}
