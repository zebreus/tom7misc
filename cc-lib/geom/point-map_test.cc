
#include "point-map.h"

#include <optional>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestPointMap3() {
  PointMap3<int> map(0.5);
  using vec3 = PointMap3<int>::vec3;

  vec3 pt1{0.0, 0.0, 0.0};
  vec3 pt2{1.0, 0.0, 0.0};
  vec3 pt3{0.2, 0.0, 0.0};

  CHECK(map.Size() == 0);

  map.Add(pt1, 42);
  CHECK(map.Size() == 1);
  CHECK(map.Contains(pt1));
  CHECK(!map.Contains(pt2));
  CHECK(map.Contains(pt3));

  std::optional<int> val1 = map.Get(pt1);
  CHECK(val1.has_value());
  CHECK(val1.value() == 42);

  std::optional<int> val2 = map.Get(pt2);
  CHECK(!val2.has_value());

  map.Add(pt2, 100);
  CHECK(map.Size() == 2);

  std::vector<vec3> pts = map.Points();
  CHECK(pts.size() == 2);
}

static void TestPointMap2() {
  PointMap2<int> map(0.5);
  using vec2 = PointMap2<int>::vec2;

  vec2 pt1{0.0, 0.0};
  vec2 pt2{1.0, 0.0};

  CHECK(map.Size() == 0);

  map.Add(pt1, 10);
  CHECK(map.Size() == 1);

  CHECK(map.Contains(pt1));
  CHECK(!map.Contains(pt2));

  std::optional<int> val1 = map.Get(pt1);
  CHECK(val1.has_value());
  CHECK(val1.value() == 10);
}

static void TestPointSet3() {
  PointSet3 set;
  using vec3 = PointSet3::vec3;

  vec3 pt1{0.0, 0.0, 0.0};

  CHECK(set.Size() == 0);

  set.Add(pt1);
  CHECK(set.Size() == 1);
  CHECK(set.Contains(pt1));

  std::vector<vec3> pts = set.Points();
  CHECK(pts.size() == 1);
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestPointMap3();
  TestPointMap2();
  TestPointSet3();

  Print("OK\n");
  return 0;
}

