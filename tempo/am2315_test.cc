#include "am2315.h"

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
    printf("I2C NACK\n");
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
  bcm2835_i2c_setClockDivider(
      BCM2835_I2C_CLOCK_DIVIDER_2500);

  for (int a = 0; a < 256; a++) {
    printf("Try address 0x%02x\n", a);
    // Test..
    bcm2835_i2c_setSlaveAddress(a);

    for (int i = 0; i < 20; i++) {
      printf("Pre wake-up:\n");
      if (WriteVec({0x00})) {
        printf("acknowledged! address 0x%02x\n", a);
        return;
      }
      usleep(1000);
    }
  }

  bcm2835_i2c_end();
}

int main(int argc, char **argv) {

  CHECK(bcm2835_init()) << "BCM Init failed!";

  // Enumerate();
  // return 0;

  printf("Initialize:\n");
  AM2315::Initialize();
  int64 success = 0, failure = 0;

  printf("Read info:\n");
  AM2315::Info info;
  const char *info_err = "not set";

  if (AM2315::ReadInfo(&info, &info_err)) {
    printf("AM2315 Info:\n"
           "  Model: %04x\n"
           "  Version: %02x\n"
           "  ID: %08x\n",
           (uint32)info.model, (uint32)info.version, info.id);
  } else {
    printf("ReadInfo failed: %s\n", info_err);
  }

  for (;;) {
    string sf = StringPrintf("[%lld/%lld = %.1f%%] ",
                             success,
                             success + failure,
                             (success * 100.0) / (success + failure));

    [[maybe_unused]] float temp = -999.0f;
    const char *err = "not set";
    if (AM2315::ReadTemp(&temp, &err)) {
      success++;
      printf("%sRead temp: %.2f deg C\n", sf.c_str(), temp);
    } else {
      failure++;
      printf("%sFailed: %s\n", sf.c_str(), err);
    }

    sleep(4);

    [[maybe_unused]] float rh = -666.0f;
    if (AM2315::ReadRH(&rh, &err)) {
      success++;
      printf("%sRead Hum: %.2f%% RH\n", sf.c_str(), rh);
    } else {
      failure++;
      printf("%sFailed: %s\n", sf.c_str(), err);
    }

    sleep(4);
  }

  return 0;
}
