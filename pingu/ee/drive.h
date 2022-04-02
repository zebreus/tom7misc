
#include <cstdint>
#include <vector>
#include <string>

struct CueDrive {
  // Six bits at 22, 23, 24, 25, 26, 27

  static constexpr int DEMUX_GPIO = 22;

  static constexpr int GROUP_SEL_A = 17;
  static constexpr int GROUP_SEL_B = 16;

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

  
  // super slow for debugging!
  // 100k or 400k supposedly work
  static constexpr int BAUD_RATE = 1000; // XXX
  
  // Set group and address (picks cue cartridge within a group).
  // address in [0, 64) (six bits).
  static void SetDemux(bool use_group_a, uint8_t address);

  // Read 512 bytes from the current cartridge.
  static std::vector<uint8_t> ReadAll(bool verbose = false);

  // Does pi-side initialization (as root).
  static void Init();

  static bool WriteVec(const std::vector<uint8_t> &msg,
		       bool verbose = false);
  
  static std::string CodeString(int code);
};
