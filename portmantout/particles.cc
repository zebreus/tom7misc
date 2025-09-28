
#include <cstdio>
#include <vector>
#include <string>

#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "util.h"

#include "makeparticles.h"

using namespace std;

int main() {
  vector<string> dict = Util::ReadFileToLines("wordlist.asc");
  Print("{} words.\n", dict.size());
  ArcFour rc("portmantout");

  vector<string> particles = MakeParticles(&rc, dict, true, nullptr);

  {
    FILE *f = fopen("particles.txt", "wb");
    CHECK(f != nullptr);
    for (const string &p : particles) {
      Print(f, "{}\n", p);
    }
    fclose(f);
    Print("Wrote particles.txt.\n");
  }

  return 0;
}
