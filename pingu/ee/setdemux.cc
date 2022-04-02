
#include <stdio.h>
#include <unistd.h>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "pi/bcm2835.h"

#include "drive.h"

using uint8 = uint8_t;
using uint32 = uint32_t;
using namespace std;

int main(int argc, char **argv) {

  CHECK(bcm2835_init()) << "BCM Init failed!";

  CHECK(argc == 3) << "Usage: ./setdemux.exe group value\n"
    "Run as root. group must be a or b. value must be in [0, 63]. \n";

  char group = argv[1][0] | 32;
  CHECK(group == 'a' || group == 'b') << group;
  bool use_group_a = group == 'a';
  int value = atoi(argv[2]);
  printf("Setting demux to %d\n", value);
	 
  CueDrive::SetDemux(use_group_a, value);
  printf("Set to group=%c, addr %d\n",
	 use_group_a ? 'A' : 'B',
	 value);
  
  // blinkenlights demo
  #if 0
  uint8 v = 0;
  for (;;) {
    CueDrive::SetDemux(use_group_a, v & 0b11);
    usleep(50000);
    v++;
  }  
  #endif
  
  return 0;
}
