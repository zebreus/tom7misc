/*
 This file only:
 Poly2Tri Copyright (c) 2009-2018, Poly2Tri Contributors
 https://github.com/jhasse/poly2tri

 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

 * Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the
   distribution.

 * Neither the name of Poly2Tri nor the names of its contributors
   may be used to endorse or promote products derived from this
   software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 Additional contributions by Tom 7 in 2026. Consider my modifications
 and additions to be available under this same permissive license.

 Local changes:
  - Put it all in one file
  - Cleaned up for local style (CHECK macros instead of exceptions, etc.)
  - Use unique_ptr instead of manual deletion
  - Remove some unused stuff.
  - Added the actual triangulation and polygonization routines at
    the bottom.
*/

#include "polygonization.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <list>
#include <memory>
#include <numbers>
#include <queue>
#include <span>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "geom/polygons.h"
#include "yocto-math.h"

namespace {

using vec2 = Polygonization::vec2;

struct Edge;

struct Point {

  double x = 0.0, y = 0.0;

  Point() {}

  /// The edges this point constitutes an upper ending point
  std::vector<Edge*> edge_list;

  /// Construct using coordinates.
  Point(double x, double y) : x(x), y(y) {}

  /// Set this point to all zeros.
  void set_zero() {
    x = 0.0;
    y = 0.0;
  }

  /// Set this point to some specified coordinates.
  void set(double x_, double y_) {
    x = x_;
    y = y_;
  }

  /// Negate this point.
  Point operator-() const {
    Point v;
    v.set(-x, -y);
    return v;
  }

  /// Add a point to this point.
  void operator+=(const Point &v) {
    x += v.x;
    y += v.y;
  }

  /// Subtract a point from this point.
  void operator-=(const Point &v) {
    x -= v.x;
    y -= v.y;
  }

  /// Multiply this point by a scalar.
  void operator*=(double a) {
    x *= a;
    y *= a;
  }

  /// Get the length of this point (the norm).
  double Length() const { return sqrt(x * x + y * y); }

  /// Convert this point into a unit point. Returns the Length.
  double Normalize() {
    const double len = Length();
    x /= len;
    y /= len;
    return len;
  }
};

// Represents a simple polygon's edge
struct Edge {
  Point *p = nullptr, *q = nullptr;

  /// Constructor
  Edge(Point &p1, Point &p2) : p(&p1), q(&p2) {
    if (p1.y > p2.y) {
      q = &p1;
      p = &p2;
    } else if (p1.y == p2.y) {
      if (p1.x > p2.x) {
        q = &p1;
        p = &p2;
      } else if (p1.x == p2.x) {
        // Repeat points
        LOG(FATAL) << "Edge::Edge: p1 == p2";
      }
    }

    q->edge_list.push_back(this);
  }
};

// Triangle-based data structures are know to have better performance
// than quad-edge structures See: J. Shewchuk, "Triangle: Engineering
// a 2D Quality Mesh Generator and Delaunay Triangulator"
// "Triangulations in CGAL"
class Triangle {
 public:
  /// Constructor
  Triangle(Point &a, Point &b, Point &c);

  /// Flags to determine if an edge is a Constrained edge
  bool constrained_edge[3];
  /// Flags to determine if an edge is a Delauney edge
  bool delaunay_edge[3];

  Point *GetPoint(int index) { return points_[index]; }
  Point *PointCW(const Point &point);
  Point *PointCCW(const Point &point);
  Point *OppositePoint(Triangle &t, const Point &p);

  Triangle *GetNeighbor(int index) { return neighbors_[index]; }
  void MarkNeighbor(Point *p1, Point *p2, Triangle *t);
  void MarkNeighbor(Triangle &t);

  void MarkConstrainedEdge(int index) {
    constrained_edge[index] = true;
  }

  void MarkConstrainedEdge(Edge &edge) {
    MarkConstrainedEdge(edge.p, edge.q);
  }

  void MarkConstrainedEdge(Point *p, Point *q) {
    if ((q == points_[0] && p == points_[1]) ||
        (q == points_[1] && p == points_[0])) {
      constrained_edge[2] = true;
    } else if ((q == points_[0] && p == points_[2]) ||
               (q == points_[2] && p == points_[0])) {
      constrained_edge[1] = true;
    } else if ((q == points_[1] && p == points_[2]) ||
               (q == points_[2] && p == points_[1])) {
      constrained_edge[0] = true;
    }
  }

  int Index(const Point *p);
  int EdgeIndex(const Point *p1, const Point *p2);

  Triangle *NeighborAcross(const Point &point);
  Triangle *NeighborCW(const Point &point);
  Triangle *NeighborCCW(const Point &point);
  bool GetConstrainedEdgeCCW(const Point &p);
  bool GetConstrainedEdgeCW(const Point &p);
  void SetConstrainedEdgeCCW(const Point &p, bool ce);
  void SetConstrainedEdgeCW(const Point &p, bool ce);
  bool GetDelunayEdgeCCW(const Point &p);
  bool GetDelunayEdgeCW(const Point &p);
  void SetDelunayEdgeCCW(const Point &p, bool e);
  void SetDelunayEdgeCW(const Point &p, bool e);

  bool Contains(const Point *p) {
    return p == points_[0] || p == points_[1] || p == points_[2];
  }

  bool Contains(const Edge &e) {
    return Contains(e.p) && Contains(e.q);
  }

  bool Contains(const Point *p, const Point *q) {
    return Contains(p) && Contains(q);
  }

  void Legalize(Point &point);
  void Legalize(Point &opoint, Point &npoint);
  /**
   * Clears all references to all other triangles and points
   */
  void Clear();
  void ClearNeighbor(const Triangle *triangle);
  void ClearNeighbors();
  void ClearDelunayEdges();

  bool IsInterior() const { return interior_; }
  void SetInterior(bool b) { interior_ = b; }

  bool CircumcicleContains(const Point &) const;

 private:

  bool IsCounterClockwise() const;

  /// Triangle points
  Point *points_[3];
  /// Neighbor list
  Triangle *neighbors_[3];

