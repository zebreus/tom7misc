// Generate lines using some classic graphics algorithms.
// Templated on integer/float types since sometimes it is helpful
// to use lower-precision types in embedded applications.
//
// Note: Tesselation of beziers moved to geom/bezier.h.

#ifndef _CC_LIB_LINES_H
#define _CC_LIB_LINES_H

#include <algorithm>
#include <cassert>
#include <tuple>
#include <utility>
#include <cmath>
#include <type_traits>
#include <optional>

// Generate lines with Bresenham's algorithm. Use like this:
//
// /* Draw a line from (3, 4) to (-7, 8). */
// for (const std::pair<int, int> point : Line<int>{3, 4, -7, 8}) {
//   int x = point.first, y = point.second;
//   drawpixel(x, y);
// }
//
// C++17:
// for (auto [x, y] : Line<int>{3, 4, -7, 8})
//   drawpixel(x, y);
template<class Int>
class Line {
 public:
  static_assert(std::is_integral<Int>::value, "Line<T> requires integral T.");
  Line(Int x0, Int y0, Int x1, Int y1);
  static Line<Int> Empty();

  // This iterator is only designed for ranged-for loops; the operators
  // may have counter-intuitive behavior.
  struct iterator {
    std::pair<Int, Int> operator *() const { return {x, y}; }
    void operator ++();
    bool operator !=(const iterator &other) const;
   private:
    iterator(const Line &parent) : parent(parent) {}
    const Line &parent;
    Int x = 0, y = 0;
    Int frac = 0;
    friend class Line;
  };
  iterator begin() const;
  iterator end() const;

  // TODO: Fix this!
  #if 0
  // Inclusive clip rectangle.
  static Line<Int> ClippedLine(Int x0, Int y0, Int x1, Int y1,
                               Int xmin, Int ymin,
                               Int xmax, Int ymax);
  #endif

 private:
  Line(Int x0, Int y0, Int x1, Int y1,
       Int dx, Int dy, Int stepx, Int stepy,
       Int start_frac);
  // All members morally const.
  const Int x0, y0, x1, y1;
  Int dx, dy;
  Int stepx, stepy;
  Int start_frac;
};

// Anti-aliased line using Wu's algorithm. The pixel to plot is
// accompanied by a brightness fraction in [0, 1].
// This is not suited for an iterator-based interface since it treats
// the endpoints separately, and draws two pixels per iteration.
// Not possible to return early.
//
// /* Draw a line from (1.0, 3.1) to (-7, 8.5), using single-precision
//    floats (deduced from args) and int output. */
// LineAA::Draw<int>(1.0f, 3.1f, -7.0f, 8.5f,
//                   [](int x, int y, float f) {
//                       blendpixel(x, y, f * 255.0f);
//                   });
class LineAA {
public:
  template<class Int, class Float, class Fn>
  static void Draw(Float x0, Float y0, Float x1, Float y1, Fn drawpixel);
};


// Compute the point of intersection between two line segments
// (given as their endpoints), or return nullopt if they do
// not intersect.
//
// (Note that even for double inputs, this does some float
// calculations and returns float. TODO: Could make it
// use double (or long double) if inputs are that type, with
// significant added trickery.)
// Ported from sml-lib.
template<class Num = float>
inline std::optional<std::pair<float, float>> LineIntersection(
    // First segment
    Num p0x, Num p0y, Num p1x, Num p1y,
    // Second segment
    Num p2x, Num p2y, Num p3x, Num p3y);

// Return the closest point (to x,y) on the given line segment.
// It may be one of the endpoints.
inline std::pair<float, float>
ClosestPointOnSegment(
    // Line segment
    float x0, float y0, float x1, float y1,
    // Point to test
    float x, float y);

// Return the minimum distance between the point and the line segment.
inline float PointLineDistance(
    // Line segment
    float x0, float y0, float x1, float y1,
    // Point to test
    float x, float y);

// Same, but for a line that's known to be horizontal.
inline float PointHorizLineDistance(
    // Line segment
    float x0, float y0, float x1, /* y1 = y0 */
    // Point to test
    float x, float y);

