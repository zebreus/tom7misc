
#ifndef _TEMPO_AM2315_H
#define _TEMPO_AM2315_H

#include <stdio.h>
#include <stdlib.h>
#include <cstdint>

// AM2315 and AM2320 temperature/humidity sensors:
//  - AM2315 is the nice wired unit with outdoor shielding.
//  - AM2320 is the little board-mount module.
// (maybe we should rename this to AM23XX)

struct AM2315 {
  // Supposedly the AM2320 uses 0xB8 as its address, but I found
  // it on 0x5C as well.
  static constexpr uint8_t ADDRESS = 0x5C;

  // There can only be one of these, since they all have the same
  // address and we only support i2c bus 1.
  static void Initialize();

  // Device info. This seems to read as all zeroes on the 2320.
  struct Info {
    uint16_t model = 0;
    uint8_t version = 0;
    // Endianness is not clear from docs. This is register
    // bytes 0x0B,0x0C,0x0D,0x0E treated as a big-endian 32-bit word.
    uint32_t id = 0;
  };

  // TODO: Version of these that returns the integer data.
  // TODO: Version that reads Temperature and Humidity in the same call.
  // XXX?
  // In degrees C.
  static bool ReadTemp(float *temp, const char **err = nullptr);
  // In % RH.
  static bool ReadRH(float *rh, const char **err = nullptr);

  static bool ReadInfo(Info *info, const char **err = nullptr);
};

#endif