  /// Has this triangle been marked as an interior triangle?
  bool interior_;
};


constexpr double PI_3div4 = 3 * std::numbers::pi / 4;
constexpr double PI_div2 = std::numbers::pi * 0.5;
constexpr double EPSILON = 1.0e-12;

enum Orientation { CW, CCW, COLINEAR };

/**
 * Forumla to calculate signed area<br>
 * Positive if CCW<br>
 * Negative if CW<br>
 * 0 if colinear<br>
 * <pre>
 * A[P1,P2,P3]  =  (x1*y2 - y1*x2) + (x2*y3 - y2*x3) + (x3*y1 - y3*x1)
 *              =  (x1-x3)*(y2-y3) - (y1-y3)*(x2-x3)
 * </pre>
 */
Orientation Orient2d(const Point &pa, const Point &pb, const Point &pc) {
  double detleft = (pa.x - pc.x) * (pb.y - pc.y);
  double detright = (pa.y - pc.y) * (pb.x - pc.x);
  double val = detleft - detright;

  // Using a tolerance here fails on concave-by-subepsilon boundaries
  //   if (val > -EPSILON && val < EPSILON) {
  // Using == on double makes -Wfloat-equal warnings yell at us
  if (std::fpclassify(val) == FP_ZERO) {
    return COLINEAR;
  } else if (val > 0) {
    return CCW;
  }
  return CW;
}

bool InScanArea(const Point &pa, const Point &pb, const Point &pc,
                const Point &pd) {
  double oadb = (pa.x - pb.x) * (pd.y - pb.y) - (pd.x - pb.x) * (pa.y - pb.y);
  if (oadb >= -EPSILON) {
    return false;
  }

  double oadc = (pa.x - pc.x)*(pd.y - pc.y) - (pd.x - pc.x)*(pa.y - pc.y);
  if (oadc <= EPSILON) {
    return false;
  }
  return true;
}


inline bool cmp(const Point *a, const Point *b) {
  if (a->y < b->y) {
    return true;
  } else if (a->y == b->y) {
    // Make sure q is point with greater x value
    if (a->x < b->x) {
      return true;
    }
  }
  return false;
}

inline bool operator==(const Point &a, const Point &b) {
  return a.x == b.x && a.y == b.y;
}

Triangle::Triangle(Point &a, Point &b, Point &c) {
  points_[0] = &a;
  points_[1] = &b;
  points_[2] = &c;
  neighbors_[0] = nullptr;
  neighbors_[1] = nullptr;
  neighbors_[2] = nullptr;
  constrained_edge[0] = constrained_edge[1] = constrained_edge[2] = false;
  delaunay_edge[0] = delaunay_edge[1] = delaunay_edge[2] = false;
  interior_ = false;
}

// Update neighbor pointers
void Triangle::MarkNeighbor(Point *p1, Point *p2, Triangle *t) {
  if ((p1 == points_[2] && p2 == points_[1]) ||
      (p1 == points_[1] && p2 == points_[2])) {
    neighbors_[0] = t;
  } else if ((p1 == points_[0] && p2 == points_[2]) ||
             (p1 == points_[2] && p2 == points_[0])) {
    neighbors_[1] = t;
  } else if ((p1 == points_[0] && p2 == points_[1]) ||
             (p1 == points_[1] && p2 == points_[0])) {
    neighbors_[2] = t;
  } else {
    LOG(FATAL) << "Bug";
  }
}

// Exhaustive search to update neighbor pointers
void Triangle::MarkNeighbor(Triangle &t) {
  if (t.Contains(points_[1], points_[2])) {
    neighbors_[0] = &t;
    t.MarkNeighbor(points_[1], points_[2], this);
  } else if (t.Contains(points_[0], points_[2])) {
    neighbors_[1] = &t;
    t.MarkNeighbor(points_[0], points_[2], this);
  } else if (t.Contains(points_[0], points_[1])) {
    neighbors_[2] = &t;
    t.MarkNeighbor(points_[0], points_[1], this);
  }
}

// Clears all references to all other triangles and points
void Triangle::Clear() {
  for (Triangle *neighbor : neighbors_) {
    if (neighbor != nullptr) {
      neighbor->ClearNeighbor(this);
    }
  }
  ClearNeighbors();
  points_[0] = points_[1] = points_[2] = nullptr;
}

void Triangle::ClearNeighbor(const Triangle *triangle) {
  if (neighbors_[0] == triangle) {
    neighbors_[0] = nullptr;
  } else if (neighbors_[1] == triangle) {
    neighbors_[1] = nullptr;
  } else {
    neighbors_[2] = nullptr;
  }
}

void Triangle::ClearNeighbors() {
  neighbors_[0] = nullptr;
  neighbors_[1] = nullptr;
  neighbors_[2] = nullptr;
}

void Triangle::ClearDelunayEdges() {
  delaunay_edge[0] = delaunay_edge[1] = delaunay_edge[2] = false;
}

Point *Triangle::OppositePoint(Triangle &t, const Point &p) {
  Point *cw = t.PointCW(p);
  return PointCW(*cw);
}

// Legalize triangle by rotating clockwise around point(0)
void Triangle::Legalize(Point &point) {
  points_[1] = points_[0];
  points_[0] = points_[2];
  points_[2] = &point;
}

// Legalize triangle by rotating clockwise around opoint
void Triangle::Legalize(Point &opoint, Point &npoint) {
  if (&opoint == points_[0]) {
    points_[1] = points_[0];
    points_[0] = points_[2];
    points_[2] = &npoint;
  } else if (&opoint == points_[1]) {
    points_[2] = points_[1];
    points_[1] = points_[0];
    points_[0] = &npoint;
  } else if (&opoint == points_[2]) {
    points_[0] = points_[2];
    points_[2] = points_[1];
    points_[1] = &npoint;
  } else {
    LOG(FATAL) << "Bug";
  }
}

int Triangle::Index(const Point *p) {
  if (p == points_[0]) {
    return 0;
  } else if (p == points_[1]) {
    return 1;
  } else if (p == points_[2]) {
    return 2;
  }
  LOG(FATAL) << "Bug";
  return -1;
}

int Triangle::EdgeIndex(const Point *p1, const Point *p2) {
  if (points_[0] == p1) {
    if (points_[1] == p2) {
      return 2;
    } else if (points_[2] == p2) {
      return 1;
    }
  } else if (points_[1] == p1) {
    if (points_[2] == p2) {
      return 0;
    } else if (points_[0] == p2) {
      return 2;
    }
  } else if (points_[2] == p1) {
    if (points_[0] == p2) {
      return 1;
    } else if (points_[1] == p2) {
      return 0;
    }
  }
  return -1;
}

// The point counter-clockwise to given point
Point *Triangle::PointCW(const Point &point) {
  if (&point == points_[0]) {
    return points_[2];
  } else if (&point == points_[1]) {
    return points_[0];
  } else if (&point == points_[2]) {
    return points_[1];
  }
  LOG(FATAL) << "Bug";
  return nullptr;
}

// The point counter-clockwise to given point
Point *Triangle::PointCCW(const Point &point) {
  if (&point == points_[0]) {
    return points_[1];
  } else if (&point == points_[1]) {
    return points_[2];
  } else if (&point == points_[2]) {
    return points_[0];
  }
  LOG(FATAL) << "Bug";
  return nullptr;
}

// The neighbor across to given point
Triangle *Triangle::NeighborAcross(const Point &point) {
  if (&point == points_[0]) {
    return neighbors_[0];
  } else if (&point == points_[1]) {
    return neighbors_[1];
  }
  return neighbors_[2];
}

// The neighbor clockwise to given point
Triangle *Triangle::NeighborCW(const Point &point) {
  if (&point == points_[0]) {
    return neighbors_[1];
  } else if (&point == points_[1]) {
    return neighbors_[2];
  }
  return neighbors_[0];
}

// The neighbor counter-clockwise to given point
Triangle *Triangle::NeighborCCW(const Point &point) {
  if (&point == points_[0]) {
    return neighbors_[2];
  } else if (&point == points_[1]) {
    return neighbors_[0];
  }
  return neighbors_[1];
}

bool Triangle::GetConstrainedEdgeCCW(const Point &p) {
  if (&p == points_[0]) {
    return constrained_edge[2];
  } else if (&p == points_[1]) {
    return constrained_edge[0];
  }
  return constrained_edge[1];
}

bool Triangle::GetConstrainedEdgeCW(const Point &p) {
  if (&p == points_[0]) {
    return constrained_edge[1];
  } else if (&p == points_[1]) {
    return constrained_edge[2];
  }
  return constrained_edge[0];
}

void Triangle::SetConstrainedEdgeCCW(const Point &p, bool ce) {
  if (&p == points_[0]) {
    constrained_edge[2] = ce;
  } else if (&p == points_[1]) {
    constrained_edge[0] = ce;
  } else {
    constrained_edge[1] = ce;
  }
}

void Triangle::SetConstrainedEdgeCW(const Point &p, bool ce) {
  if (&p == points_[0]) {
    constrained_edge[1] = ce;
  } else if (&p == points_[1]) {
    constrained_edge[2] = ce;
  } else {
    constrained_edge[0] = ce;
  }
}

bool Triangle::GetDelunayEdgeCCW(const Point &p) {
  if (&p == points_[0]) {
    return delaunay_edge[2];
  } else if (&p == points_[1]) {
    return delaunay_edge[0];
  }
  return delaunay_edge[1];
}

bool Triangle::GetDelunayEdgeCW(const Point &p) {
  if (&p == points_[0]) {
    return delaunay_edge[1];
  } else if (&p == points_[1]) {
    return delaunay_edge[2];
  }
  return delaunay_edge[0];
}

void Triangle::SetDelunayEdgeCCW(const Point &p, bool e) {
  if (&p == points_[0]) {
    delaunay_edge[2] = e;
  } else if (&p == points_[1]) {
    delaunay_edge[0] = e;
  } else {
    delaunay_edge[1] = e;
  }
}

void Triangle::SetDelunayEdgeCW(const Point &p, bool e) {
  if (&p == points_[0]) {
    delaunay_edge[1] = e;
  } else if (&p == points_[1]) {
    delaunay_edge[2] = e;
  } else {
    delaunay_edge[0] = e;
  }
}

bool Triangle::CircumcicleContains(const Point &point) const {
  CHECK(IsCounterClockwise());
  const double dx = points_[0]->x - point.x;
  const double dy = points_[0]->y - point.y;
  const double ex = points_[1]->x - point.x;
  const double ey = points_[1]->y - point.y;
  const double fx = points_[2]->x - point.x;
  const double fy = points_[2]->y - point.y;

  const double ap = dx * dx + dy * dy;
  const double bp = ex * ex + ey * ey;
  const double cp = fx * fx + fy * fy;

  return (dx * (fy * bp - cp * ey) - dy * (fx * bp - cp * ex) +
          ap * (fx * ey - fy * ex)) < 0;
}

bool Triangle::IsCounterClockwise() const {
  return (points_[1]->x - points_[0]->x) * (points_[2]->y - points_[0]->y) -
    (points_[2]->x - points_[0]->x) * (points_[1]->y - points_[0]->y) > 0;
}

// Advancing front node
struct Node {
  Point *point = nullptr;
  Triangle *triangle = nullptr;

  Node *next = nullptr;
  Node *prev = nullptr;

  double value = 0.0;

  Node(Point &p) : point(&p), value(p.x) {}
  Node(Point &p, Triangle &t) : point(&p), triangle(&t),  value(p.x) {}
};

// Advancing front
class AdvancingFront {
 public:
  AdvancingFront(Node &head, Node &tail) : head_(&head), tail_(&tail),
                                           search_node_(&head) {}
  ~AdvancingFront() {}

  Node *head() { return head_; }
  void set_head(Node *node) { head_ = node; }

  Node *tail() { return tail_; }
  void set_tail(Node *node) { tail_ = node; }

  Node *search() { return search_node_; }
  void set_search(Node *node) { search_node_ = node; }


  /// Locate insertion point along advancing front
  Node *LocateNode(double x);

  Node *LocatePoint(const Point *point);

