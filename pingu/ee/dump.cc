
#include <stdio.h>
#include <unistd.h>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "pi/bcm2835.h"
#include "timer.h"

using uint8 = uint8_t;
using uint32 = uint32_t;
using namespace std;

//   7     6     5     4     3     2     1     0
//  device type ident.   | chip sel  | addr8 | r/w
//   1     0     1     0     0     0     A     R
// device type always 1010, and chip select for this
// chip is wired to 00.
//  R=read
//  A=high bit of address
// The R/W bit is implemented by the i2c peripheral
// (using 7-bit addressing). So we have two:
[[maybe_unused]]
static constexpr uint8_t ADDR0 = 0b01010000;
[[maybe_unused]]
static constexpr uint8_t ADDR1 = 0b01010001;

// super slow for debugging!
// 100k or 400k supposedly work
static constexpr int BAUD_RATE = 100; // XXX

static string CodeString(int code) {
  switch (code) {
  case BCM2835_I2C_REASON_OK: return "OK";
  case BCM2835_I2C_REASON_ERROR_NACK: return "NACK";
  case BCM2835_I2C_REASON_ERROR_CLKT: return "Clock stretch";
  case BCM2835_I2C_REASON_ERROR_DATA:
    return "not all data sent / timeout";
  default: return "other??";
  }
}

[[maybe_unused]]
static bool WriteVec(const std::vector<uint8> &msg) {
  uint8_t code =
    bcm2835_i2c_write((const char *)msg.data(), msg.size());
  printf("i2c write vec len=%d %s\n",
         (int)msg.size(), CodeString(code).c_str());
  return code == BCM2835_I2C_REASON_OK;
}

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
      if (c >= 32 && c < 128) {
        printf("%c", c);
      } else {
        printf(".");
      }
    }

    printf("\n");
  }
}

static vector<uint8_t> ReadAll(int start_addr = 0) {
  constexpr int SIZE = 512;
  char buf[SIZE];
  // load address to read from by doing a dummy write
  WriteVec({(uint8)start_addr});
  // then read as many bytes as you want
  int code = bcm2835_i2c_read(buf, SIZE);
  printf("i2c read: %s\n", CodeString(code).c_str());
  CHECK(code == BCM2835_I2C_REASON_OK);

  // PERF directly...
  std::vector<uint8_t> res;
  res.reserve(SIZE);
  for (int i = 0; i < SIZE; i++) {
    res.push_back(buf[i]);
  }
  return res;
}


[[maybe_unused]]
static void Dump(int start_addr) {
  CHECK(bcm2835_i2c_begin()) << "root? called bcm2835_init?";

  bcm2835_i2c_set_baudrate(BAUD_RATE);

  bcm2835_i2c_setSlaveAddress(ADDR0);

  std::vector<uint8> contents = ReadAll(start_addr);

  PrintData2Col(contents);

  bcm2835_i2c_end();
  return;
}

int main(int argc, char **argv) {
  CHECK(bcm2835_init()) << "BCM Init failed!";
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
