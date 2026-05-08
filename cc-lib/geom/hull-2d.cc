
#include "hull-2d.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "yocto-math.h"

using vec2 = yocto::vec<double, 2>;

static std::string VecString(const vec2 &v) {
  return std::format("({:.17g}, {:.17g})", v.x, v.y);
}

template<typename T>
static inline int sgn(T val) {
  return (T(0) < val) - (val < T(0));
}

bool Hull2D::IsHullConvex(std::span<const vec2> vertices,
                          std::span<const int> polygon) {
  if (polygon.size() <= 3) return true;
  std::optional<int> s;
  for (int i = 0; i < (int)polygon.size(); i++) {
    const vec2 &p0 = vertices[polygon[i]];
    const vec2 &p1 = vertices[polygon[(i + 1) % polygon.size()]];
    const vec2 &p2 = vertices[polygon[(i + 2) % polygon.size()]];

    vec2 e1 = p1 - p0;
    vec2 e2 = p2 - p1;

    double cx = cross(e1, e2);
    if (std::abs(cx) < 1e-10) continue;
    int sign = sgn(cx);
    if (s.has_value() && s.value() != sign)
      return false;
    s = {sign};
  }
  return true;
}


std::vector<int> Hull2D::GiftWrap(std::span<const vec2> vertices) {
  constexpr bool VERBOSE = false;
  constexpr bool SELF_CHECK = false;
  CHECK(vertices.size() > 2);

  // Explicitly mark vertices as used to avoid reusing them. This may
  // not actually be necessary (I think the real issue was that I used
  // to always start with "next" being node cur+1, even if that was an
  // invalid choice) but it is pretty cheap and colinear/coincident
  // points can cause tests to behave in countergeometric ways.
  std::vector<bool> used(vertices.size(), false);

  // Find the starting point. This must be a point on the convex hull.
  // The leftmost bottommost point is one.
  const int start = [&]() {
      int besti = 0;
      for (int i = 1; i < vertices.size(); i++) {
        if ((vertices[i].y < vertices[besti].y) ||
            (vertices[i].y == vertices[besti].y &&
             vertices[i].x < vertices[besti].x)) {
          besti = i;
        }
      }
      return besti;
    }();

  if (VERBOSE) {
    Print("Start idx: {}\n", start);

    for (const vec2 &v : vertices) {
      Print("vec2{{" "{:.17g}, {:.17g}" "}}, ", v.x, v.y);
    }
  }

  const vec2 &vstart = vertices[start];

  if (VERBOSE) {
    Print("\n");
  }

  std::vector<int> hull;
  int cur = start;
  do {
    if (VERBOSE) {
      Print("Loop with cur={}\n", cur);
    }

    if (SELF_CHECK) {
      for (int a : hull) {
        if (a == cur) {
          Print(stderr, "About to add duplicate point {} to hull.\n"
                "Points so far:\n",
                cur);
          for (int i = 0; i < (int)vertices.size(); i++) {
            Print(stderr, "{}. vec2{{" "{:.17g}, {:.17g}" "}}\n",
                    i, vertices[i].x, vertices[i].y);
          }
          Print(stderr, "Hull so far:");
          for (int x : hull) {
            Print(stderr, " {}{}", x,
                  used[x] ? " used" : ARED(" not used??"));
          }
          Print(stderr, "\n");
          LOG(FATAL) << "Infinite loop!";
        }
      }
    }

    hull.push_back(cur);
    used[cur] = true;

    // We consider every other point, finding the one with
    // the smallest angle from the current point.

    // We need to choose a point that's not already used (unless
    // we're closing the loop). So start is a good choice, except
    // on the first step (because then the start-start edge is
    // degenerate).
    // int next = (cur == start) ? ((cur + 1) % vertices.size()) :
    // start;
    int next = -1;

    // First, find any point that's unused and not exactly the same as
    // the current point.
    for (int i = 0; i < vertices.size(); i++) {
      if (i != cur && (i == start || !used[i]) &&
          vertices[i] != vertices[cur]) {
        next = i;
        break;
      }
    }

    // We exhausted all of the nodes, so we must be done.
    if (next == -1) {
      if (VERBOSE) {
        Print(ACYAN("No more nodes.") "\n");
      }
      return hull;
    }

    for (int i = 0; i < vertices.size(); i++) {
      if (VERBOSE) {
        Print("Inner loop at i={} w/ next={}.\n",
              i, next);
      }
      // We need to consider the start point as a candidate
      // (which will always have been marked 'used') because
      // it's how we actually end. It prevents us from choosing
      // invalid points (that are not "to the left").
      if ((i == start || !used[i]) && i != cur && i != next) {
        const vec2 &vcur = vertices[cur];
        const vec2 &vnext = vertices[next];
        const vec2 &vi = vertices[i];

        const vec2 to_next = vnext - vcur;
        const vec2 to_i = vi - vcur;

        // Compare against the current candidate, using cross
        // product to find the "leftmost" one.
        double angle = yocto::cross(to_next, to_i);

        const bool is_strictly_left = angle < 0.0;

        bool take = is_strictly_left;

        if (VERBOSE) {
          Print("  Angle: {:.17g}. {} {}\n", angle,
                is_strictly_left ? " left" : "",
                take ? " take." : "");
        }

        if (take) {
          // However, if we get back to a point that's exactly equal
          // to start, we want to use the start index (for one thing,
          // so that this loop terminates).
          if (vi == vstart) {
            next = start;
          } else {
            next = i;
          }
        }
      }
    }

    cur = next;
  } while (cur != start);

  if (VERBOSE) {
    Print(ACYAN("Returned to start.") "\n");
  }
  return hull;
}