 private:
  Node *head_ = nullptr, *tail_ = nullptr, *search_node_ = nullptr;
  Node *FindSearchNode(double x);
};

Node *AdvancingFront::LocateNode(double x) {
  Node *node = search_node_;

  if (x < node->value) {
    while ((node = node->prev) != nullptr) {
      if (x >= node->value) {
        search_node_ = node;
        return node;
      }
    }
  } else {
    while ((node = node->next) != nullptr) {
      if (x < node->value) {
        search_node_ = node->prev;
        return node->prev;
      }
    }
  }
  return nullptr;
}

Node *AdvancingFront::FindSearchNode([[maybe_unused]] double x) {
  // TODO: implement BST index
  return search_node_;
}

Node *AdvancingFront::LocatePoint(const Point *point) {
  const double px = point->x;
  Node *node = FindSearchNode(px);
  const double nx = node->point->x;

  if (px == nx) {
    if (point != node->point) {
      // We might have two nodes with same x value for a short time
      if (point == node->prev->point) {
        node = node->prev;
      } else if (point == node->next->point) {
        node = node->next;
      } else {
        LOG(FATAL) << "Bug";
      }
    }
  } else if (px < nx) {
    while ((node = node->prev) != nullptr) {
      if (point == node->point) {
        break;
      }
    }
  } else {
    while ((node = node->next) != nullptr) {
      if (point == node->point)
        break;
    }
  }
  if (node)
    search_node_ = node;
  return node;
}


// Inital triangle factor, seed triangle will extend 30% of
// PointSet width to both left and right.
constexpr double kAlpha = 0.3;

class SweepContext {
 public:
  explicit SweepContext(std::vector<Point *> polyline);
  ~SweepContext() {}

  void set_head(std::unique_ptr<Point> p1);

  Point *head() const;

  void set_tail(std::unique_ptr<Point> p1);

  Point *tail() const;

  size_t point_count() const;

  Node *LocateNode(const Point &point);

  void RemoveNode(Node *node) {}

  void CreateAdvancingFront();

  /// Try to map a node to all sides of this triangle that don't have a neighbor
  void MapTriangleToNodes(Triangle &t);

  void AddToMap(Triangle *triangle);

  Point *GetPoint(size_t index);

  Point *GetPoints();

  void RemoveFromMap(Triangle *triangle);

  void AddHole(const std::vector<Point *> &polyline);

  void AddPoint(Point *point);

  AdvancingFront *front() const;

  void MeshClean(Triangle &triangle);

  const std::vector<Triangle*> &GetTriangles() const {
    return triangles_;
  }

  const std::list<std::unique_ptr<Triangle>> &GetMap() const {
    return map_;
  }

  std::vector<std::unique_ptr<Edge>> edge_list;

  struct Basin {
    Node *left_node;
    Node *bottom_node;
    Node *right_node;
    double width;
    bool left_highest;

    Basin()
        : left_node(nullptr), bottom_node(nullptr), right_node(nullptr),
          width(0.0), left_highest(false) {}

    void Clear() {
      left_node = nullptr;
      bottom_node = nullptr;
      right_node = nullptr;
      width = 0.0;
      left_highest = false;
    }
  };

  struct EdgeEvent {
    Edge *constrained_edge;
    bool right;

    EdgeEvent() : constrained_edge(NULL), right(false) {}
  };

  Basin basin;
  EdgeEvent edge_event;

  private:

  friend class Sweep;

  std::vector<Triangle*> triangles_;
  std::list<std::unique_ptr<Triangle>> map_;
  std::vector<Point*> points_;

  // Advancing front
  std::unique_ptr<AdvancingFront> front_;
  // head point used with advancing front
  std::unique_ptr<Point> head_;
  // tail point used with advancing front
  std::unique_ptr<Point> tail_;

  std::unique_ptr<Node> af_head_, af_middle_, af_tail_;

  void InitTriangulation();
  void InitEdges(const std::vector<Point*>& polyline);
};

inline AdvancingFront *SweepContext::front() const { return front_.get(); }
inline size_t SweepContext::point_count() const { return points_.size(); }
inline void SweepContext::set_head(std::unique_ptr<Point> p1) { head_ = std::move(p1); }
inline Point *SweepContext::head() const { return head_.get(); }
inline void SweepContext::set_tail(std::unique_ptr<Point> p1) { tail_ = std::move(p1); }
inline Point *SweepContext::tail() const { return tail_.get(); }

SweepContext::SweepContext(std::vector<Point *> polyline)
    : points_(std::move(polyline)) {
  InitEdges(points_);
}

void SweepContext::AddHole(const std::vector<Point*>& polyline) {
  InitEdges(polyline);
  for (auto i : polyline) {
    points_.push_back(i);
  }
}

void SweepContext::AddPoint(Point* point) {
  points_.push_back(point);
}

void SweepContext::InitTriangulation() {
  double xmax(points_[0]->x), xmin(points_[0]->x);
  double ymax(points_[0]->y), ymin(points_[0]->y);

  // Calculate bounds.
  for (auto& point : points_) {
    Point& p = *point;
    if (p.x > xmax)
      xmax = p.x;
    if (p.x < xmin)
      xmin = p.x;
    if (p.y > ymax)
      ymax = p.y;
    if (p.y < ymin)
      ymin = p.y;
  }

  double dx = kAlpha * (xmax - xmin);
  double dy = kAlpha * (ymax - ymin);
  head_ = std::make_unique<Point>(xmin - dx, ymin - dy);
  tail_ = std::make_unique<Point>(xmax + dx, ymin - dy);

  // Sort points along y-axis
  std::sort(points_.begin(), points_.end(), cmp);
}

void SweepContext::InitEdges(const std::vector<Point *> &polyline) {
  size_t num_points = polyline.size();
  for (size_t i = 0; i < num_points; i++) {
    size_t j = i < num_points - 1 ? i + 1 : 0;
    edge_list.push_back(std::make_unique<Edge>(*polyline[i], *polyline[j]));
  }
}

Point *SweepContext::GetPoint(size_t index) { return points_[index]; }

void SweepContext::AddToMap(Triangle *triangle) { map_.push_back(std::unique_ptr<Triangle>(triangle)); }

Node *SweepContext::LocateNode(const Point &point) {
  // TODO implement search tree
  return front_->LocateNode(point.x);
}

void SweepContext::CreateAdvancingFront() {

  // Initial triangle
  Triangle* triangle = new Triangle(*points_[0], *head_, *tail_);

  map_.push_back(std::unique_ptr<Triangle>(triangle));

  af_head_ = std::make_unique<Node>(*triangle->GetPoint(1), *triangle);
  af_middle_ = std::make_unique<Node>(*triangle->GetPoint(0), *triangle);
  af_tail_ = std::make_unique<Node>(*triangle->GetPoint(2));
  front_ = std::make_unique<AdvancingFront>(*af_head_, *af_tail_);

  // TODO: More intuitive if head is middles next and not previous?
  //       so swap head and tail
  af_head_->next = af_middle_.get();
  af_middle_->next = af_tail_.get();
  af_middle_->prev = af_head_.get();
  af_tail_->prev = af_middle_.get();
}

void SweepContext::MapTriangleToNodes(Triangle &t) {
  for (int i = 0; i < 3; i++) {
    if (!t.GetNeighbor(i)) {
      Node *n = front_->LocatePoint(t.PointCW(*t.GetPoint(i)));
      if (n)
        n->triangle = &t;
    }
  }
}

void SweepContext::RemoveFromMap(Triangle *triangle) {
  map_.remove_if([triangle](const std::unique_ptr<Triangle> &ptr) {
    return ptr.get() == triangle;
  });
}

void SweepContext::MeshClean(Triangle &triangle) {
  std::vector<Triangle *> triangles;
  triangles.push_back(&triangle);

  while (!triangles.empty()) {
    Triangle *t = triangles.back();
    triangles.pop_back();

    if (t != nullptr && !t->IsInterior()) {
      t->SetInterior(true);
      triangles_.push_back(t);
      for (int i = 0; i < 3; i++) {
        if (!t->constrained_edge[i])
          triangles.push_back(t->GetNeighbor(i));
      }
    }
  }
}

class Sweep {
 public:
  void Triangulate(SweepContext &tcx);
  ~Sweep() {}

 private:
  // Start sweeping the Y-sorted point set from bottom to top
  void SweepPoints(SweepContext &tcx);

  // Find closest node to the left of the new point and
  // create a new triangle. If needed new holes and basins
  // will be filled too.
  Node &PointEvent(SweepContext &tcx, Point &point);

  void EdgeEvent(SweepContext &tcx, Edge *edge, Node *node);

  void EdgeEvent(SweepContext &tcx, Point &ep, Point &eq, Triangle *triangle,
                 Point &point);

  // Creates a new front triangle and legalizes it.
  Node &NewFrontTriangle(SweepContext &tcx, Point &point, Node &node);

  // Adds a triangle to the advancing front to fill a hole.
  // node: middle node, that is the bottom of the hole
  void Fill(SweepContext &tcx, Node &node);

  // Returns true if triangle was legalized.
  bool Legalize(SweepContext &tcx, Triangle &t);

