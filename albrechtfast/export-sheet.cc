

#include <string>
#include <vector>

#include "ansi.h"
#include "db.h"
#include "util.h"

static void Export(std::string_view filename) {
  DB db;

  db.Spreadsheet(filename);
}

int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 2) << "./export-sheet.exe filename-out.tsv";

  Export(argv[1]);

  return 0;
}
