#include "solution.h"

#include <string>

#include "level-base.h"
#include "base/logging.h"
#include "base/print.h"


static void TestRoundTrip() {
  {
    Solution empty = Solution::Empty();
    std::string s = empty.ToString();
    Solution out;
    CHECK(Solution::FromString(s, &out));
    CHECK(out.IsEmpty());
    CHECK(Solution::Equal(empty, out));
  }

  {
    Solution sol;
    sol.Append(DIR_UP);
    sol.Append(DIR_DOWN);
    sol.Append(DIR_LEFT);
    sol.Append(DIR_RIGHT);
    sol.Append(DIR_UP);

    std::string s = sol.ToString();
    Solution out;
    CHECK(Solution::FromString(s, &out));
    CHECK(Solution::Equal(sol, out));
    CHECK(out.Length() == 5);
    CHECK(out.At(0) == DIR_UP);
    CHECK(out.At(1) == DIR_DOWN);
    CHECK(out.At(2) == DIR_LEFT);
    CHECK(out.At(3) == DIR_RIGHT);
    CHECK(out.At(4) == DIR_UP);
  }
}

int main(int argc, char **argv) {

  TestRoundTrip();

  Print("OK\n");
  return 0;
}
