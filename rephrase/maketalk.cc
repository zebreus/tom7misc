
#include "talk.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "ansi.h"
#include "util.h"
#include "base/logging.h"

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 3 || argc == 5) <<
    "./maketalk.exe dir file.talk [1920 1080]\n";

  // XXX: Configurable from bovex file
  const int screen_width = argc == 5 ? atoi(argv[3]) : 1920;
  const int screen_height = argc == 5 ? atoi(argv[4]) : 1080;

  Talk talk = Talk::Load(Util::DirPlus(argv[1], argv[2]));
  talk.screen_width = screen_width;
  talk.screen_height = screen_height;
  talk.SaveJS(
      Util::BinaryDir(argv[0]),
      argv[1]);

  printf("OK\n");
}
