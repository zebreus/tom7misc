#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <initializer_list>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "ico.h"
#include "image.h"
#include "util.h"

// A few more secret options available here:
// https://learn.microsoft.com/en-us/windows/win32/menurc/about-cursors

[[maybe_unused]]
static std::initializer_list<std::string_view> CURSOR_TYPES = {
  "arrow",
  "ibeam",
  "wait",
  "cross",
  "uparrow",
  "sizenwse",
  "sizenesw",
  "sizewe",
  "sizens",
  "sizeall",
  "no",
  "hand",
  "appstarting",
  "help",
  "pin",
  "person",
};

struct CursorConfig {
  std::vector<ICO::Cursor> sizes;
};

struct Config {
  std::map<std::string, CursorConfig> cursors;
};

static Config LoadConfig(std::string_view filename) {
  Config config;
  std::string current_cursor;

  for (std::string_view line : Util::ReadFileToLines(filename)) {
    Util::RemoveOuterWhitespace(&line);
    if (line.empty()) continue;

    if (line.starts_with("#")) continue;

    // If the line ends with ':', it starts a new cursor.
    if (Util::TryStripSuffix(":", &line)) {
      Util::RemoveOuterWhitespace(&line);
      current_cursor = std::string(line);
      continue;
    }

    CHECK(!current_cursor.empty()) << "Give a cursor name like pointer: "
      "before giving frames!";

    std::string_view img_file = Util::Chop(&line);
    std::string_view x_str = Util::Chop(&line);
    std::string_view y_str = Util::Chop(&line);

    CHECK(!img_file.empty() && !x_str.empty() && !y_str.empty());

    std::unique_ptr<ImageRGBA> frame(ImageRGBA::Load(img_file));
    CHECK(frame.get() != nullptr) << img_file << " not found";

    if (frame->Width() != frame->Height()) {
      Print("Warning: Cursor images should be square.\n");
    }

    // Let's also support fractional positions. If the string
    // ends with %, then treat it as a percentage of that dimension.
    auto ParseDim = [](std::string_view s, int dimension) -> int {
        if (Util::TryStripSuffix("%", &s)) {
          return std::round(Util::ParseDouble(s) * (dimension / 100.0));
        }
        return Util::ParseInt64(s);
      };

    int x = ParseDim(x_str, frame->Width());
    int y = ParseDim(y_str, frame->Height());

    config.cursors[current_cursor].sizes.push_back(ICO::Cursor{
        .img = std::move(*frame),
        .x = x,
        .y = y,
      });
  }

  return config;
}

static void Generate(const Config &config, std::string_view basename) {
  // TODO: Apparently it is possible to pack these into a single
  // "theme pack" file (CAB archive).

  for (const auto &[name, cursor] : config.cursors) {
    std::string filename = std::format("{}-{}.cur", basename, name);
    ICO::SaveCUR(filename, cursor.sizes);
    Print("Wrote {} to " AGREEN("{}") "\n",
          cursor.sizes.size(), filename);
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  CHECK(argc == 2) << "./makecursor cursor.cfg\n";
  std::string cfg_filename = argv[1];

  std::string_view basename = Util::FileBaseOf(cfg_filename);
  Generate(LoadConfig(cfg_filename), basename);

  Print("OK\n");
  return 0;
}