  /**
   * 1. a,b and c form a triangle.
   * 2. a and d is know to be on opposite side of bc
   *                a
   *                +
   *               / \
   *              /   \
   *            b/     \c
   *            +-------+
   *           /    d    \
   *          /           \
   *
   * Fact: d has to be in area B to have a chance to be inside the
   * circle formed by a,b and c
   * d is outside B if orient2d(a,b,d) or
   * orient2d(c,a,d) is CW
   * This preknowledge gives us a way to optimize the
   * incircle test
   * @param a - triangle point, opposite d
   * @param b - triangle point
   * @param c - triangle point
   * @param d - point opposite a
   * @return true if d is inside circle, false if on circle edge
   */
  bool Incircle(const Point &pa, const Point &pb, const Point &pc,
                const Point &pd) const;

  /**
   * Rotates a triangle pair one vertex CW
   *
   *       n2                    n2
   *  P +-----+             P +-----+
   *    | t  /|               |\  t |
   *    |   / |               | \   |
   *  n1|  /  |n3           n1|  \  |n3
   *    | /   |    after CW   |   \ |
   *    |/ oT |               | oT \|
   *    +-----+ oP            +-----+
   *       n4                    n4
   *
   */
  void RotateTrianglePair(Triangle &t, Point &p, Triangle &ot, Point &op) const;

  // Fills holes in the Advancing Front
  void FillAdvancingFront(SweepContext &tcx, Node &n);

  // Decision-making about when to Fill hole.
  // Contributed by ToolmakerSteve2
  bool LargeHole_DontFill(const Node *node) const;
  bool AngleIsNegative(const Point *origin, const Point *pa,
                       const Point *pb) const;
  bool AngleExceeds90Degrees(const Point *origin, const Point *pa,
                             const Point *pb) const;
  bool AngleExceedsPlus90DegreesOrIsNegative(const Point *origin,
                                             const Point *pa,
                                             const Point *pb) const;
  double Angle(const Point *origin, const Point *pa, const Point *pb) const;

  // Returns the angle between 3 front nodes.
  // node - middle node
  double HoleAngle(const Node &node) const;

  // The basin angle is decided against the horizontal line [1,0].
  double BasinAngle(const Node &node) const;

  // Fills a basin that has formed on the Advancing Front to the right
  // of given node.
  // First we decide a left,bottom and right node that forms the
  // boundaries of the basin. Then we do a recursive fill.
  // node - starting node, this or next node will be left node
  void FillBasin(SweepContext &tcx, Node &node);

  // Recursively fill a Basin with triangles.
  // node - bottom_node
  void FillBasinReq(SweepContext &tcx, Node *node);

  bool IsShallow(SweepContext &tcx, Node &node);

  bool IsEdgeSideOfTriangle(Triangle &triangle, Point &ep, Point &eq);

  void FillEdgeEvent(SweepContext &tcx, Edge *edge, Node *node);

  void FillRightAboveEdgeEvent(SweepContext &tcx, Edge *edge, Node *node);

  void FillRightBelowEdgeEvent(SweepContext &tcx, Edge *edge, Node &node);

  void FillRightConcaveEdgeEvent(SweepContext &tcx, Edge *edge, Node &node);

  void FillRightConvexEdgeEvent(SweepContext &tcx, Edge *edge, Node &node);

  void FillLeftAboveEdgeEvent(SweepContext &tcx, Edge *edge, Node *node);

  void FillLeftBelowEdgeEvent(SweepContext &tcx, Edge *edge, Node &node);

  void FillLeftConcaveEdgeEvent(SweepContext &tcx, Edge *edge, Node &node);

  void FillLeftConvexEdgeEvent(SweepContext &tcx, Edge *edge, Node &node);

  void FlipEdgeEvent(SweepContext &tcx, Point &ep, Point &eq, Triangle *t,
                     Point &p);

  // After a flip we have two triangles and know that only one will still be
  // intersecting the edge. So decide which to contiune with and legalize the
  // other.
  // o - should be the result of an orient2d( eq, op, ep )
  // t - triangle 1
  // ot - triangle 2
  // p - a point shared by both triangles
  // op - another point shared by both triangles
  // returns the triangle still intersecting the edge.
  Triangle &NextFlipTriangle(SweepContext &tcx, int o, Triangle &t,
                             Triangle &ot, Point &p, Point &op);

  // When we need to traverse from one triangle to the next we need
  // the point in current triangle that is the opposite point to the next
  // triangle.
  Point& NextFlipPoint(Point& ep, Point& eq, Triangle& ot, Point& op);

  // Scan part of the FlipScan algorithm.
  // When a triangle pair isn't flippable we will scan for the next
  // point that is inside the flip triangle scan area. When found
  // we generate a new flipEdgeEvent.
  //  ep - last point on the edge we are traversing
  //  eq - first point on the edge we are traversing
  //  flipTriangle - the current triangle sharing the point eq with edge
  void FlipScanEdgeEvent(SweepContext &tcx, Point &ep, Point &eq,
                         Triangle &flip_triangle, Triangle &t, Point &p);

  void FinalizationPolygon(SweepContext& tcx);

