
#include <cstdio>
#include <cstdint>
#include <string>
#include <cmath>
#include <set>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"

enum class Style {
  NINETY,
  EDGE,
};

// pins we actually use
static const std::set<int> CONNECTED = { 9, 10, 11, 13, };

// File format docs:
// https://www.compuphase.com/electronics/LibraryFileFormats.pdf

constexpr int NUM_INNER_PINS = 18;
// Everything in millimeters.
constexpr double innerpins = 22.82;
constexpr double small_ystride = innerpins / NUM_INNER_PINS;
constexpr double large_connwidth = 1.2;
constexpr double small_connwidth = 1.1;
constexpr double gapwidth = small_ystride - small_connwidth;
constexpr double large_ystride = gapwidth + large_connwidth;
// these can be tweaked. Bigger means more potential contact
// area (at 90deg) but smaller gap between pins, which may
// make soldering more challenging.
constexpr double small_padcopperdia = 0.95;
constexpr double large_padcopperdia = 1.05;
constexpr double paddrilldia = 0.6;
constexpr double padsquaringmargin = 1.0;
constexpr double padleft = large_padcopperdia + 2.0;

constexpr double markerleft = -(padleft + 1.0);
constexpr double markerlen = 2.0;

static string Float(double d) {
  int x = std::round(d * 1000.0);
  double r = x / 1000.0;
  return StringPrintf("%0.3f", r);
}


constexpr double press_fit = 0.025;
constexpr double cueheight = 32.0 + press_fit;
constexpr double boardcutlength = 27.0;
constexpr double tabin = 1.6 + press_fit;
constexpr double tabup = (cueheight - boardcutlength) / 2.0;
constexpr double cuewidth = 12.6 + press_fit;
// derived; should be about 11.0mm (measured).
constexpr double cueinnerwidth = cuewidth - tabin;

// the board is not symmetric in the hole, alas
// XXX measure this more carefully
constexpr double dtofirstconn = 0.65;

static Style style = Style::NINETY;

//     ^+-----------+
// tabup|    hole  ^|
//     ||          ||
//     v+--*    c  ||
//         |^   u  c|
//         o| <-e->u|
//         o|   i  e|
//         :bcut   h|
//         o|      e|
//         o|      i|
//         |v      g
//      +--+       h|
//      | ^        ||
//      | tabin    v|
//      +-----------+
//      <---cuewid-->

// give origin y (position of *).
string CueCutout(int stamp, double oy) {
  string ret;
  double cx = 0.0, cy = oy;
  int serial = 0;
  auto Relative = [stamp, &ret, &cx, &cy, &serial](
      double dx, double dy, bool emit = true) {
      double nx = cx + dx;
      double ny = cy + dy;
      if (emit) {
        StringAppendF(&ret,
                      "(fp_line (start %s %s) (end %s %s) "
                      "(layer \"Edge.Cuts\") (width 0) "
                      "(tstamp 62d%05x-e55a-4e26-9395-2055b67%05x))\n",
                      Float(cx).c_str(), Float(cy).c_str(),
                      Float(nx).c_str(), Float(ny).c_str(),
                      stamp, serial);
      }
      cx = nx;
      cy = ny;
      serial++;
    };

  Relative(-tabin, 0.0);
  Relative(0.0, -tabup);
  Relative(cuewidth, 0.0, style == Style::NINETY);
  Relative(0.0, cueheight, style == Style::NINETY);
  Relative(-cuewidth, 0.0, style == Style::NINETY);
  Relative(0.0, -tabup);
  Relative(tabin, 0.0);
  Relative(0.0, -boardcutlength);

  return ret;
}