std::vector<int> Hull2D::GrahamScan(std::span<const vec2> vertices) {

  // Place the leftmost bottommost point first in the
  // working hull. This will be on the hull and it is the
  // reference point.
  const int ref = [&]() {
      int besti = 0;
      for (int i = 1; i < vertices.size(); i++) {
        if ((vertices[i].y < vertices[besti].y) ||
            (vertices[i].y == vertices[besti].y &&
             vertices[i].x < vertices[besti].x)) {
          besti = i;
        }
      }
      return besti;
    }();

  const vec2 &vref = vertices[ref];

  struct Point {
    int idx;
    double angle;
    double distsq;
  };
  // Get the remainder of the point indices.
  std::vector<Point> points;
  points.reserve(vertices.size() - 1);
  for (int i = 0; i < vertices.size(); i++) {
    if (i != ref) {
      const vec2 &v = vertices[i];
      const vec2 e = v - vref;
      points.emplace_back(Point{
          .idx = i,
          .angle = std::atan2(e.y, e.x),
          .distsq = length_squared(e),
        });
    }
  }

  // Sort the remaining points by their polar angle.
  std::sort(points.begin(),
            points.end(),
            [&](const Point &a, const Point &b) {
              if (a.angle == b.angle) {
                if (a.distsq == b.distsq) {
                  return a.idx < b.idx;
                } else {
                  return a.distsq < b.distsq;
                }
              } else {
                return a.angle < b.angle;
              }
            });

  // Remove collinear points now. We keep the farthest
  // one (and in the case of ties, break them using the
  // largest index).
  int new_size = 0;
  for (int i = 0; i < points.size(); i++) {
    // Skip a point if it has the same angle as the
    // next one; the last point in such a sequence is the
    // best one.
    if (i + 1 < points.size() &&
        points[i].angle == points[i + 1].angle) {
      continue;
    } else {
      points[new_size++] = points[i];
    }
  }
  points.resize(new_size);

  // Now we have the candidate set.
  // If it's trivial or degenerate, return those points.
  if (points.size() <= 2) {
    std::vector<int> hull = {ref};
    for (const Point &p : points) hull.push_back(p.idx);
    return hull;
  }

  // A proper set. Do the scan.
  std::vector<int> hull = {ref, points[0].idx, points[1].idx};

  // Taking vertex indices.
  auto CCW = [&vertices] (int a, int b, int c) {
      const vec2 &va = vertices[a];
      const vec2 &vb = vertices[b];
      const vec2 &vc = vertices[c];

      return cross(vb - va, vc - va) < 0.0;
    };

  for (int i = 2; i < points.size(); i++) {
    const Point &point = points[i];

    while (hull.size() >= 2 && CCW(hull[hull.size() - 2],
                                   hull[hull.size() - 1],
                                   point.idx)) {
      hull.pop_back();
    }

    hull.push_back(point.idx);
  }

  return hull;
}

// The QuickHull implementation below and its helper routines are based on code
// by Miguel Vieira (see LICENSES) although I have heavily modified it.
// Some changes:
//  - Uses yocto library for more stuff
//  - Uses vertex indices so it can be run directly on Polyhedron/Mesh2D.
//  - Fixes some bugs relating to exactly equal or colinear input points
//  - Some algorithmic improvements (e.g. left and right sets in the recursive
//    calls must be disjoint).

// Returns the index of the farthest point from segment (a, b).
// Requires that all points are to the left of the segment (a,b) (or colinear).
// (If you need it to handle both, you can just use fabs in the Dist function,
// but for quickhull our candidate set always lies on the left of the edge.)
static int GetFarthest(const vec2 &a, const vec2 &b,
                       std::span<const vec2> v,
                       std::span<const int> pts) {
  CHECK(!pts.empty());
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  auto Dist = [&](const vec2 &p) -> double {
      return dx * (a.y - p.y) - dy * (a.x - p.x);
    };

  int best_idx = pts[0];
  double best_dist = Dist(v[best_idx]);

  for (int i = 1; i < pts.size(); i++) {
    int p = pts[i];
    double d = Dist(v[p]);
    if (d < best_dist) {
      best_idx = p;
      best_dist = d;
    }
  }

  return best_idx;
}

// The z-value of the cross product of segments
// (a, b) and (a, c). Positive means c is ccw (to the left)
// from (a, b), negative cw. Zero means it's colinear.
static inline double CounterClockwise(
    const vec2 &a, const vec2 &b, const vec2 &c) {
  return yocto::cross(b - a, c - a);
}