  std::vector<std::unique_ptr<Node>> nodes_;
};

void Sweep::Triangulate(SweepContext &tcx) {
  tcx.InitTriangulation();
  tcx.CreateAdvancingFront();
  // Sweep points; build mesh
  SweepPoints(tcx);
  // Clean up
  FinalizationPolygon(tcx);
}

void Sweep::SweepPoints(SweepContext &tcx) {
  for (size_t i = 1; i < tcx.point_count(); i++) {
    Point &point = *tcx.GetPoint(i);
    Node *node = &PointEvent(tcx, point);
    for (auto &j : point.edge_list) {
      EdgeEvent(tcx, j, node);
    }
  }
}

void Sweep::FinalizationPolygon(SweepContext &tcx) {
  // Get an Internal triangle to start with
  Triangle *t = tcx.front()->head()->next->triangle;
  Point *p = tcx.front()->head()->next->point;
  while (t && !t->GetConstrainedEdgeCW(*p)) {
    t = t->NeighborCCW(*p);
  }

  // Collect interior triangles constrained by edges
  if (t) {
    tcx.MeshClean(*t);
  }
}

Node &Sweep::PointEvent(SweepContext &tcx, Point &point) {
  Node *node_ptr = tcx.LocateNode(point);
  if (!node_ptr || !node_ptr->point || !node_ptr->next ||
      !node_ptr->next->point) {
    LOG(FATAL) << "PointEvent - null node";
  }

  Node &node = *node_ptr;
  Node &new_node = NewFrontTriangle(tcx, point, node);

  // Only need to check +epsilon since point never have smaller
  // x value than node due to how we fetch nodes from the front
  if (point.x <= node.point->x + EPSILON) {
    Fill(tcx, node);
  }

  //tcx.AddNode(new_node);

  FillAdvancingFront(tcx, new_node);
  return new_node;
}

void Sweep::EdgeEvent(SweepContext &tcx, Edge *edge, Node *node) {
  tcx.edge_event.constrained_edge = edge;
  tcx.edge_event.right = (edge->p->x > edge->q->x);

  if (IsEdgeSideOfTriangle(*node->triangle, *edge->p, *edge->q)) {
    return;
  }

  // For now we will do all needed filling.
  // TODO: integrating with flip process might give some better
  //       performance but for now this avoid the issue with cases
  //       that needs both flips and fills
  FillEdgeEvent(tcx, edge, node);
  EdgeEvent(tcx, *edge->p, *edge->q, node->triangle, *edge->q);
}

void Sweep::EdgeEvent(SweepContext &tcx, Point &ep, Point &eq,
                      Triangle *triangle, Point &point) {
  if (triangle == nullptr) {
    LOG(FATAL) << "EdgeEvent - null triangle";
  }
  if (IsEdgeSideOfTriangle(*triangle, ep, eq)) {
    return;
  }

  Point *p1 = triangle->PointCCW(point);
  Orientation o1 = Orient2d(eq, *p1, ep);
  if (o1 == COLINEAR) {
    if (triangle->Contains(&eq, p1)) {
      triangle->MarkConstrainedEdge(&eq, p1);
      // We are modifying the constraint maybe it would be better to
      // not change the given constraint and just keep a variable for the new
      // constraint
      tcx.edge_event.constrained_edge->q = p1;
      triangle = triangle->NeighborAcross(point);
      EdgeEvent(tcx, ep, *p1, triangle, *p1);
    } else {
      LOG(FATAL) << "EdgeEvent - colinear points not supported";
    }
    return;
  }

  Point *p2 = triangle->PointCW(point);
  Orientation o2 = Orient2d(eq, *p2, ep);
  if (o2 == COLINEAR) {
    if (triangle->Contains(&eq, p2)) {
      triangle->MarkConstrainedEdge(&eq, p2);
      // We are modifying the constraint maybe it would be better to
      // not change the given constraint and just keep a variable for the new
      // constraint
      tcx.edge_event.constrained_edge->q = p2;
      triangle = triangle->NeighborAcross(point);
      EdgeEvent(tcx, ep, *p2, triangle, *p2);
    } else {
      LOG(FATAL) << "EdgeEvent - colinear points not supported";
    }
    return;
  }

  if (o1 == o2) {
    // Need to decide if we are rotating CW or CCW to get to a triangle
    // that will cross edge
    if (o1 == CW) {
      triangle = triangle->NeighborCCW(point);
    } else {
      triangle = triangle->NeighborCW(point);
    }
    EdgeEvent(tcx, ep, eq, triangle, point);
  } else {
    // This triangle crosses constraint so let's flippin' start!
    CHECK(triangle);
    FlipEdgeEvent(tcx, ep, eq, triangle, point);
  }
}

bool Sweep::IsEdgeSideOfTriangle(Triangle &triangle, Point &ep, Point &eq) {
  const int index = triangle.EdgeIndex(&ep, &eq);

  if (index != -1) {
    triangle.MarkConstrainedEdge(index);
    Triangle *t = triangle.GetNeighbor(index);
    if (t) {
      t->MarkConstrainedEdge(&ep, &eq);
    }
    return true;
  }
  return false;
}

Node &Sweep::NewFrontTriangle(SweepContext &tcx, Point &point, Node &node) {
  Triangle *triangle = new Triangle(point, *node.point, *node.next->point);

  triangle->MarkNeighbor(*node.triangle);
  tcx.AddToMap(triangle);

  auto new_node_ptr = std::make_unique<Node>(point);
  Node *new_node = new_node_ptr.get();
  nodes_.push_back(std::move(new_node_ptr));

  new_node->next = node.next;
  new_node->prev = &node;
  node.next->prev = new_node;
  node.next = new_node;

  if (!Legalize(tcx, *triangle)) {
    tcx.MapTriangleToNodes(*triangle);
  }

  return *new_node;
}

void Sweep::Fill(SweepContext &tcx, Node &node) {
  Triangle *triangle =
      new Triangle(*node.prev->point, *node.point, *node.next->point);

  // TODO: should copy the constrained_edge value from neighbor triangles
  //       for now constrained_edge values are copied during the legalize
  triangle->MarkNeighbor(*node.prev->triangle);
  triangle->MarkNeighbor(*node.triangle);

  tcx.AddToMap(triangle);

  // Update the advancing front
  node.prev->next = node.next;
  node.next->prev = node.prev;

  // If it was legalized the triangle has already been mapped
  if (!Legalize(tcx, *triangle)) {
    tcx.MapTriangleToNodes(*triangle);
  }
}

void Sweep::FillAdvancingFront(SweepContext &tcx, Node &n) {
  // Fill right holes
  Node *node = n.next;

  while (node && node->next) {
    // if HoleAngle exceeds 90 degrees then break.
    if (LargeHole_DontFill(node))
      break;
    Fill(tcx, *node);
    node = node->next;
  }

  // Fill left holes
  node = n.prev;

  while (node && node->prev) {
    // if HoleAngle exceeds 90 degrees then break.
    if (LargeHole_DontFill(node))
      break;
    Fill(tcx, *node);
    node = node->prev;
  }

  // Fill right basins
  if (n.next && n.next->next) {
    const double angle = BasinAngle(n);
    if (angle < PI_3div4) {
      FillBasin(tcx, n);
    }
  }
}

// True if HoleAngle exceeds 90 degrees.
// LargeHole_DontFill checks if the advancing front has a large hole.
// A "Large hole" is a triangle formed by a sequence of points in the
// advancing front where three neighbor points form a triangle. And
// angle between left-top, bottom, and right-top points is more than
// 90 degrees. The first part of the algorithm reviews only three
// neighbor points, e.g. named A, B, C. Additional part of this logic
// reviews a sequence of 5 points - additionally reviews one point
// before and one after the sequence of three (A, B, C), e.g. named X
// and Y.
//
// In this case, angles are XBC and ABY and this if angles are
// negative or more than 90 degrees LargeHole_DontFill returns true.
// But there is a configuration when ABC has a negative angle but XBC
// or ABY is less than 90 degrees and positive. Then function
// LargeHole_DontFill return false and initiates filling. This filling
// creates a triangle ABC and adds it to the advancing front. But in
// the case when angle ABC is negative this triangle goes inside the
// advancing front and can intersect previously created triangles.
// This triangle leads to making wrong advancing front and problems in
// triangulation in the future. Looks like such a triangle should not
// be created. The simplest way to check and fix it is to check an
// angle ABC. If it is negative LargeHole_DontFill should return true
// and not initiate creating the ABC triangle in the advancing front.
//
// X______A         Y
//        \        /
//         \      /
//          \ B  /
//           |  /
//           | /
//           |/
//           C
bool Sweep::LargeHole_DontFill(const Node *node) const {
  const Node *nextNode = node->next;
  const Node *prevNode = node->prev;
  if (!AngleExceeds90Degrees(node->point, nextNode->point, prevNode->point))
    return false;

  if (AngleIsNegative(node->point, nextNode->point, prevNode->point))
    return true;

  // Check additional points on front.
  const Node *next2Node = nextNode->next;
  // "..Plus.." because only want angles on same side as point being added.
  if (next2Node != nullptr &&
      !AngleExceedsPlus90DegreesOrIsNegative(node->point, next2Node->point,
                                             prevNode->point))
    return false;

  const Node *prev2Node = prevNode->prev;
  // "..Plus.." because only want angles on same side as point being added.
  if (prev2Node != nullptr &&
      !AngleExceedsPlus90DegreesOrIsNegative(node->point, nextNode->point,
                                             prev2Node->point))
    return false;

  return true;
}

bool Sweep::AngleIsNegative(const Point *origin, const Point *pa,
                            const Point *pb) const {
  const double angle = Angle(origin, pa, pb);
  return angle < 0;
}

bool Sweep::AngleExceeds90Degrees(const Point *origin, const Point *pa,
                                  const Point *pb) const {
  const double angle = Angle(origin, pa, pb);
  return (angle > PI_div2) || (angle < -PI_div2);
}

bool Sweep::AngleExceedsPlus90DegreesOrIsNegative(const Point *origin,
                                                  const Point *pa,
                                                  const Point *pb) const {
  const double angle = Angle(origin, pa, pb);
  return (angle > PI_div2) || (angle < 0);
}

double Sweep::Angle(const Point *origin, const Point *pa,
                    const Point *pb) const {
  /* Complex plane
   * ab = cosA +i*sinA
   * ab = (ax + ay*i)(bx + by*i) = (ax*bx + ay*by) + i(ax*by-ay*bx)
   * atan2(y,x) computes the principal value of the argument function
   * applied to the complex number x+iy
   * Where x = ax*bx + ay*by
   *       y = ax*by - ay*bx
   */
  const double px = origin->x;
  const double py = origin->y;
  const double ax = pa->x - px;
  const double ay = pa->y - py;
  const double bx = pb->x - px;
  const double by = pb->y - py;
  const double x = ax * by - ay * bx;
  const double y = ax * bx + ay * by;
  return atan2(x, y);
}

double Sweep::BasinAngle(const Node &node) const {
  const double ax = node.point->x - node.next->next->point->x;
  const double ay = node.point->y - node.next->next->point->y;
  return atan2(ay, ax);
}

double Sweep::HoleAngle(const Node &node) const {
  /* Complex plane
   * ab = cosA +i*sinA
   * ab = (ax + ay*i)(bx + by*i) = (ax*bx + ay*by) + i(ax*by-ay*bx)
   * atan2(y,x) computes the principal value of the argument function
   * applied to the complex number x+iy
   * Where x = ax*bx + ay*by
   *       y = ax*by - ay*bx
   */
  const double ax = node.next->point->x - node.point->x;
  const double ay = node.next->point->y - node.point->y;
  const double bx = node.prev->point->x - node.point->x;
  const double by = node.prev->point->y - node.point->y;
  return atan2(ax * by - ay * bx, ax * bx + ay * by);
}

bool Sweep::Legalize(SweepContext &tcx, Triangle &t) {
  // To legalize a triangle we start by finding if any of the three edges
  // violate the Delaunay condition
  for (int i = 0; i < 3; i++) {
    if (t.delaunay_edge[i])
      continue;

    Triangle *ot = t.GetNeighbor(i);

    if (ot) {
      Point *p = t.GetPoint(i);
      Point *op = ot->OppositePoint(t, *p);
      int oi = ot->Index(op);

      // If this is a Constrained Edge or a Delaunay Edge(only during recursive
      // legalization) then we should not try to legalize
      if (ot->constrained_edge[oi] || ot->delaunay_edge[oi]) {
        t.constrained_edge[i] = ot->constrained_edge[oi];
        continue;
      }

      bool inside = Incircle(*p, *t.PointCCW(*p), *t.PointCW(*p), *op);

      if (inside) {
        // Lets mark this shared edge as Delaunay
        t.delaunay_edge[i] = true;
        ot->delaunay_edge[oi] = true;

        // Lets rotate shared edge one vertex CW to legalize it
        RotateTrianglePair(t, *p, *ot, *op);

        // We now got one valid Delaunay Edge shared by two triangles
        // This gives us 4 new edges to check for Delaunay

        // Make sure that triangle to node mapping is done only one time for a
        // specific triangle
        bool not_legalized = !Legalize(tcx, t);
        if (not_legalized) {
          tcx.MapTriangleToNodes(t);
        }

        not_legalized = !Legalize(tcx, *ot);
        if (not_legalized)
          tcx.MapTriangleToNodes(*ot);

        // Reset the Delaunay edges, since they only are valid Delaunay edges
        // until we add a new triangle or point.
        // XXX: need to think about this. Can these edges be tried after we
        //      return to previous recursive level?
        t.delaunay_edge[i] = false;
        ot->delaunay_edge[oi] = false;

        // If triangle have been legalized no need to check the other edges
        // since the recursive legalization will handles those so we can end
        // here.
        return true;
      }
    }
  }
  return false;
}

bool Sweep::Incircle(const Point &pa, const Point &pb, const Point &pc,
                     const Point &pd) const {
  const double adx = pa.x - pd.x;
  const double ady = pa.y - pd.y;
  const double bdx = pb.x - pd.x;
  const double bdy = pb.y - pd.y;

  const double adxbdy = adx * bdy;
  const double bdxady = bdx * ady;
  const double oabd = adxbdy - bdxady;

  if (oabd <= 0)
    return false;

  const double cdx = pc.x - pd.x;
  const double cdy = pc.y - pd.y;

  const double cdxady = cdx * ady;
  const double adxcdy = adx * cdy;
  const double ocad = cdxady - adxcdy;

  if (ocad <= 0)
    return false;

  const double bdxcdy = bdx * cdy;
  const double cdxbdy = cdx * bdy;

  const double alift = adx * adx + ady * ady;
  const double blift = bdx * bdx + bdy * bdy;
  const double clift = cdx * cdx + cdy * cdy;

  const double det = alift * (bdxcdy - cdxbdy) + blift * ocad + clift * oabd;

  return det > 0;
}

void Sweep::RotateTrianglePair(Triangle &t, Point &p, Triangle &ot,
                               Point &op) const {
  Triangle *n1 = t.NeighborCCW(p);
  Triangle *n2 = t.NeighborCW(p);
  Triangle *n3 = ot.NeighborCCW(op);
  Triangle *n4 = ot.NeighborCW(op);

  bool ce1 = t.GetConstrainedEdgeCCW(p);
  bool ce2 = t.GetConstrainedEdgeCW(p);
  bool ce3 = ot.GetConstrainedEdgeCCW(op);
  bool ce4 = ot.GetConstrainedEdgeCW(op);

  bool de1 = t.GetDelunayEdgeCCW(p);
  bool de2 = t.GetDelunayEdgeCW(p);
  bool de3 = ot.GetDelunayEdgeCCW(op);
  bool de4 = ot.GetDelunayEdgeCW(op);

  t.Legalize(p, op);
  ot.Legalize(op, p);

  // Remap delaunay_edge
  ot.SetDelunayEdgeCCW(p, de1);
  t.SetDelunayEdgeCW(p, de2);
  t.SetDelunayEdgeCCW(op, de3);
  ot.SetDelunayEdgeCW(op, de4);

  // Remap constrained_edge
  ot.SetConstrainedEdgeCCW(p, ce1);
  t.SetConstrainedEdgeCW(p, ce2);
  t.SetConstrainedEdgeCCW(op, ce3);
  ot.SetConstrainedEdgeCW(op, ce4);

  // Remap neighbors
  // XXX: might optimize the markNeighbor by keeping track of
  //      what side should be assigned to what neighbor after the
  //      rotation. Now mark neighbor does lots of testing to find
  //      the right side.
  t.ClearNeighbors();
  ot.ClearNeighbors();
  if (n1)
    ot.MarkNeighbor(*n1);
  if (n2)
    t.MarkNeighbor(*n2);
  if (n3)
    t.MarkNeighbor(*n3);
  if (n4)
    ot.MarkNeighbor(*n4);
  t.MarkNeighbor(ot);
}

void Sweep::FillBasin(SweepContext &tcx, Node &node) {
  if (Orient2d(*node.point, *node.next->point, *node.next->next->point) ==
      CCW) {
    tcx.basin.left_node = node.next->next;
  } else {
    tcx.basin.left_node = node.next;
  }

  // Find the bottom and right node
  tcx.basin.bottom_node = tcx.basin.left_node;
  while (tcx.basin.bottom_node->next &&
         tcx.basin.bottom_node->point->y >=
             tcx.basin.bottom_node->next->point->y) {
    tcx.basin.bottom_node = tcx.basin.bottom_node->next;
  }
  if (tcx.basin.bottom_node == tcx.basin.left_node) {
    // No valid basin
    return;
  }

  tcx.basin.right_node = tcx.basin.bottom_node;
  while (tcx.basin.right_node->next &&
         tcx.basin.right_node->point->y <
             tcx.basin.right_node->next->point->y) {
    tcx.basin.right_node = tcx.basin.right_node->next;
  }
  if (tcx.basin.right_node == tcx.basin.bottom_node) {
    // No valid basins
    return;
  }

  tcx.basin.width =
      tcx.basin.right_node->point->x - tcx.basin.left_node->point->x;
  tcx.basin.left_highest =
      tcx.basin.left_node->point->y > tcx.basin.right_node->point->y;

  FillBasinReq(tcx, tcx.basin.bottom_node);
}

void Sweep::FillBasinReq(SweepContext &tcx, Node *node) {
  // if shallow stop filling
  if (IsShallow(tcx, *node)) {
    return;
  }

  Fill(tcx, *node);

  if (node->prev == tcx.basin.left_node && node->next == tcx.basin.right_node) {
    return;
  } else if (node->prev == tcx.basin.left_node) {
    Orientation o =
        Orient2d(*node->point, *node->next->point, *node->next->next->point);
    if (o == CW) {
      return;
    }
    node = node->next;
  } else if (node->next == tcx.basin.right_node) {
    Orientation o =
        Orient2d(*node->point, *node->prev->point, *node->prev->prev->point);
    if (o == CCW) {
      return;
    }
    node = node->prev;
  } else {
    // Continue with the neighbor node with lowest Y value
    if (node->prev->point->y < node->next->point->y) {
      node = node->prev;
    } else {
      node = node->next;
    }
  }

  FillBasinReq(tcx, node);
}

bool Sweep::IsShallow(SweepContext &tcx, Node &node) {
  double height;

  if (tcx.basin.left_highest) {
    height = tcx.basin.left_node->point->y - node.point->y;
  } else {
    height = tcx.basin.right_node->point->y - node.point->y;
  }

  // if shallow stop filling
  if (tcx.basin.width > height) {
    return true;
  }
  return false;
}

void Sweep::FillEdgeEvent(SweepContext &tcx, Edge *edge, Node *node) {
  if (tcx.edge_event.right) {
    FillRightAboveEdgeEvent(tcx, edge, node);
  } else {
    FillLeftAboveEdgeEvent(tcx, edge, node);
  }
}

void Sweep::FillRightAboveEdgeEvent(SweepContext &tcx, Edge *edge, Node *node) {
  while (node->next->point->x < edge->p->x) {
    // Check if next node is below the edge
    if (Orient2d(*edge->q, *node->next->point, *edge->p) == CCW) {
      FillRightBelowEdgeEvent(tcx, edge, *node);
    } else {
      node = node->next;
    }
  }
}

void Sweep::FillRightBelowEdgeEvent(SweepContext &tcx, Edge *edge, Node &node) {
  if (node.point->x < edge->p->x) {
    if (Orient2d(*node.point, *node.next->point, *node.next->next->point) ==
        CCW) {
      // Concave
      FillRightConcaveEdgeEvent(tcx, edge, node);
    } else {
      // Convex
      FillRightConvexEdgeEvent(tcx, edge, node);
      // Retry this one
      FillRightBelowEdgeEvent(tcx, edge, node);
    }
  }
}

void Sweep::FillRightConcaveEdgeEvent(SweepContext &tcx, Edge *edge,
                                      Node &node) {
  Fill(tcx, *node.next);
  if (node.next->point != edge->p) {
    // Next above or below edge?
    if (Orient2d(*edge->q, *node.next->point, *edge->p) == CCW) {
      // Below
      if (Orient2d(*node.point, *node.next->point, *node.next->next->point) ==
          CCW) {
        // Next is concave
        FillRightConcaveEdgeEvent(tcx, edge, node);
      } else {
        // Next is convex
      }
    }
  }
}

void Sweep::FillRightConvexEdgeEvent(SweepContext &tcx, Edge *edge,
                                     Node &node) {
  // Next concave or convex?
  if (Orient2d(*node.next->point, *node.next->next->point,
               *node.next->next->next->point) == CCW) {
    // Concave
    FillRightConcaveEdgeEvent(tcx, edge, *node.next);
  } else {
    // Convex
    // Next above or below edge?
    if (Orient2d(*edge->q, *node.next->next->point, *edge->p) == CCW) {
      // Below
      FillRightConvexEdgeEvent(tcx, edge, *node.next);
    } else {
      // Above
    }
  }
}

void Sweep::FillLeftAboveEdgeEvent(SweepContext &tcx, Edge *edge, Node *node) {
  while (node->prev->point->x > edge->p->x) {
    // Check if next node is below the edge
    if (Orient2d(*edge->q, *node->prev->point, *edge->p) == CW) {
      FillLeftBelowEdgeEvent(tcx, edge, *node);
    } else {
      node = node->prev;
    }
  }
}

void Sweep::FillLeftBelowEdgeEvent(SweepContext &tcx, Edge *edge, Node &node) {
  if (node.point->x > edge->p->x) {
    if (Orient2d(*node.point, *node.prev->point, *node.prev->prev->point) ==
        CW) {
      // Concave
      FillLeftConcaveEdgeEvent(tcx, edge, node);
    } else {
      // Convex
      FillLeftConvexEdgeEvent(tcx, edge, node);
      // Retry this one
      FillLeftBelowEdgeEvent(tcx, edge, node);
    }
  }
}

void Sweep::FillLeftConvexEdgeEvent(SweepContext &tcx, Edge *edge, Node &node) {
  // Next concave or convex?
  if (Orient2d(*node.prev->point, *node.prev->prev->point,
               *node.prev->prev->prev->point) == CW) {
    // Concave
    FillLeftConcaveEdgeEvent(tcx, edge, *node.prev);
  } else {
    // Convex
    // Next above or below edge?
    if (Orient2d(*edge->q, *node.prev->prev->point, *edge->p) == CW) {
      // Below
      FillLeftConvexEdgeEvent(tcx, edge, *node.prev);
    } else {
      // Above
    }
  }
}

void Sweep::FillLeftConcaveEdgeEvent(SweepContext &tcx, Edge *edge,
                                     Node &node) {
  Fill(tcx, *node.prev);
  if (node.prev->point != edge->p) {
    // Next above or below edge?
    if (Orient2d(*edge->q, *node.prev->point, *edge->p) == CW) {
      // Below
      if (Orient2d(*node.point, *node.prev->point,
                   *node.prev->prev->point) == CW) {
        // Next is concave
        FillLeftConcaveEdgeEvent(tcx, edge, node);
      } else {
        // Next is convex
      }
    }
  }
}

void Sweep::FlipEdgeEvent(SweepContext &tcx, Point &ep, Point &eq, Triangle *t,
                          Point &p) {
  CHECK(t != nullptr);
  Triangle *ot_ptr = t->NeighborAcross(p);
  if (ot_ptr == nullptr) {
    LOG(FATAL) << "FlipEdgeEvent - null neighbor across";
  }
  Triangle &ot = *ot_ptr;
  Point &op = *ot.OppositePoint(*t, p);

  if (InScanArea(p, *t->PointCCW(p), *t->PointCW(p), op)) {
    // Lets rotate shared edge one vertex CW
    RotateTrianglePair(*t, p, ot, op);
    tcx.MapTriangleToNodes(*t);
    tcx.MapTriangleToNodes(ot);

    if (p == eq && op == ep) {
      if (eq == *tcx.edge_event.constrained_edge->q &&
          ep == *tcx.edge_event.constrained_edge->p) {
        t->MarkConstrainedEdge(&ep, &eq);
        ot.MarkConstrainedEdge(&ep, &eq);
        Legalize(tcx, *t);
        Legalize(tcx, ot);
      } else {
        // XXX: I think one of the triangles should be legalized here?
      }
    } else {
      Orientation o = Orient2d(eq, op, ep);
      t = &NextFlipTriangle(tcx, (int)o, *t, ot, p, op);
      FlipEdgeEvent(tcx, ep, eq, t, p);
    }
  } else {
    Point &newP = NextFlipPoint(ep, eq, ot, op);
    FlipScanEdgeEvent(tcx, ep, eq, *t, ot, newP);
    EdgeEvent(tcx, ep, eq, t, p);
  }
}

Triangle &Sweep::NextFlipTriangle(SweepContext &tcx, int o, Triangle &t,
                                  Triangle &ot, Point &p, Point &op) {
  if (o == CCW) {
    // ot is not crossing edge after flip
    int edge_index = ot.EdgeIndex(&p, &op);
    ot.delaunay_edge[edge_index] = true;
    Legalize(tcx, ot);
    ot.ClearDelunayEdges();
    return t;
  }

  // t is not crossing edge after flip
  int edge_index = t.EdgeIndex(&p, &op);

  t.delaunay_edge[edge_index] = true;
  Legalize(tcx, t);
  t.ClearDelunayEdges();
  return ot;
}

Point &Sweep::NextFlipPoint(Point &ep, Point &eq, Triangle &ot, Point &op) {
  Orientation o2d = Orient2d(eq, op, ep);
  if (o2d == CW) {
    // Right
    return *ot.PointCCW(op);
  } else if (o2d == CCW) {
    // Left
    return *ot.PointCW(op);
  }
  LOG(FATAL) << "[Unsupported] Opposing point on constrained edge";
}

void Sweep::FlipScanEdgeEvent(SweepContext &tcx, Point &ep, Point &eq,
                              Triangle &flip_triangle, Triangle &t, Point &p) {
  Triangle *ot_ptr = t.NeighborAcross(p);
  if (ot_ptr == nullptr) {
    LOG(FATAL) << "FlipScanEdgeEvent - null neighbor across";
  }

  Point *op_ptr = ot_ptr->OppositePoint(t, p);
  if (op_ptr == nullptr) {
    LOG(FATAL) << "FlipScanEdgeEvent - null opposing point";
  }

  Point *p1 = flip_triangle.PointCCW(eq);
  Point *p2 = flip_triangle.PointCW(eq);
  if (p1 == nullptr || p2 == nullptr) {
    LOG(FATAL) << "FlipScanEdgeEvent - null on either of points";
  }

  Triangle &ot = *ot_ptr;
  Point &op = *op_ptr;

  if (InScanArea(eq, *p1, *p2, op)) {
    // flip with new edge op->eq
    FlipEdgeEvent(tcx, eq, op, &ot, op);
    // TODO: Actually I just figured out that it should be possible to
    //       improve this by getting the next ot and op before the the above
    //       flip and continue the flipScanEdgeEvent here
    // set new ot and op here and loop back to inScanArea test
    // also need to set a new flip_triangle first
    // Turns out at first glance that this is somewhat complicated
    // so it will have to wait.
  } else {
    Point &newP = NextFlipPoint(ep, eq, ot, op);
    FlipScanEdgeEvent(tcx, ep, eq, flip_triangle, ot, newP);
  }
}

class CDT {
 public:
  // Add polyline with non repeating points
  explicit CDT(const std::vector<Point *> &polyline) {
    sweep_context_ = std::make_unique<SweepContext>(polyline);
    sweep_ = std::make_unique<Sweep>();
  }