int main(int argc, char **argv) {

  CHECK(argc == 2) << "pass 90deg, edge, etc.";
  const string sarg = argv[1];
  if (sarg == "90deg") {
    style = Style::NINETY;
  } else if (sarg == "edge") {
    style = Style::EDGE;
  } else {
    LOG(FATAL) << "Unknown style: " << sarg;
  }

  ArcFour rc(StringPrintf("%lld.%s", time(nullptr), sarg.c_str()));

  int stamp = RandTo(&rc, 0x100000);

  // Prelude
  printf(R"__(
(footprint "cuecastle" (version 20211014) (generator pcbnew)
  (layer "F.Cu")
  (tedit 61F22DB3)
  (attr smd)
  (fp_text reference "REF**" (at -6 -1.5 unlocked) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15)))
    (tstamp 5b9%05x-46ba-4466-8241-fbc1cd0e9bbd)
  )
  (fp_text value "cuecastle%s" (at -9 -3.5 unlocked) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15)))
    (tstamp fce%05x-eec8-5979-aefd-686432f00d6c)
  )
)__", stamp, sarg.c_str(), stamp); /* " */

  double cut_origin = -(large_connwidth / 2.0 + dtofirstconn);
  printf("%s\n", CueCutout(stamp, cut_origin).c_str());

  // board cut along x=0
  constexpr float xpos = 0.0;
  // Now, each pad.
  double ypos = 0.0;
  for (int i = 0; i < 20; i++) {
    const bool is_large = i == 0 || i == 19;

    const double copperdia =
      is_large ? large_padcopperdia : small_padcopperdia;
    const double ystride =
      is_large ? large_ystride : small_ystride;

    printf("(pad \"%d\" thru_hole custom "
           "(at %s %s) (size %s %s) (drill %s) "
           "(property pad_prop_castellated) (layers *.Cu *.Mask)\n",
           i + 1,
           Float(xpos).c_str(), Float(ypos).c_str(),
           Float(copperdia).c_str(), Float(copperdia).c_str(),
           Float(paddrilldia).c_str());
    printf("(options (clearance outline) (anchor circle))\n"
           "(primitives\n"
           ") (tstamp 23e%05x-c0de-4335-93c2-5c91dfd%05d))\n",
           stamp,
           i);


    // Filled polygon to make left side of pad square.
    auto PolyPoints = [&]() {
        //    1            2
        //
        //            3
        //                 o
        //            4
        //
        //    6            5

        // always use large dia for left, so that they all
        // line up.
        double xleft = -(large_padcopperdia / 2.0 + padsquaringmargin);
        double xmid = -(paddrilldia / 2.0 +
                        (copperdia - paddrilldia) / 4.0);
        double ytop = ypos - copperdia / 2.0;
        double ybot = ypos + copperdia / 2.0;
        return StringPrintf(
            "(xy %s %s)\n"
            "(xy %s %s)\n"
            "(xy %s %s)\n"
            "(xy %s %s)\n"
            "(xy %s %s)\n"
            "(xy %s %s)\n",
            Float(xleft).c_str(), Float(ytop).c_str(),
            Float(0).c_str(), Float(ytop).c_str(),
            Float(xmid).c_str(), Float(ypos - copperdia / 4.0).c_str(),
            Float(xmid).c_str(), Float(ypos + copperdia / 4.0).c_str(),
            Float(0).c_str(), Float(ybot).c_str(),
            Float(xleft).c_str(), Float(ybot).c_str());
      };

    int serial = 0;
    for (const std::string layer : {"F.Cu", "B.Cu", "F.Mask", "B.Mask"}) {
      printf("(fp_poly (pts\n"
             "%s"
             ") (layer \"%s\") (width 0) (fill solid) "
             "(tstamp 53c%05x-cafe-4fae-a84%05x-721cfc0%05d))\n",
             PolyPoints().c_str(),
             layer.c_str(),
             serial,
             stamp, i);
      serial++;
    }

    // TODO: (unmasked) square to left, too?

    if (CONNECTED.find(i + 1) != CONNECTED.end()) {
      // Mark pins that we actually use visually

      printf("(fp_line (start %s %s) (end %s %s) "
             "(layer \"F.SilkS\") (width 0.12) "
             "(tstamp 808%05x-e55a-4e26-9395-2055b67%05x))\n",
             Float(markerleft).c_str(), Float(ypos).c_str(),
             Float(markerleft + markerlen).c_str(), Float(ypos).c_str(),
             stamp, 777);
    }

    ypos += ystride;
  }

  printf(")\n");

  return 0;
}
