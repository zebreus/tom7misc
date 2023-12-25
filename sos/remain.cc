
// Prints stats about mod.exe's progress.

#include <cstdint>
#include <array>
#include <memory>
#include <optional>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "image.h"
#include "threadutil.h"
#include "ansi.h"
#include "timer.h"
#include "periodically.h"
#include "factorization.h"
#include "numbers.h"
#include "util.h"

#include "mod-util.h"
#include "auto-histo.h"

static void VerifySmall(const Work &work) {
  for (int m = Work::MINIMUM; m <= Work::MAXIMUM; m++) {
    for (int n = Work::MINIMUM; n <= Work::MAXIMUM; n++) {
      if (Work::Eligible(m, n)) {
        uint64_t no_sol_at = work.GetNoSolAt(m, n);
        if (no_sol_at > 0) {
          if (no_sol_at < 100) {
            auto so = SimpleSolve(m, n, no_sol_at);
            if (so.has_value()) {
              const auto &[a, b, c] = so.value();
              printf("(" ABLUE("%d") "," APURPLE("%d") ") was eliminated "
                     "by " AYELLOW("%llu") ",\n"
                     "but it has solution " ARED("%lld, %lld, %lld") "!\n",
                     m, n, no_sol_at,
                     a, b, c);
              CHECK(false);
            }
          }
        }
      }
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(Work::Exists());

  Work work;
  work.Load();

  VerifySmall(work);

  for (int m = Work::MINIMUM; m <= Work::MAXIMUM; m++) {
    for (int n = Work::MINIMUM; n <= Work::MAXIMUM; n++) {
      if (Work::Eligible(m, n)) {
        uint64_t no_sol_at = work.GetNoSolAt(m, n);
        if (no_sol_at > 0) {
          if (no_sol_at > 1000) {
            printf("(" ABLUE("%d") "," APURPLE("%d") ") eliminated by "
                   AYELLOW("%llu") "\n", m, n, no_sol_at);
          }
        } else {
          uint64_t p = work.PrimeAt(m, n);
          printf("(" ABLUE("%d") "," APURPLE("%d") ") at prime "
                 AWHITE("%llu") "\n", m, n, p);
        }
      }
    }
  }

  return 0;
}


