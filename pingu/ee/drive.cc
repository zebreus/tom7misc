
#include "drive.h"

#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

#include "base/logging.h"
#include "pi/bcm2835.h"

using namespace std;
using uint8 = uint8_t;
using uint32 = uint32_t;

void CueDrive::SetDemux(bool use_group_a, uint8 value) {
  CHECK_GE(value, 0) << value;
  CHECK_LT(value, 64) << value;

  {
    // This is hard-coded, but we need to set the group sel bits to
    // 10 or 01.
    bcm2835_gpio_fsel(GROUP_SEL_A, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_set_pud(GROUP_SEL_A, BCM2835_GPIO_PUD_OFF);
    bcm2835_gpio_fsel(GROUP_SEL_B, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_set_pud(GROUP_SEL_B, BCM2835_GPIO_PUD_OFF);

    constexpr uint32 GROUP_MASK =
      (1 << GROUP_SEL_A) | (1 << GROUP_SEL_B);
    if (use_group_a) {
      bcm2835_gpio_write_mask((1 << GROUP_SEL_A), GROUP_MASK);
    } else {
      bcm2835_gpio_write_mask((1 << GROUP_SEL_B), GROUP_MASK);
    }
  }
  
  for (int i = 0; i < 6; i++) {
    bcm2835_gpio_fsel(DEMUX_GPIO + i, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_set_pud(DEMUX_GPIO + i, BCM2835_GPIO_PUD_OFF);
  }
    
  bcm2835_gpio_write_mask((value << DEMUX_GPIO), (0b111111 << DEMUX_GPIO));
  printf("Set demux to %d.\n", value);

  return;
}

std::vector<uint8_t> CueDrive::ReadAll(bool verbose) {
  CHECK(bcm2835_i2c_begin()) << "root? called bcm2835_init?";

  bcm2835_i2c_set_baudrate(BAUD_RATE);

  bcm2835_i2c_setSlaveAddress(ADDR0);
  
  constexpr int SIZE = 512;
  char buf[SIZE];
  // Start from first "device". It doesn't mind reading
  // across the boundary, though.
  bcm2835_i2c_setSlaveAddress(ADDR0);
  // load address to read from by doing a dummy write
  WriteVec({0});
  // then read as many bytes as you want
  int code = bcm2835_i2c_read(buf, SIZE);
  if (verbose) printf("i2c read: %s\n", CodeString(code).c_str());
  CHECK(code == BCM2835_I2C_REASON_OK);
  bcm2835_i2c_end();
  
  // PERF directly...
  std::vector<uint8_t> res;
  res.reserve(SIZE);
  for (int i = 0; i < SIZE; i++) {
    res.push_back(buf[i]);
  }

  return res;
}

string CueDrive::CodeString(int code) {
  switch (code) {
  case BCM2835_I2C_REASON_OK: return "OK";
  case BCM2835_I2C_REASON_ERROR_NACK: return "NACK";
  case BCM2835_I2C_REASON_ERROR_CLKT: return "Clock stretch";
  case BCM2835_I2C_REASON_ERROR_DATA:
    return "not all data sent / timeout";
  default: return "other??";
  }
}

bool CueDrive::WriteVec(const std::vector<uint8> &msg,
                        bool verbose) {
  uint8_t code =
    bcm2835_i2c_write((const char *)msg.data(), msg.size());
  if (verbose)
    printf("i2c write vec len=%d %s\n",
	   (int)msg.size(), CodeString(code).c_str());
  return code == BCM2835_I2C_REASON_OK;
}


void CueDrive::Init() {
  CHECK(bcm2835_init()) << "BCM Init failed! root?";
  
}
