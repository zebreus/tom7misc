
#include "talk.h"

#include <cstdio>
#include <string>

#include "ansi.h"
#include "util.h"
#include "base/logging.h"

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 3) << "./maketalk.exe dir file.talk\n";

  Talk talk = Talk::Load(Util::DirPlus(argv[1], argv[2]));
  talk.SaveJS(
      Util::BinaryDir(argv[0]),
      argv[1]);

  printf("OK\n");
}
