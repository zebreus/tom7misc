
#include <string>
#include <memory>
#include <vector>

#include "arcfour.h"
#include "randutil.h"

#include "bignum/big.h"
#include "bignum/big-overloads.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "subprocess.h"
#include "ansi.h"
#include "timer.h"
#include "periodically.h"

#define MAGNITUDE 16384

using namespace std;

static std::vector<std::string> RunCmd(const std::string &cmd) {
  std::unique_ptr<Subprocess> proc(Subprocess::Create(cmd));
  CHECK(proc.get() != nullptr) << cmd;
  std::vector<std::string> ret;
  for (;;) {
    std::string line;
    if (!proc->ReadLine(&line)) return ret;
    if (line.size() > 75) line.resize(75);
    printf(AGREY("%s") "\n", line.c_str());
    ret.push_back(line);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  string seed = StringPrintf("findcoverage.%lld", time(nullptr));
  ArcFour rc(seed);

  CHECK(argc == 2) << "./findcoverage.exe target_string";

  std::string target = argv[1];

  printf("Seed: " ABLUE("%s")
         ". Looking for [" APURPLE("%s" )"]\n",
         seed.c_str(), target.c_str());

  auto RandInt = [&rc]() {
      int64_t r = (int64_t)RandTo(&rc, MAGNITUDE) - (MAGNITUDE / 2);
      return r;
    };

  Periodically stats_per(5.0);

  double total_sec = 0.0;
  for (int tries = 1; true; tries++) {

    // ax^2 + bxy + cy^2 + dx + ey = -f
    BigInt a((rc.Byte() < 200) ? RandInt() : 0);
    BigInt b((rc.Byte() < 128) ? RandInt() : 0);
    BigInt c((rc.Byte() < 200) ? RandInt() : 0);
    BigInt d((rc.Byte() < 128) ? RandInt() : 0);
    BigInt e((rc.Byte() < 128) ? RandInt() : 0);

    BigInt f;

    if (rc.Byte() < 200) {
      // the solution
      BigInt x(RandInt());
      BigInt y(RandInt());

      // now compute f
      BigInt negf = a * x * x + b * x * y + c * y * y + d * x + e * y;

      f = -negf;

      printf(AFGCOLOR(70, 55, 70, "x = %s  y = %s") "\n",
             x.ToString().c_str(),
             y.ToString().c_str());
    } else {
      // zero is actually boring here because then x=0, y=0 is always a solution
      f = BigInt((rc.Byte() < 200) ? RandInt() : (rc.Byte() & 1) ? 1 : -1);
      printf(AFGCOLOR(70, 55, 70, "(no solution known)") "\n");
    }

    string cmd = StringPrintf("quad.exe %s %s %s %s %s %s",
                              a.ToString().c_str(),
                              b.ToString().c_str(),
                              c.ToString().c_str(),
                              d.ToString().c_str(),
                              e.ToString().c_str(),
                              f.ToString().c_str());

    printf(AWHITE("%s") "\n", cmd.c_str());

    Timer run_timer;
    std::vector<std::string> lines = RunCmd(cmd);
    double sec = run_timer.Seconds();
    total_sec += sec;

    for (const std::string &line : lines) {
      if (line.find(target) != string::npos) {
        printf(AGREEN("Found") ": %s\n", line.c_str());
        printf("Ran %d in %.3f sec\n", tries, total_sec);
        return 0;
      }
    }

    if (stats_per.ShouldRun()) {
      // Maybe some kind of histogram would be better here, since
      // there can be a very wide range in performance depending
      // on the arguments.
      double spt = total_sec / tries;
      printf(ABLUE("%d") " tries in %s (%s/ea)\n",
             tries, ANSI::Time(total_sec).c_str(),
             ANSI::Time(spt).c_str());
    }
  }

  return 0;
}
