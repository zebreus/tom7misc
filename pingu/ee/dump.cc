
#include <stdio.h>
#include <unistd.h>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "pi/bcm2835.h"
#include "timer.h"
#include "drive.h"

using uint8 = uint8_t;
using uint32 = uint32_t;
using namespace std;

static void PrintData2Col(const vector<uint8> &v) {
  CHECK(v.size() == 512);
  for (int p = 0; p < 512 / 16; p++) {
    // Print line, first hex, then ascii
    for (int i = 0; i < 16; i++) {
      char c = v[p * 16 + i];
      printf("%02x ", c);
    }

    printf("| ");

    for (int i = 0; i < 16; i++) {
      char c = v[p * 16 + i];
      if (c >= 32 && c < 127) {
        printf("%c", c);
      } else {
        printf(".");
      }
    }

    printf("\n");
  }
}

[[maybe_unused]]
static void Dump(int start_addr) {
  CHECK(start_addr == 0) << "Sorry, disabled start_addr";
  std::vector<uint8_t> contents = CueDrive::ReadAll(true);
  PrintData2Col(contents);
  return;
}

int main(int argc, char **argv) {
  CueDrive::Init();
  if (argc > 1) {
    int start_addr = atoi(argv[1]);
    CHECK(start_addr >= 0 && start_addr < 256) << start_addr;
    printf("Starting from %d...\n", start_addr);
    Dump(start_addr);
  } else {
    Dump(0);
  }
  return 0;
}