// ... and vertical.
inline float PointVertLineDistance(
    // Line segment
    float x0, float y0, /* x1 = x0 */ float y1,
    // Point to test
    float x, float y);

template<class Num = float>
std::pair<Num, Num> ReflectPointAboutLine(
    // Line segment
    Num x0, Num y0,
    Num x1, Num y1,
    // Point to reflect
    Num x, Num y);

template<class Num = float>
inline std::optional<std::tuple<Num, Num, Num, Num>>
ClipLineToRectangle(Num x0, Num y0, Num x1, Num y1,
                    Num xmin, Num ymin, Num xmax, Num ymax) {
  // This would compile with integers, but the integer division
  // is problematic, and the result would often not describe
  // the same line.
  static_assert(std::is_floating_point<Num>::value,
                "ClipLineToRectangle needs a floating-point "
                "template argument.");

  // via "Another Simple but Faster Method for 2D Line Clipping",
  // Matthes & Drakopoulos 2019
  if (x0 < xmin && x1 < xmin) return std::nullopt;
  if (x0 > xmax && x1 > xmax) return std::nullopt;
  if (y0 < ymin && y1 < ymin) return std::nullopt;
  if (y0 > ymax && y1 > ymax) return std::nullopt;

  Num x[2] = {x0, x1};
  Num y[2] = {y0, y1};

  for (int i = 0; i < 2; i++) {
    if (x[i] < xmin) {
      x[i] = xmin;
      y[i] = ((y1 - y0) / (x1 - x0)) * (xmin - x0) + y0;
    } else if (x[i] > xmax) {
      x[i] = xmax;
      y[i] = ((y1 - y0) / (x1 - x0)) * (xmax - x0) + y0;
    }

    if (y[i] < ymin) {
      y[i] = ymin;
      x[i] = ((x1 - x0) / (y1 - y0)) * (ymin - y0) + x0;
    } else if (y[i] > ymax) {
      y[i] = ymax;
      x[i] = ((x1 - x0) / (y1 - y0)) * (ymax - y0) + x0;
    }
  }

  if (!(x[0] < xmin && x[1] < xmin) && !(x[0] > xmax && x[1] > xmax)) {
    return std::make_tuple(x[0], y[0], x[1], y[1]);
  } else {
    return std::nullopt;
  }
}


// Template implementations follow.

template<class Int>
Line<Int>::Line(Int x0, Int y0, Int x1, Int y1) :
  x0(x0), y0(y0), x1(x1), y1(y1) {
  dy = y1 - y0;
  dx = x1 - x0;

  if (dy < 0) {
    dy = -dy;
    stepy = -1;
  } else {
    stepy = 1;
  }

  if (dx < 0) {
    dx = -dx;
    stepx = -1;
  } else {
    stepx = 1;
  }

  dy <<= 1;
  dx <<= 1;

  if (dx > dy) {
    start_frac = dy - (dx >> 1);
  } else {
    start_frac = dx - (dy >> 1);
  }
}

template<class Int>
typename Line<Int>::iterator Line<Int>::begin() const {
  iterator it{*this};
  it.x = x0;
  it.y = y0;
  it.frac = start_frac;
  return it;
}

template<class Int>
typename Line<Int>::iterator Line<Int>::end() const {
  iterator it{*this};

  // One step beyond the end point, so that the line includes
  // (x1, y1).
  if (dx > dy) {
    it.x = x1 + stepx;
  } else {
    it.y = y1 + stepy;
  }
  return it;
}

template<class Int>
Line<Int>::Line(Int x0, Int y0, Int x1, Int y1,
                Int dx, Int dy, Int stepx, Int stepy,
                Int start_frac) : x0(x0), y0(y0),
                                  x1(x1), y1(y1),
                                  dx(dx), dy(dy),
                                  stepx(stepx), stepy(stepy),
                                  start_frac(start_frac) {
}

template<class Int>
Line<Int> Line<Int>::Empty() {
  // Any line such that .begin() == .end()
  Line<int> empty(
    // x0, y0, x1, y1 (we passed the end by one pixel)
    1, 1, 0, 0,
    // dx, dy, stepx, stepy
    1, 1, 1, 1,
    // start_frac
    0);

  assert(!(empty.begin() != empty.end()));
  return empty;
}

