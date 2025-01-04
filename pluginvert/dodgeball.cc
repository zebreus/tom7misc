
#include "network.h"

#include <string>
#include <vector>
#include <cstdio>
#include <memory>
#include <optional>

#include "network-test-util.h"
#include "util.h"
#include "image.h"
#include "lines.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"

using namespace std;

int main(int argc, char **argv) {

  ArcFour rc(StringPrintf("%lld", time(nullptr)));
  RandomGaussian gauss(&rc);
  
  auto tn = NetworkTestUtil::DodgeballAdam(10, 2, 2);

  int count = 0, first = 0;
  for (int i = 0; i < 2000; i++) {
    vector<float> input;
    for (int i = 0; i < 10; i++)
      input.push_back(gauss.Next());
    vector<float> out = tn.f(input);
    CHECK(out.size() == 2);
    if (i < 40) {
      printf("%.2f %.2f\n", out[0], out[1]);
    }
    if (out[0] > 0.5f) count++;
    if (out[0] > 0.5f && out[1] == 0.0f) first++;
  }

  printf("%d/2000 = %.2f%% collided, %d = %.2f%% on first frame",
         count, (count * 100.0) / 2000.0,
         first, (first * 100.0) / 2000.0);
  
  return 0;
}