// Recursive call of the quickhull algorithm.
static void QuickHullRec(std::span<const vec2> vertices,
                         std::span<const int> pts,
                         int a, int b,
                         std::vector<int> *hull) {
  static constexpr bool SELF_CHECK = false;
  static constexpr int VERBOSE = 0;

  if (VERBOSE == 1) {
    Print("QuickHullRec({} vs, {} pts, {} ({}), {} ({}))\n",
          vertices.size(), pts.size(),
          a, VecString(vertices[a]),
          b, VecString(vertices[b]));
  } else if (VERBOSE > 0) {
    Print("QuickHullRec({} vs, {} pts, {} ({}), {} ({})):",
          vertices.size(), pts.size(),
          a, VecString(vertices[a]),
          b, VecString(vertices[b]));
    for (int p : pts) {
      if (p == a || p == b) {
        Print(" " ANSI_BG(0, 0, 128) "{}" ANSI_RESET,
              VecString(vertices[p]));
      } else {
        Print(" {}", VecString(vertices[p]));
      }
    }
    Print("\n");
  }
  if (pts.empty()) {
    return;
  }

  if (SELF_CHECK) {
    for (int x : pts) {
      CHECK(x != a && x != b) << x << " candidate points should "
        "not include the endpoints of the recursed upon segment.";
      for (int y : *hull) {
        CHECK(x != y) << x << " is already in the hull!";
      }
    }
  }

  const vec2 &aa = vertices[a];
  const vec2 &bb = vertices[b];

  if (SELF_CHECK) {
    for (int x : pts) {
      const vec2 &xx = vertices[x];
      CHECK(aa == xx || bb == xx ||
            CounterClockwise(aa, bb, xx) > 0.0);
    }
  }

  int f = GetFarthest(aa, bb, vertices, pts);
  const vec2 &ff = vertices[f];
  if (VERBOSE) Print("Farthest is {} ({})\n", f, VecString(ff));

  // CHECK(CounterClockwise(aa, ff, aa) <= 0.0);
  // CHECK(CounterClockwise(aa, ff, ff) <= 0.0);

  // Collect points to the left of segment (a, f) and to the left
  // of segment f, b (which we call "right"). A point cannot be in both
  // sets, because that would require it to be farther away than f, but f
  // is maximal.
  //
  //             f
  //       left / \   right
  //      set  /   \   set
  //          a     b
  //
  std::vector<int> left, right;
  for (int i : pts) {
    const vec2 &ii = vertices[i];
    // In the presence of exact duplicates for one of the endpoints,
    // we need to filter them out here or else we can end up in
    // infinite loops. Removing duplicates does not affect the hull.
    if (ii == aa || ii == bb || ii == ff) continue;

    if (CounterClockwise(aa, ff, ii) > 0.0) {
      left.push_back(i);
    } else if (CounterClockwise(ff, bb, ii) > 0.0) {
      right.push_back(i);
    }
  }
  if (VERBOSE) Print("{} left, {} right vertices.\n",
                     left.size(), right.size());
  QuickHullRec(vertices, left, a, f, hull);

  // Add f to the hull
  hull->push_back(f);

  if (VERBOSE) Print("{} right vertices.\n", right.size());
  QuickHullRec(vertices, right, f, b, hull);
}

// QuickHull algorithm.
// https://en.wikipedia.org/wiki/QuickHull
std::vector<int> Hull2D::QuickHull(std::span<const vec2> vertices) {
  std::vector<int> hull;
  if (vertices.empty()) return {};
  if (vertices.size() == 1) return {0};
  if (vertices.size() == 2) return {0, 1};

  // Returns true if a is lexicographically before b.
  auto LeftOf = [](const vec2 &a, const vec2 &b) -> bool {
      return (a.x < b.x || (a.x == b.x && a.y < b.y));
    };


  // Get the leftmost (a) and rightmost (b) points.
  int a = 0, b = 0;
  for (int i = 1; i < (int)vertices.size(); i++) {
    if (LeftOf(vertices[i], vertices[a])) a = i;
    if (LeftOf(vertices[b], vertices[i])) b = i;
  }

  if (a == b) [[unlikely]] {
    Print(stderr, "Quickhull failure on:\n");
    for (const vec2 &v : vertices) {
      Print(stderr, "  {{" "{:.17g}, {:.17g}" "}},\n",
            v.x, v.y);
    }
    LOG(FATAL) << "Should not be possible!";
  }

  // Split the points on either side of segment (a, b).
  std::vector<int> left, right;
  for (int i = 0; i < (int)vertices.size(); i++) {
    if (i != a && i != b) {
      double side = CounterClockwise(vertices[a], vertices[b], vertices[i]);
      if (side > 0.0) left.push_back(i);
      else if (side < 0.0) right.push_back(i);
      // Ignore if colinear.
    }
  }

  // Be careful to add points to the hull
  // in the correct order. Add our leftmost point.
  hull.push_back(a);

  // Add hull points from the left (top)
  QuickHullRec(vertices, left, a, b, &hull);

  // Add our rightmost point
  hull.push_back(b);

  // Add hull points from the right (bottom)
  QuickHullRec(vertices, right, b, a, &hull);

  return hull;
}
