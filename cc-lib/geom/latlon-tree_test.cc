
#include "latlon-tree.h"

#include "geom/latlon.h"
#include "base/logging.h"
#include "arcfour.h"
#include "randutil.h"

using namespace std;

static void Instantiate() {
  internal::LLKDTree<int> llkd_int;
}

static void InsertApp() {
  ArcFour rc("insert-app");
  vector<pair<LatLon, bool>> points;

  LatLonTree<int> latlontree;

  // Insert it with its data being its index
  // in the points vector.
  for (int i = 0; i < 10000; i++) {
    LatLon ll = LatLon::FromDegs(
        // Avoid poles
        0.0 + RandDouble(&rc) * 50.0,
        -180.0 + RandDouble(&rc) * 360.0);
    latlontree.Insert(ll, (int)points.size());
    points.emplace_back(ll, false);
  }

  printf("Insert ok\n");

  // Find each node exactly once.
  latlontree.App([&points](LatLon ll, int idx) {
      CHECK(idx >= 0 && idx < points.size());
      CHECK(!points[idx].second) << idx;
      points[idx].second = true;
    });

  for (const auto &[ll, found] : points) {
    CHECK(found);
  }

  for (auto &p : points) p.second = false;

  printf("Applied to all points ok\n");

  // Find every point by radius search with a huge
  // radius.
  vector<pair<LatLon, int>> res = latlontree.Lookup(
      LatLon::FromDegs(40.0, -80.0),
      // 25000 miles
      40233600.0);

  for (const auto &[ll, idx] : res) {
    CHECK(idx >= 0 && idx < points.size());
    CHECK(!points[idx].second) << idx;
    points[idx].second = true;
  }

  for (const auto &[ll, found] : points) {
    CHECK(found);
  }

  printf("Universal radius search ok\n");
}

int main(int argc, char **argv) {
  Instantiate();

  InsertApp();
  // XXX tests!

  printf("OK\n");

  return 0;
}