template<class Int>
bool Line<Int>::iterator::operator !=(const iterator &other) const {
  return parent.dx > parent.dy ?
    x != other.x :
    y != other.y;
}

template<class Int>
void Line<Int>::iterator::operator ++() {
  if (parent.dx > parent.dy) {
    if (frac >= 0) {
      y += parent.stepy;
      frac -= parent.dx;
    }
    x += parent.stepx;
    frac += parent.dy;
  } else {
    if (frac >= 0) {
      x += parent.stepx;
      frac -= parent.dy;
    }
    y += parent.stepy;
    frac += parent.dx;
  }
}

// TODO: There may be some problem with the endpoint drawing;
// there seem to be discontinuities when drawing a polyline.
// (Could also be a problem with ImageRGBA::BlendPixel?)
template<class Int, class Float, class Fn>
void LineAA::Draw(Float x0, Float y0, Float x1, Float y1, Fn drawpixel) {
  static_assert(std::is_integral<Int>::value,
                "LineAA<T,F> requires integral T.");
  static_assert(std::is_floating_point<Float>::value,
                "LineAA<T,F> requires floating-point F.");

  // floor and round are each overloaded on float and double.
  auto ipart = [](Float x) -> Int { return Int(std::floor(x)); };
  auto round = [](Float x) -> Float { return std::round(x); };
  auto fpart = [](Float x) -> Float { return x - std::floor(x); };
  auto rfpart = [fpart](Float x) -> Float { return Float(1.0f) - fpart(x); };

  const bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
  if (steep) {
    std::swap(x0,y0);
    std::swap(x1,y1);
  }
  if (x0 > x1) {
    std::swap(x0,x1);
    std::swap(y0,y1);
  }

  const Float dx = x1 - x0;
  const Float dy = y1 - y0;
  const Float gradient = (dx == 0) ? 1 : dy / dx;

  Int xpx11;
  Float intery;
  {
    const Float xend = round(x0);
    const Float yend = y0 + gradient * (xend - x0);
    const Float xgap = rfpart(x0 + 0.5);
    xpx11 = Int(xend);
    const Int ypx11 = ipart(yend);
    if (steep) {
      drawpixel(ypx11, xpx11, rfpart(yend) * xgap);
      drawpixel(ypx11 + 1, xpx11, fpart(yend) * xgap);
    } else {
      drawpixel(xpx11, ypx11, rfpart(yend) * xgap);
      drawpixel(xpx11, ypx11 + 1, fpart(yend) * xgap);
    }
    intery = yend + gradient;
  }

  Int xpx12;
  {
    const Float xend = round(x1);
    const Float yend = y1 + gradient * (xend - x1);
    const Float xgap = rfpart(x1 + 0.5);
    xpx12 = Int(xend);
    const Int ypx12 = ipart(yend);
    if (steep) {
      drawpixel(ypx12, xpx12, rfpart(yend) * xgap);
      drawpixel(ypx12 + 1, xpx12, fpart(yend) * xgap);
    } else {
      drawpixel(xpx12, ypx12, rfpart(yend) * xgap);
      drawpixel(xpx12, ypx12 + 1, fpart(yend) * xgap);
    }
  }

  if (steep) {
    for (Int x = xpx11 + 1; x < xpx12; x++) {
      drawpixel(ipart(intery), x, rfpart(intery));
      drawpixel(ipart(intery) + 1, x, fpart(intery));
      intery += gradient;
    }
  } else {
    for (Int x = xpx11 + 1; x < xpx12; x++) {
      drawpixel(x, ipart(intery), rfpart(intery));
      drawpixel(x, ipart(intery) + 1, fpart(intery));
      intery += gradient;
    }
  }
}

