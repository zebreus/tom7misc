// Loads a font from one config, and writes it back out using another.

#include <cstdint>
#include <string>

#include "base/logging.h"
#include "font-image.h"
#include "util.h"

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
    "Usage: ./reencode.exe config-in.cfg config-out.cfg";

  printf("Normalize %s to %s\n", argv[1], argv[2]);
  const Config config_in = ParseAndCheckConfig(argv[1]);
  const Config config_out = ParseAndCheckConfig(argv[2]);

  FontImage font_in(config_in);

  FontImage font_out = font_in;
  font_out.config = config_out;

  if (Util::ExistsFile(config_out.pngfile)) {
    std::string back = Util::BackupFile(config_out.pngfile);
    printf("Moved old %s to %s\n", config_out.pngfile.c_str(),
           back.c_str());
  }
  font_out.SaveImage(config_out.pngfile);

  return 0;
}
