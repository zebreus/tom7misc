// Generate a clean font image (that can be used as an input) from a
// font image/config. Might do this if the image has something wrong
// with it (stray pixels, etc.).

#include <cstdint>
#include <string>

#include "base/logging.h"
#include "font-image.h"

using namespace std;
using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

using Glyph = FontImage::Glyph;

// TODO: Allow specifying a second config, which we use to write.
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

  font.SaveImage(argv[2]);

  return 0;
}
