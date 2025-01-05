
#include "assemble.h"

#include "ansi.h"
#include "base/logging.h"

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 3) << "./asm7.exe source.nes out.rom\n";

  Assembly assembly = Assembly::Assemble(argv[1]);
  assembly.WriteToDisk(argv[2]);

  return 0;
}
