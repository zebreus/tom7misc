// Generate a text SFD file from a PNG containing a proportional font.
// The characters are arranged in a grid a la makegrid.cc. White
// (#fff) pixels give the character shapes, with a solid vertical
// black (#000) line gives (one past) the width.

// TODO: I changed this encoding and I need to update the DFX fonts!

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "font-image.h"
#include "fonts/island-finder.h"
#include "fonts/ttf.h"
#include "image.h"
#include "util.h"

using namespace std;
using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

using Glyph = FontImage::Glyph;

// XXX remove
static constexpr bool VERBOSE = false;

template<class C, class K>
static bool ContainsKey(const C &c, const K &k) {
  return c.find(k) != c.end();
}

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
  for (const auto &[ex, ey] : points) {
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
// The bitmap must have one contiguous non-empty region with values >0,
// which is the shape to trace.
//
// This is based on code that was tracing SDFs (from ../lowercase) which
// might account for some overkill therein?
static vector<pair<int, int>> VectorizeOne(const ImageA &bitmap) {
  auto InBlob = [&bitmap](int x, int y) -> bool {
      if (x < 0 || y < 0 || x >= bitmap.Width() || y >= bitmap.Height())
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
      // Print("Loop finished!\n");
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
      if (VERBOSE) Print("DEPTH {}/{}\n", d, maps.max_depth);
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
        if (VERBOSE) {
          Print("Tracing eqc {} (descendants:", this_eqc);
          for (uint8 d : descendants) Print(" {}", d);
          Print(")\n");
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

  const Config config = ParseAndCheckConfig(argv[1]);
  if (VERBOSE)
    Print("Converting from {}\n", argv[1]);
  const string out_sfd = argv[2];
  const string out_test_png = (argc > 3) ? argv[3] : "";

  FontImage font_image(config);
  auto &unicode = font_image.unicode_to_glyph;

  if (config.no_lowercase) {
    for (int c = 'A'; c <= 'Z'; c++) {
      int lc = c | 32;
      auto lit = unicode.find(lc);
      bool lc_missing = lit == unicode.end() ||
        (config.fixed_width &&
         FontImage::EmptyGlyph(font_image.glyphs[lit->second]));
      auto cit = unicode.find(c);
      if (lc_missing && cit != unicode.end()) {
        // Use same glyph.
        unicode[lc] = cit->second;
      }
    }
  }

  if (!out_test_png.empty()) {
    const int BORDER = 2;
    const int output_height = config.charbox_height + config.extra_linespacing;

    // Output test pattern PNG.
    // Heart can't actually go here because it is \0.
    #define HEART "<3"
    #define INFTY "\x02"
    #define NOTES "\x01"
    #define PLUSMINUS "\x03"
    #define DEG    "\x04"
    std::vector<string> testpattern = {
      ("  Welcome to my font!  it is cozy here " HEART "  (ok) "),
      ("  Now is the FALL-TIME of our DISCONTENT !!|1Il "),
      "",
      "",
      ("  " NOTES " Enable hyper-drive      for (;;) {"),
      ("  " NOTES " Enable ultra-disc         Print(\"hi?\\n\"); "),
      ("  " NOTES " Disable introspection   }"),
      "",
      "  Mr. Jock, TV Quiz Ph.D., bags few lynx!  ",
      "  (glib jocks quiz nymph to vex dwarf) ",
      "  (SYMPATHIZING WOULD FIX QUAKER OBJECTIVES.) ",
      "  XW!@#$%^&*()-=_+{}[]\\|:\";'<>?,./ZXCVB~` ",
      "",
      ("  " PLUSMINUS "123,456 * 7,890 = 974,067,840" DEG),
      "",
      ("  jungle quip, " INFTY " If you knew where you'd fall,"),
      ("  TTTTTT QQQQ` " INFTY  " you'd put a pillow!"),
      ("  http://.com/ " INFTY  " (watch--said I--beloved)"),
    };

    ImageRGBA test(config.charbox_width * 48 + 2 * BORDER,
                   output_height * testpattern.size() + 2 * BORDER);
    test.Clear32(0x000033FF);

    for (int lidx = 0; lidx < (int)testpattern.size(); lidx++) {
      const string &line = testpattern[lidx];
      const int ypos = BORDER + lidx * output_height;
      int xpos = BORDER;
      for (int cidx = 0; cidx < (int)line.size(); cidx++) {
        const int codepoint = line[cidx];
        auto it = unicode.find(codepoint);
        if (it != unicode.end()) {
          // Converting to imagergba each time obviously wasteful...!
          int glyph_idx = it->second;
          const Glyph &glyph = font_image.glyphs[glyph_idx];
          ImageRGBA glyph_img = glyph.pic.AlphaMaskRGBA(0xEE, 0xEE, 0xFF);
          test.BlendImage(xpos, ypos, glyph_img);
          xpos += glyph_img.Width();
          // XXX left edge support
        }
      }
    }

    test.ScaleBy(3).Save(out_test_png);
  }

  const double one_pixel = 1.0 / config.charbox_height;

  TTF::Font ttf_font;

  for (const auto &[codepoint, glyph_idx] : font_image.unicode_to_glyph) {
    // (PERF: There are often multiple codepoints that use the
    // same glyph. I think it's possible for a single outline to be used
    // for multiple characters, so we should do that instead! Or
    // perhaps even better to just dedupe as a separate matter.)
    const FontImage::Glyph &glyph = font_image.glyphs[glyph_idx];
    TTF::Char ch = Vectorize(glyph);

    ch.width *= one_pixel;
    TTF::MapCoords([one_pixel](float x, float y) {
        return make_pair(x * one_pixel, y * one_pixel);
      }, &ch);

    ttf_font.chars[codepoint] = std::move(ch);
  }

  ttf_font.baseline = 1.0 - (config.descent * one_pixel);
  ttf_font.linegap = config.extra_linespacing * one_pixel;
  // Might only affect FontForge, but it at least looks better in the
  // editor without anti-aliasing.
  ttf_font.antialias = false;
  ttf_font.bitmap_grid_height = config.charbox_height;
  // "Frog" is reserved for Tom 7!
  for (int i = 0; i < 4; i++)
    ttf_font.vendor[i] = config.vendor[i];
  ttf_font.copyright = config.copyright;

  const string sfd = ttf_font.ToSFD(config.name);
  Util::WriteFile(out_sfd, sfd);

  return 0;
}
