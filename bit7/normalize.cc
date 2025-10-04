// Generate a clean font image (that can be used as an input) from a
// font image/config. Might do this if the image has something wrong
// with it (stray pixels, etc.).

#include <string>

#include "base/logging.h"
#include "base/print.h"
#include "font-image.h"
#include "util.h"

using namespace std;

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
  CHECK(argc == 2 || argc == 3) <<
    "Usage: ./normalize.exe config.cfg [normalized.png]";

  const Config config = ParseAndCheckConfig(argv[1]);
  std::string dest = config.pngfile;
  if (argc == 3) dest = argv[2];

  FontImage font(config);

  if (Util::ExistsFile(dest)) {
    std::string back = Util::BackupFile(dest);
    Print("Moved old {} to {}\n", dest, back);
  }

  font.SaveImage(dest);

  return 0;
}
