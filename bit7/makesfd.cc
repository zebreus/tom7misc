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

constexpr int CHARS_ACROSS = 16;
constexpr int CHARS_DOWN = 8;

// XXX remove
static constexpr bool verbose = false;

template<class C, class K>
static bool ContainsKey(const C &c, const K &k) {
  return c.find(k) != c.end();
}

namespace {
struct Glyph {
  // Can be negative, allowing for overhang on a character like j, for example.
  int left_edge = 0;
  // Height will be charbox_height; width of the image may vary from glyph to glyph.
  // This is a 1-bit bitmap; 0 means "off" (transparent) and any other value is "on".
  ImageA pic;
};
}

// Scale these coordinates, probably?
// XXX We also want to remove colinear points.
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

        vector<pair<int, int>> points = VectorizeOne(bitmap);
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
  CHECK(argc == 5) << "Usage: ./makesfd.exe charbox_w charbox_h descent file.png\n";
  const int charbox_width = atoi(argv[1]);
  const int charbox_height = atoi(argv[2]);
  const int descent = atoi(argv[3]);
  const string filename = argv[4];
  CHECK(charbox_width > 0) << "Width must be a number";
  CHECK(charbox_height > 0) << "Height must be a number";
  CHECK(descent >= 0) << "Descent must be a number";
  CHECK(!filename.empty());

  // 'spacing' is presentational in makegrid; we derive the width
  // from the black line in each character cell.
  
  std::unique_ptr<ImageRGBA> input(ImageRGBA::Load(filename));
  CHECK(input.get() != nullptr) << "Couldn't load: " << filename;
  CHECK(CHARS_ACROSS * charbox_width == input->Width() &&
        CHARS_DOWN * charbox_height == input->Height()) <<
    "Image should be " << (CHARS_ACROSS * charbox_width) << "x"
                       << (CHARS_DOWN * charbox_height) << " but got "
                       << input->Width() << "x" << input->Height();
  
  std::map<int, Glyph> font;
  
  for (int cy = 0; cy < CHARS_DOWN; cy++) {
    for (int cx = 0; cx < CHARS_ACROSS; cx++) {
      const int cidx = CHARS_ACROSS * cy + cx;
      // TODO: Allow some mapping of codepoints!
      const int codepoint = cidx;

      // Get width, by searching for a column of all black.
      auto GetWidth = [&]() {
          for (int x = 0; x < charbox_width; x++) {
            auto IsBlackColumn = [&]() {
                int sx = cx * charbox_width + x;
                for (int y = 0; y < charbox_height; y++) {
                  int sy = cy * charbox_height + y;
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
          for (int y = 0; y < charbox_height; y++) {
            for (int x = 0; x < charbox_width; x++) {
              int sx = cx * charbox_width + x;
              int sy = cy * charbox_height + y;
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
        ImageA pic{width, charbox_height};
        pic.Clear(0x00);
      
        for (int y = 0; y < charbox_height; y++) {
          for (int x = 0; x < width; x++) {
            int sx = cx * charbox_width + x;
            int sy = cy * charbox_height + y;
            bool bit = input->GetPixel32(sx, sy) == 0xFFFFFFFF;
            if (bit) pic.SetPixel(x, y, 0xFF);
          }
        }

        Glyph *glyph = &font[codepoint];
        // No way to set this from image yet...
        glyph->left_edge = 0;
        glyph->pic = std::move(pic);
      }
    }
  }

  {
    // Output test pattern PNG.
    #define INFTY "\x13"
    #define NOTES "\x12"
    #define HEART "\x11"
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
     "  123,456 * 7,890 = 974,067,840 ",
     "",
     "  jungle quip, " INFTY " If you knew where you'd fall,",
     "  TTTTTT QQQQ` " INFTY  " you'd put a pillow!",
     "  http://.com/ " INFTY  " (watch--said I--beloved)",
    };

    const int output_height = charbox_height + 1;
    
    ImageRGBA test(400, output_height * testpattern.size());
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
    
    test.ScaleBy(3).Save("makesdf-testpattern.png");
  }

  const double one_pixel = 1.0 / charbox_height;
  
  TTF::Font ttf_font;
  for (const auto &[codepoint, glyph] : font) {
    TTF::Char ch = Vectorize(glyph);

    ch.width *= one_pixel;
    TTF::MapCoords([one_pixel](float x, float y) {
        return make_pair(x * one_pixel, y * one_pixel);
      }, &ch);
    
    ttf_font.chars[codepoint] = std::move(ch);
  }

  ttf_font.baseline = 1.0 - (descent * one_pixel);
  ttf_font.linegap = one_pixel;

  const string sfd =
  ttf_font.ToSFD(
      "Bitmap",
      "Generated by makesfd.exe");
  Util::WriteFile("generated.sfd", sfd);
  
  return 0;
}
