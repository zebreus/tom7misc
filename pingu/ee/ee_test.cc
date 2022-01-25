
#include <stdio.h>
#include <unistd.h>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "pi/bcm2835.h"

using uint8 = uint8_t;
using uint32 = uint32_t;

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
    bcm2835_i2c_write((const char *)&msg[0], msg.size());
  printf("i2c %s\n", CodeString(code).c_str());
  return code == BCM2835_I2C_REASON_OK;
}

[[maybe_unused]]
static void Enumerate() {

  CHECK(bcm2835_i2c_begin()) << "root? called bcm2835_init?";

  // Can tune this for speed, but for this sensor there's likely
  // very little we gain from a fast connection.
  // Conservative:
  // bcm2835_i2c_setClockDivider(BCM2835_I2C_CLOCK_DIVIDER_2500);
  // bcm2835_i2c_setClockDivider(
  // BCM2835_I2C_CLOCK_DIVIDER_2500);

  // supposedly this supports 100khz and 400khz.
  bcm2835_i2c_set_baudrate(100000);

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
  constexpr uint8_t addr0 = 0b01010000;
  [[maybe_unused]]
  constexpr uint8_t addr1 = 0b01010001;

  bcm2835_i2c_setSlaveAddress(addr0);

  constexpr int SIZE = 512;
  char buf[SIZE];
  [[maybe_unused]] char reg = 0x04;
  // load address to read from by doing a dummy write
  WriteVec({0});
  // then read 16 bytes (or whatever)
  int code = bcm2835_i2c_read(buf, SIZE);
  printf("i2c read: %s\n", CodeString(code).c_str());

  printf("Result:");
  for (int i = 0; i < SIZE; i++) {
    if (i % 16 == 0) printf("\n");
    char c = buf[i];
    printf(" %02x", c);
    #if 0
      if (c >= 32 && c < 128)
        printf(" %c", c);
      else
        printf(" _");
    #endif
  }
  printf("\n");

  bcm2835_i2c_end();
  return;
}

int main(int argc, char **argv) {

  CHECK(bcm2835_init()) << "BCM Init failed!";

  Enumerate();
  return 0;
}
