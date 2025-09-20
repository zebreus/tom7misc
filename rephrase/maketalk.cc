
#include "talk.h"

#include <cstdlib>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "util.h"

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 3) << "./maketalk.exe dir file.talk\n";

  Talk talk = Talk::Load(Util::DirPlus(argv[1], argv[2]));
  CHECK(talk.screen_width > 0 &&
        talk.screen_width > 0) << "Talk now requires a screen "
    "width and height; maybe you need to rebuild it?";

  talk.SaveJS(
      Util::BinaryDir(argv[0]),
      argv[1]);

  Print("OK\n");
}
