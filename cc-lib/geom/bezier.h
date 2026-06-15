
#ifndef _CC_LIB_GEOM_BEZIER_H
#define _CC_LIB_GEOM_BEZIER_H

#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Returns {closest_x, closest_y, dist}.
std::tuple<float, float, float>
DistanceFromPointToQuadBezier(
    // The point to test
    float px, float py,
    // Bezier start point
    float sx, float sy,
    // Bezier control point
    float cx, float cy,
    // Bezier end point
    float ex, float ey);

// Return a vector of endpoints, not including the start point (but
// including the end), to draw as individual line segments in order to
// approximate the given quadratic Bézier curve.
//
// Num should work as integral (then all math is integral) or
// floating-point types.
template<class Num = float>
inline std::vector<std::pair<Num, Num>> TesselateQuadBezier(
    // starting vertex
    Num x0, Num y0,
    // control point
    Num x1, Num y1,
    // end point
    Num x2, Num y2,
    Num max_error_squared = Num(2),
    int max_depth = 16);

// As above, but for cubic Béziers (two control points).
template<class Num = float>
inline std::vector<std::pair<Num, Num>> TesselateCubicBezier(
    // starting vertex
    Num x0, Num y0,
    // first control point
    Num x1, Num y1,
    // second control point
    Num x2, Num y2,
    // end point
    Num x3, Num y3,
    Num max_error_squared = Num(2),
    int max_depth = 16);



// --- implementations follow ----

template<class Num>
inline std::vector<std::pair<Num, Num>> TesselateQuadBezier(
    // starting vertex
    Num x0, Num y0,
    // control point
    Num x1, Num y1,
    // end point
    Num x2, Num y2,
    Num max_error_squared,
    int max_depth) {

  static_assert(std::is_arithmetic<Num>::value,
                "TesselateQuadBezier needs an integral or floating-point "
                "template argument.");

  std::vector<std::pair<Num, Num>> out;
  std::function<void(Num, Num, Num, Num, Num, Num, int)> Rec =
    [&out, max_error_squared, &Rec](Num x0, Num y0,
                                    Num x1, Num y1,
                                    Num x2, Num y2,
                                    int max_depth) {
      // This is based on public-domain code from stb_truetype; thanks!

      // Midpoint of the curve.
      // ("Midpoint" here likely means t/2, not the geometric midpoint?
      // So this might be overly conservative, in that we might have
      // a good approximation to a line but not pass near the line's
      // midpoint at the curve's midpoint. (Consider the case where the
      // control point is on the line, near one of the endpoints.))
      const Num mx = (x0 + (x1 * 2) + x2) / 4;
      const Num my = (y0 + (y1 * 2) + y2) / 4;

      // Midpoint of a straight line.
      const Num lx = (x0 + x2) / 2;
      const Num ly = (y0 + y2) / 2;

      // Error.
      const Num dx = lx - mx;
      const Num dy = ly - my;
      const Num error = (dx * dx) + (dy * dy);

      if (error > max_error_squared && max_depth > 0) {
        Rec(x0, y0, (x0 + x1) / 2, (y0 + y1) / 2, mx, my, max_depth - 1);
        Rec(mx, my, (x1 + x2) / 2, (y1 + y2) / 2, x2, y2, max_depth - 1);
      } else {
        // Otherwise, emit a straight line.
        out.emplace_back(x2, y2);
      }
    };

  Rec(x0, y0, x1, y1, x2, y2, max_depth);
  return out;
}

template<class Num>
inline std::vector<std::pair<Num, Num>> TesselateCubicBezier(
    // starting vertex
    Num x0, Num y0,
    // first control point
    Num x1, Num y1,
    // second control point
    Num x2, Num y2,
    // end point
    Num x3, Num y3,
    Num max_error_squared,
    int max_depth) {

  static_assert(std::is_arithmetic<Num>::value,
                "TesselateCubicBezier needs an integral or floating-point "
                "template argument.");

  std::vector<std::pair<Num, Num>> out;
  std::function<void(Num, Num, Num, Num, Num, Num, Num, Num, int)> Rec =
    [&out, max_error_squared, &Rec](Num x0, Num y0,
                                    Num x1, Num y1,
                                    Num x2, Num y2,
                                    Num x3, Num y3,
                                    int max_depth) {
      // Midpoint of the curve.
      // (As in the quadratic case, this evaluates at t=0.5. Note that
      // for a cubic, an S-curve can have its midpoint perfectly on the
      // straight line, which might prematurely end the recursion.)
      const Num mx = (x0 + (x1 * 3) + (x2 * 3) + x3) / 8;
      const Num my = (y0 + (y1 * 3) + (y2 * 3) + y3) / 8;

      // Midpoint of a straight line.
      const Num lx = (x0 + x3) / 2;
      const Num ly = (y0 + y3) / 2;

      // Error.
      const Num dx = lx - mx;
      const Num dy = ly - my;
      const Num error = (dx * dx) + (dy * dy);

      if (error > max_error_squared && max_depth > 0) {
        Rec(x0, y0,
            (x0 + x1) / 2, (y0 + y1) / 2,
            (x0 + (x1 * 2) + x2) / 4, (y0 + (y1 * 2) + y2) / 4,
            mx, my,
            max_depth - 1);
        Rec(mx, my,
            (x1 + (x2 * 2) + x3) / 4, (y1 + (y2 * 2) + y3) / 4,
            (x2 + x3) / 2, (y2 + y3) / 2,
            x3, y3,
            max_depth - 1);
      } else {
        // Otherwise, emit a straight line.
        out.emplace_back(x3, y3);
      }
    };

  Rec(x0, y0, x1, y1, x2, y2, x3, y3, max_depth);
  return out;
}


#endif
