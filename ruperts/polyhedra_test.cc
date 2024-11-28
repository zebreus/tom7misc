
#include "polyhedra.h"

#include <cstdio>
#include <vector>

#include "arcfour.h"
#include "randutil.h"
#include "ansi.h"
#include "yocto_matht.h"

static constexpr bool VERBOSE = false;

static void TestQuickHull() {
  {
    std::vector<vec2> square = {
      vec2(1.0, 1.0),
      vec2(-1.0, 1.0),
      vec2(-1.0, -1.0),
      vec2(1.0, -1.0),
    };

    std::vector<int> hull = QuickHull(square);
    CHECK(hull.size() == 4);
  }

  {
    std::vector<vec2> degenerate_triangle = {
      vec2(-5.3333333333333333333333333,-2.666666666666666666666666666),
      vec2(-5.3333333333333333333333333,-2.666666666666666666666666666),
      vec2(-3.6666666666666666666666666,-5.333333333333333333333333333),
    };
    std::vector<int> hull = QuickHull(degenerate_triangle);
    CHECK(hull.size() == 2);
  }

  {
    ArcFour rc("hi");
    for (int num_pts = 3; num_pts < 16; num_pts++) {
      for (int i = 0; i < num_pts * num_pts * num_pts; i++) {
        std::vector<vec2> v;
        for (int j = 0; j < num_pts; j++) {
          v.push_back(vec2{
              .x = (double)RandTo(&rc, num_pts * 4) / num_pts - num_pts * 2,
              .y = (double)RandTo(&rc, num_pts * 4) / num_pts - num_pts * 2,
            });
          if (VERBOSE) {
            printf("Test:");
            for (const vec2 &x : v) {
              printf(" %s", VecString(x).c_str());
            }
            printf("\n");
          }

          std::vector<int> hull = QuickHull(v);
          // XXX check properties of hull
          if (VERBOSE) {
            printf("Hull size: %d\n", (int)hull.size());
          }
        }
      }
    }
  }

}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("\n");

  TestQuickHull();

  printf("OK\n");
  return 0;
}
