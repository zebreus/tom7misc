// Copy characters from a 1x font to 2x font, if they aren't already
// present there.

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <map>
#include <unordered_map>

#include "util.h"
#include "image.h"
#include "bit7chars.h"
#include "bitmap-font.h"
#include "base/stringprintf.h"
#include "base/logging.h"
#include "font-image.h"

using namespace std;
using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

using Glyph = FontImage::Glyph;

static Config ParseAndCheckConfig(const std::string &cfgfile) {
  Config config = Config::ParseConfig(cfgfile);
  CHECK(!config.pngfile.empty()) << "Required config line: pngfile";
  CHECK(!config.name.empty()) << "Required config line: name";

  CHECK(config.charbox_width > 0) << "Config line charbox-width must be >0";
  CHECK(config.charbox_height > 0) << "Config line charbox-height must be >0";

  CHECK(config.descent >= 0) << "Config line charbox-height must be >= 0";

  CHECK(config.chars_across > 0);
  CHECK(config.chars_down > 0);

  return config;
}

int main(int argc, char **argv) {
  CHECK(argc == 3) <<
    "Usage: ./makesfd.exe config.cfg normalized.png";

  printf("Normalize %s to %s\n", argv[1], argv[2]);
  const Config config = ParseAndCheckConfig(argv[1]);

  FontImage font(config);

  font.SaveImage(argv[2], config.chars_across, config.chars_down);

  return 0;
}
