// Generate a text SFD file from a PNG containing a proportional font.
// The characters are arranged in a grid a la makegrid.cc. White
// (#fff) pixels give the character shapes, with a solid vertical
// black (#000) line gives (one past) the width.

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
#include "fonts/island-finder.h"
#include "fonts/ttf.h"

using namespace std;
using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

// TODO: Would be nice for this to be configurable!
// Standard layout of input file, based on characters
// that were useful for DestroyFX. If -1, the spot
// is unclaimed. Should be fine to extend past 128
// characters by increasing CHARS_DOWN too.
constexpr int MAPPED_CHARS_ACROSS = 16;
constexpr int MAPPED_CHARS_DOWN = 16;

static constexpr array<int, MAPPED_CHARS_ACROSS * MAPPED_CHARS_DOWN>
CODEPOINTS = {
  // First line
  // BLACK HEART SUIT
  0x2665,
  // BEAMED EIGHTH NOTES
  0x266B,
  // INFINITY
  0x221E,
  // SQUARE ROOT
  0x221A,
  // LESS THAN OR EQUAL TO
  0x2264,
  // GREATER THAN OR EQUAL TO
  0x2265,
  // APPROXIMATELY EQUAL
  0x2248,
  // EURO SIGN
  0x20AC,
  // ARROWS: LEFT, UP, RIGHT, DOWN
  0x2190, 0x2191, 0x2192, 0x2193,

  // Remainder of line unclaimed
  -1, -1, -1, -1,
  // Second line, unclaimed
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  // ASCII, mapped to itself
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
  0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
  0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
  0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
  0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, -1,
  // These are control characters. Can claim 'em.
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  // Unicode Latin-1 Supplement, mapped to itself.
  // See https://en.wikibooks.org/wiki/Unicode/Character_reference/0000-0FFF
  0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
  0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
  0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
  0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
  0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
  0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
};


// XXX remove
static constexpr bool verbose = false;

template<class C, class K>
static bool ContainsKey(const C &c, const K &k) {
  return c.find(k) != c.end();
}

namespace {
struct Glyph {
  // Can be negative, allowing for overhang on a character like j, for example.
  // XXX not implemented
  int left_edge = 0;
  // Height will be charbox_height; width of the image may vary from
  // glyph to glyph. This is a 1-bit bitmap; 0 means "off"
  // (transparent) and any other value is "on".
  ImageA pic;
};

struct Config {
  string pngfile;

  string name;
  string copyright;

  // If true, copy uppercase letters to lowercase (where missing).
  bool no_lowercase = false;

  // Size of regular grid in input image (see e.g. makegrid).
  int charbox_width = 0;
  int charbox_height = 0;
  // Pixels at the bottom of the charbox that are beneath the baseline.
  int descent = 0;

  // Arrangement of characters in PNG file.
  int chars_across = 16;
  int chars_down = 8;

  // Additional space between lines, in pixels.
  int extra_linespacing = 0;
};
}

static Config ParseConfig(const std::string &cfgfile) {
  Config config;
  std::map<string, string> m = Util::ReadFileToMap(cfgfile);
  CHECK(!m.empty()) << "Couldn't read config file " << cfgfile;
  config.pngfile = m["pngfile"];
  CHECK(!config.pngfile.empty()) << "Required config line: pngfile";
  config.name = m["name"];
  CHECK(!config.name.empty()) << "Required config line: name";
  config.copyright = m["copyright"];
  config.charbox_width = atoi(m["charbox-width"].c_str());
  CHECK(config.charbox_width > 0) << "Config line charbox-width must be >0";
  config.charbox_height = atoi(m["charbox-height"].c_str());
  CHECK(config.charbox_height > 0) << "Config line charbox-height must be >0";
  config.charbox_height = atoi(m["charbox-height"].c_str());
  config.descent = atoi(m["descent"].c_str());
  CHECK(config.descent >= 0) << "Config line charbox-height must be >= 0";

  if (m.find("chars-across") != m.end())
    config.chars_across = atoi(m["chars-across"].c_str());
  CHECK(config.chars_across > 0);

  if (m.find("chars-down") != m.end())
    config.chars_down = atoi(m["chars-down"].c_str());
  CHECK(config.chars_down > 0);

  if (m.find("extra-linespacing") != m.end())
    config.extra_linespacing = atoi(m["extra-linespacing"].c_str());

  if (m.find("no-lowercase") != m.end())
    config.no_lowercase = true;

  return config;
}

