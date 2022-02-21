
#include <stdio.h>
#include <unistd.h>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "pi/bcm2835.h"
#include "arcfour.h"
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

// 100k or 400k supposedly work
static constexpr int BAUD_RATE = 10000; // XXX

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

static void PrintData(const string &what,
                      const vector<uint8> &v,
                      bool ascii = false) {
  printf("%s:", what.c_str());
  for (int i = 0; i < v.size(); i++) {
    if (i % 16 == 0) printf("\n");
    char c = v[i];
    if (ascii) {
      if (c >= 32 && c < 127) {
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
  // Start from first "device". It doesn't mind reading
  // across the boundary, though.
  bcm2835_i2c_setSlaveAddress(ADDR0);
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

  bcm2835_i2c_set_baudrate(BAUD_RATE);

  bcm2835_i2c_setSlaveAddress(ADDR0);

  std::vector<uint8> contents = ReadAll();

  PrintData("Data dump", contents);

  bcm2835_i2c_end();
  return;
}

// Write a page of 16 bytes. Since a write temporarily disables
// the device, this blocks until we are able to successfully
// read the line back. Returns true if the data we read back
// matches (which should be the case!).
[[maybe_unused]]
static bool WritePage(int addr, const std::vector<uint8_t> &page) {
  constexpr int PAGESIZE = 16;
  CHECK(page.size() == PAGESIZE);
  CHECK(addr % PAGESIZE == 0);
  if (addr >= 256) {
    bcm2835_i2c_setSlaveAddress(ADDR1);
  } else {
    bcm2835_i2c_setSlaveAddress(ADDR0);
  }


  std::vector<uint8> payload;
  payload.reserve(PAGESIZE + 1);
  payload.push_back((uint8)addr);
  for (uint8 b : page) payload.push_back(b);
  if (!WriteVec(payload))
    return false;

  /*
  if (!WriteVec({(uint8)addr}))
    return false;

  if (!WriteVec(page))
    return false;
  */

  // Now the device is in a write condition and will not
  // respond on the bus. Wait until we're able to read the
  // same line back.
  // sleep(1);

  char buf[PAGESIZE];

  Timer write_timer;
  int tries = 0;
  do {
    // read back the same address
    WriteVec({(uint8)addr});
    const int code = bcm2835_i2c_read(buf, PAGESIZE);
    tries++;
    if (code == BCM2835_I2C_REASON_OK) {
      printf("Read ok after %d tries\n", tries);

      for (int i = 0; i < PAGESIZE; i++) {
        if (page[i] != buf[i]) {
          printf(" .. but byte %d/%d is wrong (wrote %02x got %02x)\n",
                 i, PAGESIZE, page[i], buf[i]);

          // allow one more chance (e.g. device came online during our
          // address write)
          WriteVec({(uint8)addr});
          const int code = bcm2835_i2c_read(buf, PAGESIZE);

          if (code != BCM2835_I2C_REASON_OK) {
            printf("Second try read not OK\n");
            return false;
          }

          for (int j = 0; j < PAGESIZE; j++) {
            if (page[j] != buf[j]) {
              printf(" .. second-try byte %d/%d is wrong "
                     "(wrote %02x got %02x)\n",
                     j, PAGESIZE, page[j], buf[j]);
              return false;
            }
          }
          printf("Second try correct!\n");
          return true;
        }
      }
      // Correct!
      return true;
    }
  } while (write_timer.Seconds() < 1.0);
  return false;
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
    payload.reserve(PAGESIZE);
    for (int b = 0; b < PAGESIZE; b++) {
      payload.push_back(v[p * PAGESIZE + b]);
    }

    CHECK(payload.size() == PAGESIZE);
    CHECK(WritePage(p * PAGESIZE, payload)) << p;
  }
}

[[maybe_unused]]
static void WriteTest() {
  ArcFour rc(StringPrintf("ee%lld", time(nullptr)));

  CHECK(bcm2835_i2c_begin()) << "root? called bcm2835_init?";

  bcm2835_i2c_set_baudrate(BAUD_RATE);

  bcm2835_i2c_setSlaveAddress(ADDR0);

  #if 0
  std::vector<uint8> testpage;
  for (int i = 0; i < 16; i++)
    testpage.push_back((uint8)("[= here i am ~=]"[i]));
  CHECK(testpage.size() == 16);
  WritePage(512 + 16, testpage);
  return; // XXX
  #endif

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

  for (int i = 0; i < 16; i++) updated[16 + i] = i;

  x = 280;
  updated[x++] = 'w';
  updated[x++] = 'o';
  updated[x++] = 'r';
  updated[x++] = 'l';
  updated[x++] = 'd';
  for (int i = 0; i < 16; i++) updated[511 - i] = 'A' + i;

  x = 410;
  updated[x++] = 0;
  updated[x++] = 255;
  updated[x++] = 0;
  updated[x++] = 255;
  updated[x++] = 128;
  updated[x++] = 127;
  
  WriteAll(updated);

  std::vector<uint8> finally = ReadAll();
  PrintData("After", finally, true);

  bcm2835_i2c_end();
  return;
}


int main(int argc, char **argv) {

  CHECK(bcm2835_init()) << "BCM Init failed!";

  // (void)Dump();
  WriteTest();
  return 0;
}
