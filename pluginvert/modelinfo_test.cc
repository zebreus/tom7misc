
#include "modelinfo.h"

#include <string>

#include "base/logging.h"
#include "base/stringprintf.h"

using namespace std;
using Intervals = ModelInfo::Intervals;

static string ToString(const Intervals &iv) {
  string ret;
  for (const auto &[start, length] : iv.ivals) {
    StringAppendF(&ret, "[%d,%d]", start, length);
  }
  return ret;
}

#define CHECK_IVALS(s) do { \
  string actual = ToString(ivals); \
  CHECK(actual == (s)) << "\ngot.....: " << actual \
                       << "\nexpected: " << (s);   \
} while (0)

static void TestIntervals() {
  Intervals ivals(4);

  CHECK_IVALS("");

  ivals.Add(10);
  CHECK_IVALS("[10,1]");

  ivals.Add(20);
  CHECK_IVALS("[10,1][20,1]");

  ivals.Add(2);
  CHECK_IVALS("[2,1][10,1][20,1]");

  ivals.Add(9);
  CHECK_IVALS("[2,1][9,2][20,1]");
  ivals.Add(11);
  CHECK_IVALS("[2,1][9,3][20,1]");

  ivals.Add(3);
  CHECK_IVALS("[2,2][9,3][20,1]");
  ivals.Add(1);
  CHECK_IVALS("[1,3][9,3][20,1]");

  ivals.Add(19);
  CHECK_IVALS("[1,3][9,3][19,2]");
  ivals.Add(21);
  CHECK_IVALS("[1,3][9,3][19,3]");

  ivals.Add(10);
  CHECK_IVALS("[1,3][9,3][19,3]");
  ivals.Add(9);
  CHECK_IVALS("[1,3][9,3][19,3]");
  ivals.Add(11);
  CHECK_IVALS("[1,3][9,3][19,3]");

  ivals.Add(19);
  CHECK_IVALS("[1,3][9,3][19,3]");
  ivals.Add(21);
  CHECK_IVALS("[1,3][9,3][19,3]");
  ivals.Add(20);
  CHECK_IVALS("[1,3][9,3][19,3]");

  ivals.Add(1);
  CHECK_IVALS("[1,3][9,3][19,3]");
  ivals.Add(2);
  CHECK_IVALS("[1,3][9,3][19,3]");
  ivals.Add(3);
  CHECK_IVALS("[1,3][9,3][19,3]");

  ivals.Add(6);
  CHECK_IVALS("[1,11][19,3]");
  ivals.Add(11);
  CHECK_IVALS("[1,11][19,3]");
  ivals.Add(12);
  CHECK_IVALS("[1,12][19,3]");

  ivals.Add(17);
  CHECK_IVALS("[1,12][17,5]");
  ivals.Add(16);
  CHECK_IVALS("[1,21]");
}


int main(int argc, char **argv) {
  TestIntervals();

  printf("OK\n");
  return 0;
}
