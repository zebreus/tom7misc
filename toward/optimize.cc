#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "layout.h"
#include "optimization.h"
#include "status-bar.h"
#include "util.h"

int main(int argc, char **argv) {
  if (argc != 3) {
    Print("Usage: {} <input.layout> <output.layout>\n", argv[0]);
    return 1;
  }

  std::string input_filename = argv[1];
  std::string output_filename = argv[2];

  std::string content = Util::ReadFile(input_filename);
  CHECK(!content.empty());

  std::optional<Layout> opt_layout = LayoutEngine::Parse(content);
  CHECK(opt_layout.has_value())
      << "Failed to parse layout from " << input_filename;

  CellLibrary library;

  StatusBar status(1);
  status.Print("Optimizing layout...\n");
  Layout optimized = Optimization::Optimize(library, opt_layout.value(),
                                            &status);

  std::vector<uint8_t> out_data = LayoutEngine::Serialize(optimized);
  CHECK(Util::WriteFileBytes(output_filename, out_data));

  status.Print("Wrote {} bytes to {}.\n", out_data.size(), output_filename);
  return 0;
}