// Given a series of points on the grid that trace a proper outline
// (e.g., it has nonzero area, consecutive points are in different
// locations), generate an equivalent but more efficient outline
// by skipping points that are colinear with their neighbors. (The
// routine below generates one on every pixel corner, even when
// unnecessary.) Note that this does not handle a case like
// 1 ----- 3 ----- 2
// where a line doubles back on itself.
static vector<pair<int, int>> RemoveColinearPoints(
    const vector<pair<int, int>> &points) {
  CHECK(points.size() >= 3) << "Degenerate contour; too small!";
  // Start on a corner so that we don't need to think about that
  // edge case.
  const int corner_idx = [&points](){
      for (int idx = 0; idx < (int)points.size(); idx++) {
        int prev_idx = idx == 0 ? points.size() - 1 : (idx - 1);
        int next_idx = idx == ((int)points.size() - 1) ? 0 : idx + 1;

        // If these three points are colinear, then they will
        // share an x coordinate or y coordinate. Otherwise,
        // the center one is a corner.
        const auto [px, py] = points[prev_idx];
        const auto [x, y] = points[idx];
        const auto [nx, ny] = points[next_idx];
        if ((px == x && x == nx) ||
            (py == y && y == ny)) {
          // colinear
        } else {
          return idx;
        }
      }
      LOG(FATAL) << "Degenerate contour; no area!";
    }();

  // We definitely keep the corner index. Now loop over all the
  // points starting there, and emit points if they
  vector<pair<int, int>> out;
  out.reserve(points.size());
  out.push_back(points[corner_idx]);
  auto Observe = [&points, &out](int idx) {
      CHECK(idx >= 0 && idx < (int)points.size());
      int next_idx = idx == ((int)points.size() - 1) ? 0 : idx + 1;
      auto [px, py] = out.back();
      auto [x, y] = points[idx];
      auto [nx, ny] = points[next_idx];
      if ((px == x && x == nx) ||
          (py == y && y == ny)) {
        // colinear. skip it.
      } else {
        out.emplace_back(x, y);
      }
    };
  for (int i = corner_idx + 1; i < (int)points.size(); i++)
    Observe(i);
  for (int i = 0; i < corner_idx; i++)
    Observe(i);
  return out;
}

// Scale these coordinates, probably?
static TTF::Contour MakeContour(const vector<pair<int, int>> &points) {
  // Just return straight lines between these edge points.
  TTF::Contour ret;
  for (const auto [ex, ey] : points) {
    ret.paths.emplace_back((float)ex, (float)ey);
  }
  return ret;
}

