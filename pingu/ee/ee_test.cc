
#include <stdio.h>
#include <unistd.h>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "pi/bcm2835.h"

using uint8 = uint8_t;
using uint32 = uint32_t;

[[maybe_unused]]
static bool WriteVec(const std::vector<uint8> &msg) {

  uint8_t code =
    bcm2835_i2c_write((const char *)&msg[0], msg.size());
  switch (code) {
  case BCM2835_I2C_REASON_OK:
    printf("I2C OK\n");
    return true;
  case BCM2835_I2C_REASON_ERROR_NACK:
    // printf("I2C NACK\n");
    return false;
  case BCM2835_I2C_REASON_ERROR_CLKT:
    printf("I2C Clock stretch\n");
    return false;
  case BCM2835_I2C_REASON_ERROR_DATA:
    printf("i2c not all data sent / timeout\n");
    return false;
  default:
    printf("i2c other error??\n");
    return false;
  }
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

  // for (int a = 0; a < 256; a++) {
  for (uint8_t a : {0b10100001}) {
  // printf("Try address 0x%02x\n", a);
    // Test..
    // constexpr uint8_t a = 0b10100000;
  printf("Try address 0x%02x\n", a);
  bcm2835_i2c_setSlaveAddress(a);


  //   7     6     5     4     3     2     1     0
  //  device type ident.   | chip sel  | addr8 | r/w
  //   1     0     1     0     0     0     A     R
  // device type always 1010, and chip select for this
  // chip is wired to 00.
  //  R=read
  //  A=high bit of address
  // const uint8 cmd = 0b10100001;

  for (int i = 0; i < 3; i++) {
    char buf[8] = {};
    // int code = bcm2835_i2c_read(buf, 1);
    char reg[1] = {0x01};
    int code = bcm2835_i2c_read_register_rs(reg, buf, 1);

    switch (code) {
    case BCM2835_I2C_REASON_OK:
      printf("I2C OK\n"
             "read! address 0x%02x got %02x", a, buf[0]);
      return;
    case BCM2835_I2C_REASON_ERROR_NACK:
      printf("I2C NACK\n");
      break;
    case BCM2835_I2C_REASON_ERROR_CLKT:
      printf("I2C Clock stretch\n");
      break;
    case BCM2835_I2C_REASON_ERROR_DATA:
      printf("i2c not all data sent / timeout\n");
      break;
    default:
      printf("i2c other error??\n");
      break;
    }

      usleep(1000);
  }
  }

  bcm2835_i2c_end();
}

int main(int argc, char **argv) {

  CHECK(bcm2835_init()) << "BCM Init failed!";

  Enumerate();
  return 0;
}
