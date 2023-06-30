
#include "diff.h"
#include "expression.h"
#include "ansi.h"
#include "choppy.h"

#include "base/logging.h"


using Choppy = ChoppyGrid<256>;
using DB = Choppy::DB;

int main(int argc, char **argv) {
  AnsiInit();

  CHECK(argc == 3) << "./colordiff.exe db1.txt db2.txt\n";

  DB db1, db2;
  db1.LoadFile(argv[1]);
  db2.LoadFile(argv[2]);

  for (const auto &[k, v] : db1.fns) {
    auto it2 = db2.fns.find(k);
    if (it2 == db2.fns.end()) {
      printf(ARED("// %s") "\n",
             DB::KeyString(k).c_str());
      printf(ARED("%s") "\n",
             Exp::Serialize(v).c_str());
    } else {
      if (!Exp::Eq(v, it2->second)) {
        printf("// %s\n", DB::KeyString(k).c_str());

        auto [before, after] = ColorDiff(v, it2->second);
        // printf(AWHITE("Before") ": %s\n", Exp::Serialize(v).c_str());
        printf(AWHITE("Before") ": %s\n", before.c_str());
        // printf(AWHITE("After") ": %s\n",
        //        Exp::Serialize(it2->second).c_str());
        printf(AWHITE("After") ": %s\n", after.c_str());
      }
    }
  }

  for (const auto &[k, v] : db2.fns) {
    auto it1 = db1.fns.find(k);
    if (it1 == db1.fns.end()) {
      printf(AGREEN("// %s") "\n",
             DB::KeyString(k).c_str());
      printf(AGREEN("%s") "\n",
             Exp::Serialize(v).c_str());
    }
  }

  return 0;
}