// Trace a single pixel blob in a bitmap, producing a single clockwise
// contour. This returns a series of points, on each of the pixel
// corners, which should be further simplified to remove colinear
// points. (0, 0) is the top-left corner of the top-left pixel, with
// y increasing downward.
//
// The bitmap must have oine contiguous non-empty region with values >0,
// which is the shape to trace.
//
// This is based on code that was tracing SDFs (from ../lowercase) which
// might account for some overkill therein?
static vector<pair<int, int>> VectorizeOne(const ImageA &bitmap) {
  auto InBlob = [&bitmap](int x, int y) -> bool {
      if (x < 0 || y <0 || x >= bitmap.Width() || y >= bitmap.Height())
        return false;
      return bitmap.GetPixel(x, y) > 0;
    };

  // First, find a pixel inside the blob.
  // This pixel has the property that there is no pixel with
  // a smaller y coordinate, which is also in the blob.
  const auto [startpx, startpy] = [&bitmap, InBlob]() ->
    std::pair<int, int> {
      for (int y = 0; y < bitmap.Height(); y++) {
        for (int x = 0; x < bitmap.Width(); x++) {
          if (InBlob(x, y)) return make_pair(x, y);
        }
      }
      LOG(FATAL) << "VectorizeOne requires a non-empty bitmap!";
  }();

  // We wind around the pixel blob's exterior, always maintaining a
  // direction and a pair of pixels, one in, and one out. The starting
  // pixel we just found is such an example we scanned from top to
  // bottom. We'll be done when we return to the start pixel.
  CHECK(!InBlob(startpx, startpy - 1)) << "Need the uppermost pixel "
    "in this column.";

  // Discrete direction. The code below is written for a pattern
  // where we are moving right, with the blob down, and the
  // exterior up (which is the start condition), but it naturally
  // rotates to the other directions.
  enum Dir {
    UP,
    DOWN,
    LEFT,
    RIGHT,
  };

  // Get the orthogonal "normal" direction, which is up for Right.
  auto Normal = [](Dir d) {
      switch (d) {
      case RIGHT: return UP;
      case LEFT: return DOWN;
      case DOWN: return RIGHT;
      case UP: return LEFT;
      }
      CHECK(false) << "Bad dir";
    };

  auto TurnCCW = Normal;

  auto TurnCW = [](Dir d) {
      switch (d) {
      case RIGHT: return DOWN;
      case DOWN: return LEFT;
      case LEFT: return UP;
      case UP: return RIGHT;
      }
      CHECK(false) << "Bad dir";
    };

  auto Move = [](int x, int y, Dir d) -> pair<int, int> {
    switch (d) {
    case RIGHT: return make_pair(x + 1, y);
    case LEFT: return make_pair(x - 1, y);
    case DOWN: return make_pair(x, y + 1);
    case UP: return make_pair(x, y - 1);
    }
    CHECK(false) << "Bad dir";
  };

  // Return the starting corner when we are visiting the pixel at px,py
  // in the given direction. For example, if we are moving right, then
  // the top-left corner of the pixel (which is px,py) is the start of
  // a clockwise path around it. If we are moving up, then it is the
  // bottom-left corner (px, py+1), etc.
  auto SourceCorner = [](int px, int py, Dir dir) {
      switch (dir) {
      case RIGHT: return make_pair(px, py);
      case LEFT: return make_pair(px + 1, py + 1);
      case DOWN: return make_pair(px + 1, py);
      case UP: return make_pair(px, py + 1);
      }
      CHECK(false) << "Bad dir";
    };

  // Pixel we're currently looking at.
  int px = startpx;
  int py = startpy;

  // Direction we're currently heading. We are at the top of the
  // blob, so go right for clockwise. (It seems any local top
  // would work; compare for example the inner top edges of an 's'
  // shape.)
  Dir right = RIGHT;


  vector<std::pair<int, int>> edge_points;
  for (;;) {
    Dir up = Normal(right);
    const auto [upx, upy] = Move(px, py, up);
    // Invariant is that we are on the edge, so px,py is
    // in the blob and the pixel "above" it is not.
    CHECK(InBlob(px, py));
    CHECK(!InBlob(upx, upy));

    edge_points.push_back(SourceCorner(px, py, right));

    // We're in a situation like this (perhaps under some rotation),
    // traveling along the top edge of the filled pixel at px,py.
    // We'll proceed by case analysis on the two pixels ahead of us.
    //
    // source corner
    //    |  +--+
    //    v  |a?|
    //    +->+--+
    //    |##|b?|
    //    +--+--+

    const auto [ax, ay] = Move(upx, upy, right);
    const auto [bx, by] = Move(px, py, right);
    const bool a = InBlob(ax, ay);
    const bool b = InBlob(bx, by);

    if (!a && b) {
      // +--+--+
      // |  |a |
      // +--+--+
      // |##|b#|
      // +--+--+
      // Just continue in the same direction.
      px = bx;
      py = by;
    } else if (!a && !b) {
      // +--+--+
      // |  |a |
      // +--+--+
      // |##|b |
      // +--+--+
      // Make a 90 degree turn around this pixel, but
      // stay on it.
      right = TurnCW(right);
    } else {
      CHECK(a);
      // +--+--+
      // |  |a#|
      // +--+--+
      // |##|b?|
      // +--+--+
      // Don't care what b is (we are using 4-connectivity);
      // if it's open we'll get there separately.

      px = ax;
      py = ay;
      right = TurnCCW(right);
    }

    // Consider the case of a single pixel. We should
    // only end when we approach it with right = RIGHT, right?
    if (px == startpx &&
        py == startpy && right == RIGHT)  {
      // printf("Loop finished!\n");
      break;
    }
  }

  return edge_points;
}

