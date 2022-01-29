
#include <stdio.h>
#include <unistd.h>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "pi/bcm2835.h"
#include "arcfour.h"

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
  printf("i2c %s\n", CodeString(code).c_str());
  return code == BCM2835_I2C_REASON_OK;
}

static void PrintData(const string &what,
                      const vector<uint8> &v,
                      bool ascii = false) {
  printf("%s:", what.c_str());
  for (int i = 0; i < v.size(); i++) {
    if (i % 16 == 0) printf("\n");
    char c = v[i];
    if (ascii) {
      if (c >= 32 && c < 128) {
        printf(" %c", c);
      } else {
        printf(" _");
      }
    } else {
      printf(" %02x", c);
    }
  }
  printf("\n");
}


static vector<uint8_t> ReadAll() {
  constexpr int SIZE = 512;
  char buf[SIZE];
  // load address to read from by doing a dummy write
  WriteVec({0});
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
static void Dump() {
  CHECK(bcm2835_i2c_begin()) << "root? called bcm2835_init?";

  // Can tune this for speed, but for this sensor there's likely
  // very little we gain from a fast connection.
  // Conservative:
  // bcm2835_i2c_setClockDivider(BCM2835_I2C_CLOCK_DIVIDER_2500);
  // bcm2835_i2c_setClockDivider(
  // BCM2835_I2C_CLOCK_DIVIDER_2500);

  // supposedly this supports 100khz and 400khz.
  bcm2835_i2c_set_baudrate(100000);

  bcm2835_i2c_setSlaveAddress(ADDR0);

  std::vector<uint8> contents = ReadAll();

  PrintData("Data dump", contents);

  bcm2835_i2c_end();
  return;
}

[[maybe_unused]]
static void WriteAll(const std::vector<uint8_t> &v) {
  constexpr int SIZE = 512;
  constexpr int PAGESIZE = 16;
  static_assert(SIZE % PAGESIZE == 0);
  constexpr int NUM_PAGES = SIZE / PAGESIZE;
  CHECK(v.size() == SIZE);

  for (int p = 0; p < NUM_PAGES; p++) {
    std::vector<uint8_t> payload;
    payload.reserve(PAGESIZE + 1);
    // starting address of page
    int addr = p * PAGESIZE;
    payload.push_back(addr);

    for (int b = 0; b < PAGESIZE; b++) {
      payload.push_back(v[addr + b]);
    }
    WriteVec(payload); // << " page " << p;
    // printf("i2c write: %s\n", CodeString(code).c_str());
  }
}

[[maybe_unused]]
static void WriteTest() {
  ArcFour rc(StringPrintf("ee%lld", time(nullptr)));

  CHECK(bcm2835_i2c_begin()) << "root? called bcm2835_init?";

  bcm2835_i2c_set_baudrate(100000);

  bcm2835_i2c_setSlaveAddress(ADDR0);

  std::vector<uint8> original = ReadAll();
  PrintData("Before", original);

  std::vector<uint8> updated;
  updated.reserve(512);
  for (int i = 0; i < 512; i++) updated.push_back(0x2A);
  int x = 5;
  updated[x++] = 'h';
  updated[x++] = 'e';
  updated[x++] = 'l';
  updated[x++] = 'l';
  updated[x++] = 'o';

  x = 280;
  updated[x++] = 'w';
  updated[x++] = 'o';
  updated[x++] = 'r';
  updated[x++] = 'l';
  updated[x++] = 'd';
  WriteAll(updated);

  std::vector<uint8> finally = ReadAll();
  PrintData("After", finally, true);

  bcm2835_i2c_end();
  return;
}


int main(int argc, char **argv) {

  CHECK(bcm2835_init()) << "BCM Init failed!";

  (void)Dump();
  // WriteTest();
  return 0;
}
