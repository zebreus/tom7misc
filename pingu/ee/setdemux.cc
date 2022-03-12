
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

// XXX make configurable
static constexpr bool USE_GROUP_A = true;


int main(int argc, char **argv) {

  CHECK(bcm2835_init()) << "BCM Init failed!";

  CHECK(argc == 2) << "Usage: ./setdemux.exe value\n"
    "Run as root. value must be in [0, 7].\n";

  int value = atoi(argv[1]);
  printf("Setting demux to %d\n", value);
	 
  CueDrive::SetDemux(USE_GROUP_A, value);
  printf("Set to group=%c, addr %d\n",
	 USE_GROUP_A ? 'A' : 'B',
	 value);
  
  // blinkenlights demo
  #if 0
  uint8 v = 0;
  for (;;) {
    CueDrive::SetDemux(USE_GROUP_A, v & 0b11);
    usleep(50000);
    v++;
  }  
  #endif
  
  return 0;
}
