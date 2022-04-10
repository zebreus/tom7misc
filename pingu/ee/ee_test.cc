
#include <stdio.h>
#include <unistd.h>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "pi/bcm2835.h"
#include "arcfour.h"
#include "timer.h"
#include "drive.h"

using uint8 = uint8_t;
using uint32 = uint32_t;
using namespace std;

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


[[maybe_unused]]
static void Dump() {
  std::vector<uint8> contents = CueDrive::ReadAll(true);
  PrintData("Data dump", contents);
  return;
}

// Write a page of 16 bytes. Since a write temporarily disables
// the device, this blocks until we are able to successfully
// read the line back. Returns true if the data we read back
// matches (which should be the case!).
//
// (Perhaps this should do its own i2cstart/end?)
[[maybe_unused]]
static bool WritePage(int addr, const std::vector<uint8_t> &page) {
  constexpr int PAGESIZE = 16;
  CHECK(page.size() == PAGESIZE);
  CHECK(addr % PAGESIZE == 0);
  if (addr >= 256) {
    bcm2835_i2c_setSlaveAddress(CueDrive::ADDR1);
  } else {
    bcm2835_i2c_setSlaveAddress(CueDrive::ADDR0);
  }


  std::vector<uint8> payload;
  payload.reserve(PAGESIZE + 1);
  payload.push_back((uint8)addr);
  for (uint8 b : page) payload.push_back(b);
  if (!CueDrive::WriteVec(payload, true))
    return false;

  /*
  if (!CueDrive::WriteVec({(uint8)addr}))
    return false;

  if (!CueDrive::WriteVec(page))
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
    CueDrive::WriteVec({(uint8)addr}, true);
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
          CueDrive::WriteVec({(uint8)addr}, true);
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

  CHECK(bcm2835_i2c_begin()) << "root? called bcm2835_init?";
  bcm2835_i2c_set_baudrate(CueDrive::BAUD_RATE);
  bcm2835_i2c_setSlaveAddress(CueDrive::ADDR0);
  
  for (int p = 0; p < NUM_PAGES; p++) {
    std::vector<uint8_t> payload;
    payload.reserve(PAGESIZE);
    for (int b = 0; b < PAGESIZE; b++) {
      payload.push_back(v[p * PAGESIZE + b]);
    }

    CHECK(payload.size() == PAGESIZE);
    CHECK(WritePage(p * PAGESIZE, payload)) << p;
  }
  
  bcm2835_i2c_end();
}

[[maybe_unused]]
static void WriteTest() {
  ArcFour rc(StringPrintf("ee%lld", time(nullptr)));

  #if 0
  std::vector<uint8> testpage;
  for (int i = 0; i < 16; i++)
    testpage.push_back((uint8)("[= here i am ~=]"[i]));
  CHECK(testpage.size() == 16);
  WritePage(512 + 16, testpage);
  return; // XXX
  #endif

  std::vector<uint8> original = CueDrive::ReadAll();
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

  std::vector<uint8> finally = CueDrive::ReadAll();
  PrintData("After", finally, true);

  return;
}


int main(int argc, char **argv) {
  CueDrive::Init();

  // (void)Dump();
  WriteTest();
  return 0;
}