// Returns coordinates measured in pixels, not in the normalized [0,1]
// representation. Caller should normalize.
static TTF::Char Vectorize(const Glyph &glyph) {
  const IslandFinder::Maps maps = IslandFinder::Find(glyph.pic);

  // Tracing follows the same recursive approach as in SDF-based code
  // (../lowercase/font-problem) but is much simpler here since we
  // have exact data.

  std::function<vector<TTF::Contour>(int, uint8)> VectorizeRec =
    [&maps, &VectorizeRec](int d, uint8 parent) -> vector<TTF::Contour> {
      if (verbose) printf("DEPTH %d/%d\n", d, maps.max_depth);
      if (d > maps.max_depth) return {};

      std::vector<TTF::Contour> contours;

      // Get the equivalence classes at this depth, paired with the set
      // of (strict) descendants of that class (we want to remove
      // these holes when tracing the contour to simplify our lives).
      // Only consider classes that have 'parent' as an ancestor.
      // We'll run the routine below on each one.
      std::map<uint8, std::set<uint8>> eqclasses;
      for (int y = 0; y < maps.eqclass.Height(); y++) {
        for (int x = 0; x < maps.eqclass.Width(); x++) {
          if ((int)maps.depth.GetPixel(x, y) >= d) {
            uint8 eqc = maps.eqclass.GetPixel(x, y);
            if (maps.HasAncestor(eqc, parent)) {
              // This must exist because the depth is at least d, and
              // d > 0.
              uint8 ancestor = maps.GetAncestorAtDepth(d, eqc);
              eqclasses[ancestor].insert(eqc);
            }
          }
        }
      }

      // Now, for each component...
      for (const auto &[this_eqc, descendants] : eqclasses) {
        if (verbose) {
          printf("Tracing eqc %d (descendants:", this_eqc);
          for (uint8 d : descendants) printf(" %d", d);
          printf(")\n");
        }
        // Generate a simplified bitmap.
        ImageA bitmap(maps.eqclass.Width(), maps.eqclass.Height());
        for (int y = 0; y < maps.eqclass.Height(); y++) {
          for (int x = 0; x < maps.eqclass.Width(); x++) {
            uint8 eqc = maps.eqclass.GetPixel(x, y);
            bool inside = eqc == this_eqc || ContainsKey(descendants, eqc);
            bitmap.SetPixel(x, y, inside ? 0xFF : 0x00);
          }
        }

        vector<pair<int, int>> points =
          RemoveColinearPoints(VectorizeOne(bitmap));
        contours.push_back(MakeContour(points));

        // Now recurse on descendants, if any.
        if (!descendants.empty()) {
          const auto child_contours = VectorizeRec(d + 1, this_eqc);
          // ... but reverse the winding order so that these cut out
          // (or maybe get reversed again).
          for (TTF::Contour cc : child_contours)
            contours.push_back(TTF::ReverseContour(cc));
        }
      }

      return contours;
    };

  // Start at depth 1, since we do not want any outline for the
  // surrounding "sea".
  vector<TTF::Contour> contours = VectorizeRec(1, 0);

  TTF::Char ttf_char;
  // TODO: Honor left_edge
  ttf_char.contours = std::move(contours);
  ttf_char.width = (float)glyph.pic.Width();
  return ttf_char;
}

