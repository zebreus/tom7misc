
#include "util.h"

#include <array>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "yocto-math.h"

// This works but some of the VRML files are just wrong (clusters of
// points instead of individual ones) and other shapes are slightly rotated,
// which is annoying.
static void ConvertFromVRML() {

  Print("constexpr std::initializer_list<vec3> johnson_vertices[92] = {{\n");
  for (int i = 1; i <= 92; ++i) {
    std::string filename = std::format("j{:02d}.wrl", i);
    std::string content = Util::ReadFile(filename);

    Print("  {{\n");
    std::string_view sv = content;
    while (true) {
      size_t point_pos = sv.find("point");
      if (point_pos == std::string_view::npos) {
        break;
      }
      sv.remove_prefix(point_pos + 5);

      size_t open_pos = sv.find_first_not_of(" \t\n\r");
      if (open_pos != std::string_view::npos && sv[open_pos] == '[') {
        sv.remove_prefix(open_pos + 1);
        size_t close_pos = sv.find(']');
        if (close_pos != std::string_view::npos) {
          sv = sv.substr(0, close_pos);
          int coord_idx = 0;
          std::array<std::string, 3> coords;

          while (!sv.empty()) {
            size_t token_start = sv.find_first_not_of(" \t\n\r,");
            if (token_start == std::string_view::npos) {
              break;
            }
            sv.remove_prefix(token_start);

            size_t token_len = sv.find_first_of(" \t\n\r,");
            std::string_view token = sv.substr(0, token_len);

            Util::RemoveOuterWhitespace(&token);
            CHECK(Util::ParseDoubleOpt(token).has_value()) << token;

            coords[coord_idx++] = std::string(token);
            if (coord_idx == 3) {
              Print("    vec3{{" "{}, {}, {}" "}},\n",
                    coords[0], coords[1], coords[2]);
              coord_idx = 0;
            }

            if (token_len == std::string_view::npos) {
              break;
            }
            sv.remove_prefix(token_len);
          }
          break;
        }
      }
    }
    Print("  }},\n");
  }
  Print("}};\n");
}

int main() {
  ANSI::Init();

  Convert();

  return 0;
}
