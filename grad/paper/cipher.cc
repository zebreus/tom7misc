
#include <array>
#include <string>

#include "textsvg.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"

using namespace std;

// Reading bits from left (msb) to right (lsb), this gives
// the output location for each bit. So for example the
// first entry says that the 0th bit in the input is sent
// to the 49th bit in the output.
static constexpr std::array<int, 64> bit_indices = {
  49, 44, 34, 41, 0, 29, 40, 50, 39, 59, 8, 52, 35, 38,
  51, 3, 46, 43, 48, 31, 47, 23, 10, 5, 11, 12, 16, 36,
  60, 42, 19, 57, 22, 30, 4, 33, 15, 6, 45, 53, 61, 58,
  24, 54, 26, 63, 17, 55, 37, 56, 28, 2, 9, 1, 27, 62,
  18, 32, 21, 13, 20, 7, 25, 14,
};

static void Permutation(const string &outfile) {

  double byte_size = 200.0;
  double box_pct = 0.90;

  string svg = TextSVG::HeaderEx(0, 0, byte_size * 8, 4 * byte_size, "px", "cipher.cc");

  double topy = 0.0;
  double boty = byte_size * 3.0;
  const double margin = ((1.0 - box_pct) * byte_size) / 2.0;

  auto BitCenter = [&](int idx) {
      int byte = idx / 8;
      int bit = idx % 8;

      double bytex = (byte * byte_size) + margin;
      double w = byte_size - margin;
      double bit_width = w / 8.0;

      return bytex + bit_width * (bit + 0.5);
    };

  auto Byte = [&](double x, double y) {
      double w = byte_size - margin;
      double h = byte_size - margin;
      double bit_width = w / 8.0;

      double top = y + margin;
      // Bits inside
      for (int i = 1; i < 8; i++) {
        double bitx = x + margin + (i * bit_width);
        StringAppendF(&svg, "<line x1=\"%s\" y1=\"%s\" x2=\"%s\" y2=\"%s\" "
                      "stroke=\"#000\" stroke-opacity=\"0.75\" stroke-width=\"2\" />\n",
                      TextSVG::Rtos(bitx).c_str(), TextSVG::Rtos(top).c_str(),
                      TextSVG::Rtos(bitx).c_str(), TextSVG::Rtos(top + h).c_str());
      }

      // Byte box, above
      StringAppendF(&svg, "<rect fill=\"none\" stroke=\"#000\" stroke-width=\"3\" "
                    "x=\"%s\" y=\"%s\" width=\"%s\" height=\"%s\" rx=\"1\" />\n",
                    TextSVG::Rtos(x + margin).c_str(), TextSVG::Rtos(top).c_str(),
                    TextSVG::Rtos(byte_size - margin).c_str(), TextSVG::Rtos(h).c_str());
    };

  for (int i = 0; i < 8; i++) {
    // Draw byte boxes, top and bottom.
    Byte(i * byte_size, topy);
    Byte(i * byte_size, boty);
  }

  // Draw arrows
  for (int i = 0; i < 64; i++) {
    double y1 = byte_size + 0.05 * byte_size;
    double y1b = byte_size + 0.05 * byte_size + byte_size;
    double y2b = 3 * byte_size - 0.05 * byte_size - byte_size;
    double y2 = 3 * byte_size - 0.05 * byte_size;

    double x1 = BitCenter(i);
    double x2 = BitCenter(bit_indices[i]);
    /*
    StringAppendF(&svg, "<line x1=\"%s\" y1=\"%s\" x2=\"%s\" y2=\"%s\" "
                  "stroke=\"#333\" stroke-width=\"2\" />\n",
                  TextSVG::Rtos(x1).c_str(), TextSVG::Rtos(y1).c_str(),
                  TextSVG::Rtos(x2).c_str(), TextSVG::Rtos(y2).c_str());
    */
    StringAppendF(&svg, "<path d=\"M %s %s C %s %s, %s %s, %s, %s\" "
                  "fill=\"none\" stroke=\"#000\" stroke-opacity=\"0.8\" stroke-width=\"2\" />\n",
                  TextSVG::Rtos(x1).c_str(), TextSVG::Rtos(y1).c_str(),
                  TextSVG::Rtos(x1).c_str(), TextSVG::Rtos(y1b).c_str(),
                  TextSVG::Rtos(x2).c_str(), TextSVG::Rtos(y2b).c_str(),
                  TextSVG::Rtos(x2).c_str(), TextSVG::Rtos(y2).c_str());

  }

  svg += TextSVG::Footer();
  Util::WriteFile(outfile, svg);
}

int main(int argc, char **argv) {
  CHECK(argc == 2);
  string outfile = argv[1];

  Permutation(outfile);

  return 0;
}