template<class Num>
std::optional<std::pair<float, float>> LineIntersection(
    // First segment
    Num p0x, Num p0y, Num p1x, Num p1y,
    // Second segment
    Num p2x, Num p2y, Num p3x, Num p3y) {

  const auto s1x = p1x - p0x;
  const auto s1y = p1y - p0y;
  const auto s2x = p3x - p2x;
  const auto s2y = p3y - p2y;

  const auto l1 = p0x - p2x;
  const auto l2 = p0y - p2y;
  const float denom = s1x * s2y - s2x * s1y;

  const float s = (s1x * l2 - s1y * l1) / denom;

  if (s >= 0.0f && s <= 1.0f) {
    const float t = (s2x * l2 - s2y * l1) / denom;

    if (t >= 0.0f && t <= 1.0f) {
      return {{(float)p0x + (t * s1x),
               (float)p0y + (t * s1y)}};
    }
  }
  return std::nullopt;
}


inline std::pair<float, float>
ClosestPointOnSegment(
    // Line segment
    float x0, float y0, float x1, float y1,
    // Point to test
    float x, float y) {
  auto SqDist = [](float x0, float y0,
                   float x1, float y1) {
      const float dx = x1 - x0;
      const float dy = y1 - y0;
      return dx * dx + dy * dy;
    };

  const float sqlen = SqDist(x0, y0, x1, y1);
  if (sqlen == 0.0) {
    // Degenerate case where line segment is just a point,
    // so there is only one choice.
    return {x0, y0};
  }

  const float tf = ((x - x0) * (x1 - x0) + (y - y0) * (y1 - y0)) / sqlen;
  // Make sure it is on the segment.
  const float t = std::max(0.0f, std::min(1.0f, tf));
  // Closest point, which is on the segment.

  const float xx = x0 + t * (x1 - x0);
  const float yy = y0 + t * (y1 - y0);
  return {xx, yy};
}

// Return the minimum distance between the point and the line segment.
inline float PointLineDistance(
    // Line segment
    float x0, float y0, float x1, float y1,
    // Point to test
    float x, float y) {

  const auto [xx, yy] = ClosestPointOnSegment(x0, y0, x1, y1, x, y);
  const float dx = x - xx;
  const float dy = y - yy;
  return sqrtf(dx * dx + dy * dy);
}

// Same, but for a line that's known to be horizontal.
inline float PointHorizLineDistance(
    // Line segment
    float x0, float y0, float x1, /* y1 = y0 */
    // Point to test
    float x, float y) {
  // Put in order so that x0 < x1.
  if (x0 > x1) std::swap(x0, x1);
  const float dy = y0 - y;
  if (x <= x0) {
    // Distance is to left vertex.
    const float dx = x0 - x;
    return sqrtf(dx * dx + dy * dy);
  } else if (x >= x1) {
    // To right vertex.
    const float dx = x1 - x;
    return sqrtf(dx * dx + dy * dy);
  } else {
    // Perpendicular to segment itself.
    return fabsf(dy);
  }
}

// ... and vertical.
inline float PointVertLineDistance(
    // Line segment
    float x0, float y0, /* x1 = x0 */ float y1,
    // Point to test
    float x, float y) {
  // Put in order so that y0 < y1.
  if (y0 > y1) std::swap(y0, y1);
  const float dx = x0 - x;
  if (y <= y0) {
    // Distance is to top vertex.
    const float dy = y0 - y;
    return sqrtf(dx * dx + dy * dy);
  } else if (y >= y1) {
    // To bottom corner.
    const float dy = y1 - y;
    return sqrtf(dx * dx + dy * dy);
  } else {
    // Perpendicular to segment itself.
    return fabsf(dx);
  }
}


template<class Num>
std::pair<Num, Num> ReflectPointAboutLine(
    // Line segment
    Num x0, Num y0,
    Num x1, Num y1,
    // Point to reflect
    Num x, Num y) {

  Num dx = x1 - x0;
  Num dy = y1 - y0;
  Num dxs = dx * dx;
  Num dys = dy * dy;
  Num denom = dxs + dys;
  Num a = (dxs - dys) / denom;
  Num b = Num(2) * dx * dy / denom;

  Num x2  = a * (x - x0) + b * (y - y0) + x0;
  Num y2  = b * (x - x0) - a * (y - y0) + y0;

  return std::make_pair(x2, y2);
}


#endif
