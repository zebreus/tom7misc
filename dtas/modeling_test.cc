
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

#define ASSERT_FLAG(state, flag, props) do {                        \
    bool found_set = false, found_clear = false;                    \
    for (uint8_t f : (state).P) {                                   \
      if (f & (flag)) found_set = true;                             \
      else found_clear = true;                                      \
    }                                                               \
    bool can_be_set = (props) & CAN_BE_SET;                         \
    bool can_be_clear = (props) & CAN_BE_CLEAR;                     \
    CHECK(!(state).P.Empty() &&                                     \
          found_set == can_be_set &&                                \
          found_clear == can_be_clear) <<                           \
      std::format(("\n"                                             \
                   ARED("FAILED") "\n"                              \
                   "At: " AWHITE("{}") ":" AYELLOW("{}") " ({})\n"  \
                   "Flags: {} ({})\n"                               \
                   "found: {}{}\n"                                  \
                   "want: {}{}\n"),                                 \
                  __FILE__, __LINE__, __func__,                     \
                  (state).P.DebugString(), (state).P.Size(),        \
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
    ASSERT_FLAG(state, V_FLAG, CAN_BE_CLEAR);
    // Consumes the carry.
    ASSERT_FLAG(state, C_FLAG, CAN_BE_CLEAR);
    // Not negative, nor zero.
    ASSERT_FLAG(state, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, Z_FLAG, CAN_BE_CLEAR);
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
    ASSERT_FLAG(state, Z_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, N_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, C_FLAG, CAN_BE_CLEAR);
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

    ASSERT_FLAG(state, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, Z_FLAG, CAN_BE_SET | CAN_BE_CLEAR);
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

    ASSERT_FLAG(state, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, C_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, N_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, Z_FLAG, CAN_BE_CLEAR);
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
    ASSERT_FLAG(state, V_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, Z_FLAG, CAN_BE_CLEAR);
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
    ASSERT_FLAG(state, V_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, C_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, N_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, Z_FLAG, CAN_BE_CLEAR);
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

    ASSERT_FLAG(state, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, C_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, N_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, Z_FLAG, CAN_BE_CLEAR);
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

    ASSERT_FLAG(state, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, Z_FLAG, CAN_BE_CLEAR);
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

    ASSERT_FLAG(state, V_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, C_FLAG, CAN_BE_SET);
    ASSERT_FLAG(state, N_FLAG, CAN_BE_CLEAR);
    ASSERT_FLAG(state, Z_FLAG, CAN_BE_CLEAR);
    CHECK(state.A.Size() == 1 &&
          state.A.GetSingleton() == 0x7E);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestADC();
  TestSBC();

  Print("OK\n");
  return 0;
}