  ~CDT() {}

  void AddHole(const std::vector<Point *> &polyline) {
    sweep_context_->AddHole(polyline);
  }

  // Add a Steiner point.
  void AddPoint(Point *point) {
    sweep_context_->AddPoint(point);
  }

  // Triangulate - do this AFTER you've added the polyline, holes, and
  // Steiner points.
  void Triangulate() {
    sweep_->Triangulate(*sweep_context_);
  }

  // Get CDT triangles.
  const std::vector<Triangle*> &GetTriangles() {
    return sweep_context_->GetTriangles();
  }

  // Get triangle map.
  const std::list<std::unique_ptr<Triangle>> &GetMap() {
    return sweep_context_->GetMap();
  }

 private:
  std::unique_ptr<SweepContext> sweep_context_;
  std::unique_ptr<Sweep> sweep_;
};

}  // namespace


// TODO: Add Steiner points!
Polygonization::TriangulateResult Polygonization::Triangulate(
    const Shape &shape) {
  if (shape.polys.empty()) {
    return TriangularMesh{};
  }

  int num_polys = (int)shape.polys.size();
  std::vector<int> depth(num_polys, 0);
  std::vector<int> parent(num_polys, -1);

  for (int i = 0; i < num_polys; i++) {
    const Polygon &path = shape.polys[i];
    if (path.empty()) {
      continue;
    }
    vec2 pt = path[0];
    for (int j = 0; j < num_polys; j++) {
      if (i == j || shape.polys[j].empty()) {
        continue;
      }
      if (PointInPolygon(shape.polys[j], pt)) {
        depth[i]++;
      }
    }
  }

  for (int i = 0; i < num_polys; i++) {
    if (depth[i] == 0 || shape.polys[i].empty()) {
      continue;
    }
    vec2 pt = shape.polys[i][0];
    for (int j = 0; j < num_polys; j++) {
      if (i == j || shape.polys[j].empty()) {
        continue;
      }
      if (depth[j] == depth[i] - 1 &&
          PointInPolygon(shape.polys[j], pt)) {
        parent[i] = j;
        break;
      }
    }
  }

  auto CleanPath = [&](std::span<const vec2> original, bool ccw) {
    std::vector<vec2> path;
    for (int i = 0; i < (int)original.size(); i++) {
      const vec2 &v = original[i];
      if (path.empty() || path.back().x != v.x || path.back().y != v.y) {
        path.push_back(v);
      }
    }
    while (path.size() > 1 && path.front().x == path.back().x &&
           path.front().y == path.back().y) {
      path.pop_back();
    }
    if (path.size() < 3) {
      return path;
    }

    double area = 0.0;
    for (int i = 0; i < (int)path.size(); i++) {
      int j = (i + 1) % (int)path.size();
      area += path[i].x * path[j].y - path[j].x * path[i].y;
    }
    bool is_ccw = area > 0.0;
    if (is_ccw != ccw) {
      std::reverse(path.begin(), path.end());
    }
    return path;
  };

  std::vector<std::vector<vec2>> cleaned_paths(num_polys);
  for (int i = 0; i < num_polys; i++) {
    cleaned_paths[i] = CleanPath(shape.polys[i],
                                 depth[i] % 2 == 0);
  }

  TriangularMesh mesh;

  for (int i = 0; i < num_polys; i++) {
    if (depth[i] % 2 != 0 || cleaned_paths[i].size() < 3) {
      continue;
    }

    int num_pts = (int)cleaned_paths[i].size();
    for (int j = 0; j < num_polys; j++) {
      if (parent[j] == i && cleaned_paths[j].size() >= 3) {
        num_pts += (int)cleaned_paths[j].size();
      }
    }

    std::vector<Point> pts;
    pts.reserve(num_pts);

    std::vector<std::pair<const Point *, int>> pt_map;
    pt_map.reserve(num_pts);

    auto add_pt = [&](const vec2 &v) {
      pts.push_back(Point(v.x, v.y));
      Point *p = &pts.back();
      mesh.vertices.push_back(v);
      pt_map.push_back({p, (int)mesh.vertices.size() - 1});
      return p;
    };

    std::vector<Point *> polyline;
    for (int k = 0; k < (int)cleaned_paths[i].size(); k++) {
      polyline.push_back(add_pt(cleaned_paths[i][k]));
    }

    CDT cdt(polyline);

    for (int j = 0; j < num_polys; j++) {
      if (parent[j] == i && cleaned_paths[j].size() >= 3) {
        std::vector<Point *> hole;
        for (int k = 0; k < (int)cleaned_paths[j].size(); k++) {
          hole.push_back(add_pt(cleaned_paths[j][k]));
        }
        cdt.AddHole(hole);
      }
    }

    cdt.Triangulate();

    std::sort(pt_map.begin(), pt_map.end(),
              [](const std::pair<const Point *, int> &a,
                 const std::pair<const Point *, int> &b) {
                return a.first < b.first;
              });

    auto GetIdx = [&](const Point *p) {
      auto it = std::lower_bound(
          pt_map.begin(), pt_map.end(), std::make_pair(p, 0),
          [](const std::pair<const Point *, int> &a,
             const std::pair<const Point *, int> &b) {
            return a.first < b.first;
          });
      return it->second;
    };

    const std::vector<Triangle *> &triangles = cdt.GetTriangles();
    int num_triangles = (int)triangles.size();
    for (int t_idx = 0; t_idx < num_triangles; t_idx++) {
      Triangle *t = triangles[t_idx];
      std::array<int, 3> poly;
      for (int k = 0; k < 3; k++) {
        poly[k] = GetIdx(t->GetPoint(k));
      }
      // Reverse from Poly2Tri Cartesian CCW to Mesh Cartesian CW.
      mesh.triangles.emplace_back(poly[2], poly[1], poly[0]);
    }
  }

  return mesh;
}

