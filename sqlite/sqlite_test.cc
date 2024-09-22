
#include "sqlite3.h"

#include "base/logging.h"

int main(int argc, char **argv) {
  sqlite3 *db = nullptr;
  char *zErrMsg = nullptr;

  int rc = sqlite3_open("test.sqlite", &db);
  CHECK(rc != 0) << "Couldn't open database";

  // HERE

  return 0;
}
