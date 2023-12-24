
#include <vector>
#include <string>
#include <memory>
#include <cstdio>

#include "../fceulib/emulator.h"
#include "base/logging.h"
#include "ansi.h"
#include "csv.h"
#include "util.h"

using namespace std;

void BootReads() {
  // This function expects you to hack x6502.cc to add output
  // for every RdMem.
  const std::string cart_file = "cart/cart.nes";

  std::unique_ptr<Emulator> emu(Emulator::Create(cart_file));


  printf("---loaded---\n");
  for (int i = 0; i < 10; i++) {
    printf("== frame %d ==\n", i);
    emu->Step16(0);
  }

}

void BootCSV() {
  std::vector<std::vector<std::string>> csv =
    CSV::ParseFile("cartboot_data.csv");
  CHECK(!csv.empty());

  for (const auto &row : csv) {
    CHECK(row.size() == 3);
    double t = Util::ParseDoubleOpt(row[0]).value_or(-1.0);

    string byte = row[2];
    CHECK(Util::TryStripPrefix("0x", &byte)) << byte;
    uint8_t x = std::stoul(byte, nullptr, 16);

    printf("%.5f: %02x = ", t, x);
    for (int i = 0; i < 8; i++) {
      printf("%c", ((1 << (7 - i)) & x) ? '1' : '0');
    }
    printf("\n");
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  // BootReads();
  BootCSV();

  return 0;
}