int main(int argc, char **argv) {
  CHECK(argc == 3 || argc == 4) <<
    "Usage: ./makesfd.exe config.cfg out.sfd [testpattern.png]\n";

  const Config config = ParseConfig(argv[1]);
  const string out_sfd = argv[2];
  const string out_test_png = (argc > 3) ? argv[3] : "";

  const int chars_across = config.chars_across;
  const int chars_down = config.chars_down;

  // 'spacing' is presentational in makegrid; we derive the width
  // from the black line in each character cell.

  std::unique_ptr<ImageRGBA> input(ImageRGBA::Load(config.pngfile));
  CHECK(input.get() != nullptr) << "Couldn't load: " << config.pngfile;
  CHECK(chars_across * config.charbox_width == input->Width() &&
        chars_down * config.charbox_height == input->Height()) <<
    "Image with configured charboxes " << config.charbox_width << "x"
                                       << config.charbox_height <<
    " should be " << (chars_across * config.charbox_width) << "x"
                  << (chars_down * config.charbox_height) << " but got "
                  << input->Width() << "x" << input->Height();

  // Map from character index (position in image) to glyph.
  std::map<int, Glyph> font;

  for (int cy = 0; cy < chars_down; cy++) {
    for (int cx = 0; cx < chars_across; cx++) {
      const int cidx = chars_across * cy + cx;

      // Get width, by searching for a column of all black.
      auto GetWidth = [&]() {
          for (int x = 0; x < config.charbox_width; x++) {
            auto IsBlackColumn = [&]() {
                int sx = cx * config.charbox_width + x;
                for (int y = 0; y < config.charbox_height; y++) {
                  int sy = cy * config.charbox_height + y;
                  uint32 color = input->GetPixel32(sx, sy);
                  if (color != 0x000000FF) return false;
                }
                return true;
              };
            if (IsBlackColumn()) {
              return x;
            }
          }
          return -1;
        };
      // -1 if not found. This is tolerated for totally empty characters.
      const int width = GetWidth();

      auto IsEmpty = [&]() {
          for (int y = 0; y < config.charbox_height; y++) {
            for (int x = 0; x < config.charbox_width; x++) {
              int sx = cx * config.charbox_width + x;
              int sy = cy * config.charbox_height + y;
              uint32 color = input->GetPixel32(sx, sy);
              if (color == 0xFFFFFFFF) return false;
            }
          }
          return true;
        };

      if (width < 0) {
        if (!IsEmpty()) {
          printf("Character at cx=%d, cy=%d has no width (black column) but "
                 "has a glyph (white pixels).\n", cx, cy);
          return -1;
        }

        continue;
      } else if (width == 0) {
        printf("Character at cx=%d, cy=%d has zero width; not supported!\n",
               cx, cy);
        return -1;
      } else {
        // Glyph, but possibly an empty one...
        ImageA pic{width, config.charbox_height};
        pic.Clear(0x00);

        for (int y = 0; y < config.charbox_height; y++) {
          for (int x = 0; x < width; x++) {
            int sx = cx * config.charbox_width + x;
            int sy = cy * config.charbox_height + y;
            bool bit = input->GetPixel32(sx, sy) == 0xFFFFFFFF;
            if (bit) pic.SetPixel(x, y, 0xFF);
          }
        }

        Glyph *glyph = &font[cidx];
        // No way to set this from image yet...
        glyph->left_edge = 0;
        glyph->pic = std::move(pic);
      }
    }
  }

  if (config.no_lowercase) {
    for (int c = 'A'; c <= 'Z'; c++) {
      int lc = c | 32;
      if (font.find(c) != font.end() &&
          font.find(lc) == font.end()) {
        font[lc] = font[c];
      }
    }
  }

  if (!out_test_png.empty()) {
    const int output_height = config.charbox_height + config.extra_linespacing;

    // Output test pattern PNG.
    // Heart can't actually go here because it is \0.
    #define HEART "<3"
    #define INFTY "\x02"
    #define NOTES "\x01"
    #define PLUSMINUS "\x03"
    #define DEG    "\x04"
    vector<string> testpattern = {
      "  Welcome to my font!  it is cozy here " HEART "  (ok) ",
      "  Now is the FALL-TIME of our DISCONTENT !!|1Il ",
     "",
     "",
     "  " NOTES " Enable hyper-drive      for (;;) {",
     "  " NOTES " Enable ultra-disc         printf(\"hi?\\n\"); ",
     "  " NOTES " Disable introspection   }",
     "",
     "  Mr. Jock, TV Quiz Ph.D., bags few lynx!  ",
     "  (glib jocks quiz nymph to vex dwarf) ",
     "  (SYMPATHIZING WOULD FIX QUAKER OBJECTIVES.) ",
     "  XW!@#$%^&*()-=_+{}[]\\|:\";'<>?,./ZXCVB~` ",
     "",
     "  " PLUSMINUS "123,456 * 7,890 = 974,067,840" DEG,
     "",
     "  jungle quip, " INFTY " If you knew where you'd fall,",
     "  TTTTTT QQQQ` " INFTY  " you'd put a pillow!",
     "  http://.com/ " INFTY  " (watch--said I--beloved)",
    };

    ImageRGBA test(config.charbox_width * 32, output_height * testpattern.size());
    test.Clear32(0x000033FF);

    for (int lidx = 0; lidx < (int)testpattern.size(); lidx++) {
      const string &line = testpattern[lidx];
      const int ypos = lidx * output_height;
      int xpos = 2;
      for (int cidx = 0; cidx < (int)line.size(); cidx++) {
        const int codepoint = line[cidx];
        auto it = font.find(codepoint);
        if (it != font.end()) {
          // Converting to imagergba each time obviously wasteful...!
          ImageRGBA glyph = it->second.pic.AlphaMaskRGBA(0xEE, 0xEE, 0xFF);
          test.BlendImage(xpos, ypos, glyph);
          xpos += glyph.Width();
        }
      }
    }

    test.ScaleBy(3).Save(out_test_png);
  }

  const double one_pixel = 1.0 / config.charbox_height;

  TTF::Font ttf_font;
  for (const auto &[index, glyph] : font) {
    if (index >= (int)CODEPOINTS.size()) {
      printf("Skipping glyph at %d,%d because it is outside the codepoint "
             "array!\n", index % chars_across, index / chars_across);
      continue;
    }
    CHECK(index >= 0 && index < (int)CODEPOINTS.size());
    const int codepoint = CODEPOINTS[index];
    if (codepoint < 0) {
      printf("Skipping glyph at %d,%d because the codepoint is not "
             "configured!\n", index % chars_across, index / chars_across);
    } else {
      TTF::Char ch = Vectorize(glyph);

      ch.width *= one_pixel;
      TTF::MapCoords([one_pixel](float x, float y) {
          return make_pair(x * one_pixel, y * one_pixel);
        }, &ch);

      ttf_font.chars[codepoint] = std::move(ch);
    }
  }

  ttf_font.baseline = 1.0 - (config.descent * one_pixel);
  ttf_font.linegap = config.extra_linespacing * one_pixel;
  // Might only affect FontForge, but it at least looks better in the
  // editor without anti-aliasing.
  ttf_font.antialias = false;
  ttf_font.bitmap_grid_height = config.charbox_height;
  // Reserved for Tom 7!
  ttf_font.vendor = {'F', 'r', 'o', 'g'};
  ttf_font.copyright = config.copyright;

  const string sfd = ttf_font.ToSFD(config.name);
  Util::WriteFile(out_sfd, sfd);

  return 0;
}