Polygonization::PolygonizeResult Polygonization::Polygonize(
    const Shape &shape, int max_vertices) {
  auto tres = Triangulate(shape);
  if (const std::string_view *err = std::get_if<std::string_view>(&tres)) {
    return {*err};
  }
  CHECK(std::holds_alternative<TriangularMesh>(tres));
  TriangularMesh &tri_mesh = std::get<TriangularMesh>(tres);
  std::vector<std::tuple<int, int, int>> triangles =
    std::move(tri_mesh.triangles);

  Mesh mesh;
  mesh.vertices = std::move(tri_mesh.vertices);

  if (mesh.vertices.empty() || triangles.empty()) {
    return mesh;
  }

  std::vector<std::vector<int>> polygons;
  polygons.reserve(triangles.size());
  for (size_t i = 0; i < triangles.size(); i++) {
    const auto &[a, b, c] = triangles[i];
    polygons.push_back({a, b, c});
  }

  std::vector<bool> active(polygons.size(), true);
  std::unordered_map<uint64_t, int> edge_to_poly;

  auto MakeEdge = [](int u, int v) -> uint64_t {
    return ((uint64_t)(uint32_t)u << 32) | (uint32_t)v;
  };

  for (int i = 0; i < (int)polygons.size(); i++) {
    const std::vector<int> &poly = polygons[i];
    for (int j = 0; j < (int)poly.size(); j++) {
      int u = poly[j];
      int v = poly[(j + 1) % poly.size()];
      edge_to_poly[MakeEdge(u, v)] = i;
    }
  }

  struct EdgeMerge {
    int p1, p2;
    int u, v;
    double score;
    bool operator<(const EdgeMerge &other) const {
      return score < other.score;
    }
  };
  std::priority_queue<EdgeMerge> pq;

  // Prioritize merging along the longest edges first to eliminate slivers.
  for (int i = 0; i < (int)polygons.size(); i++) {
    const std::vector<int> &poly = polygons[i];
    for (int j = 0; j < (int)poly.size(); j++) {
      int u = poly[j];
      int v = poly[(j + 1) % poly.size()];
      if (u < v) {
        std::unordered_map<uint64_t, int>::iterator it1 =
            edge_to_poly.find(MakeEdge(u, v));
        std::unordered_map<uint64_t, int>::iterator it2 =
            edge_to_poly.find(MakeEdge(v, u));

        if (it1 != edge_to_poly.end() && it2 != edge_to_poly.end()) {
          int p1 = it1->second;
          int p2 = it2->second;
          vec2 diff = mesh.vertices[u] - mesh.vertices[v];
          double score = diff.x * diff.x + diff.y * diff.y;
          pq.push({p1, p2, u, v, score});
        }
      }
    }
  }

  auto CheckAngle = [&](int prev, int curr, int next) {
    vec2 p_prev = mesh.vertices[prev];
    vec2 p_curr = mesh.vertices[curr];
    vec2 p_next = mesh.vertices[next];

    vec2 v1 = p_curr - p_prev;
    vec2 v2 = p_next - p_curr;

    double cross_prod = v1.x * v2.y - v1.y * v2.x;
    double dot_prod = v1.x * v2.x + v1.y * v2.y;

    // Reject if concave or completely colinear (CW turn means
    // cross_prod < 0).
    if (cross_prod > -1e-6) return false;

    double len_sq1 = v1.x * v1.x + v1.y * v1.y;
    double len_sq2 = v2.x * v2.x + v2.y * v2.y;

    // Reject very shallow angles (deviating by less than ~5 degrees).
    // This corresponds to dot_prod > 0 and
    // cross_prod^2 < sin(5 deg)^2 * lens.
    if (dot_prod > 0 &&
        cross_prod * cross_prod < 0.0075 * len_sq1 * len_sq2) {
      return false;
    }
    return true;
  };

  while (!pq.empty()) {
    EdgeMerge em = pq.top();
    pq.pop();

    if (!active[em.p1] || !active[em.p2]) continue;

    const std::vector<int> &poly1 = polygons[em.p1];
    const std::vector<int> &poly2 = polygons[em.p2];

    int i1 = -1;
    for (int i = 0; i < (int)poly1.size(); i++) {
      if (poly1[i] == em.u && poly1[(i + 1) % poly1.size()] == em.v) {
        i1 = i;
        break;
      }
    }
    int i2 = -1;
    for (int i = 0; i < (int)poly2.size(); i++) {
      if (poly2[i] == em.v && poly2[(i + 1) % poly2.size()] == em.u) {
        i2 = i;
        break;
      }
    }

    if (i1 == -1 || i2 == -1) continue;

    int new_size = (int)poly1.size() + (int)poly2.size() - 2;
    if (new_size > max_vertices) continue;

    int a = poly1[(i1 + poly1.size() - 1) % poly1.size()];
    int b = poly1[(i1 + 2) % poly1.size()];
    int c = poly2[(i2 + poly2.size() - 1) % poly2.size()];
    int d = poly2[(i2 + 2) % poly2.size()];

    // Only the angles at the joined vertices change; verify they
    // remain valid.
    if (!CheckAngle(a, em.u, d)) continue;
    if (!CheckAngle(c, em.v, b)) continue;

    std::vector<int> merged;
    merged.reserve(new_size);
    for (int i = 1; i <= (int)poly1.size(); i++) {
      merged.push_back(poly1[(i1 + i) % poly1.size()]);
    }
    for (int i = 2; i < (int)poly2.size(); i++) {
      merged.push_back(poly2[(i2 + i) % poly2.size()]);
    }

    active[em.p1] = false;
    active[em.p2] = false;

    int p_new = (int)polygons.size();
    polygons.push_back(std::move(merged));
    active.push_back(true);

    const std::vector<int> &new_poly = polygons[p_new];
    for (int i = 0; i < (int)new_poly.size(); i++) {
      int u = new_poly[i];
      int v = new_poly[(i + 1) % new_poly.size()];

      edge_to_poly[MakeEdge(u, v)] = p_new;

      std::unordered_map<uint64_t, int>::iterator it_rev =
          edge_to_poly.find(MakeEdge(v, u));
      if (it_rev != edge_to_poly.end()) {
        int p_neighbor = it_rev->second;
        if (active[p_neighbor]) {
          vec2 diff = mesh.vertices[u] - mesh.vertices[v];
          double score = diff.x * diff.x + diff.y * diff.y;
          pq.push({p_new, p_neighbor, u, v, score});
        }
      }
    }
  }

  for (size_t i = 0; i < polygons.size(); i++) {
    if (active[i]) {
      mesh.polygons.push_back(std::move(polygons[i]));
    }
  }

  return mesh;
}


