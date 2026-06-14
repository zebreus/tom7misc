
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include <memory>

#include "timer.h"
#include "font-problem.h"

#include "image.h"
#include "lines.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "gtl/top_n.h"

using namespace std;

static constexpr FontProblem::SDFConfig SDF_CONFIG = {};

using uint8 = uint8_t;
using uint32 = uint32_t;
using int64 = int64_t;

constexpr int WIDTH = 1920/2;
constexpr int HEIGHT = 1080/2;

struct Obj {
  double x = 0.0f;
  double y = 0.0f;
  double z = 0.0f;
  double dz = 0.0f;
  uint32 color = 0xFFFFFFFF;
  string s;
  vector<int> friends;

  std::pair<int, int> ScreenPos() const {
    float a = atan2f(y, x);
    // center of image at 0,0
    // z = 0 is "far"
    float dx = z * cosf(a);
    float dy = z * sinf(a);
    float xx = x + dx;
    float yy = y + dy;

    return std::make_pair(xx + WIDTH/2.0f, yy + HEIGHT/2.0f);
  }
};


#if 0
int main(int argc, char **argv) {
  ArcFour rc("cyberspace");

  constexpr int FRAMES = 13 * 60;
  constexpr int NUM_OBJS = 2000;
  // positive speeds mean moving towards us
  constexpr double MIN_SPEED = 0.1;
  constexpr double MAX_SPEED = 5.0;

  constexpr double MIN_DEPTH = 100;
  constexpr double VIEW_PLANE = 500;

  auto Reset = [&rc](Obj *obj) {
      uint32 color = ((uint32)rc.Byte() << 24) |
        ((uint32)rc.Byte() << 16) |
        ((uint32)rc.Byte() << 8) | 0xAA;

      obj->x = RandDouble(&rc) * WIDTH - WIDTH/2;
      obj->y = RandDouble(&rc) * HEIGHT - HEIGHT/2;
      obj->z = RandDouble(&rc) * MIN_DEPTH;
      obj->dz = MIN_SPEED + RandDouble(&rc) * MAX_SPEED;
      obj->color = color;
      if (rc.Byte() < 7) {
        obj->s = "cyberspace";
      } else {
        obj->s = (rc.Byte() & 1) ? "1" : "0";
      }
    };

  vector<Obj> objs;
  for (int i = 0; i < NUM_OBJS; i++) {
    Obj obj;
    Reset(&obj);
    objs.push_back(obj);
  }

  for (int i = 0; i < FRAMES; i++) {
    ImageRGBA img(WIDTH, HEIGHT);
    img.Clear32(0x000000FF);

    for (Obj &obj : objs) {
      obj.z += obj.dz;

      auto [sx, sy] = obj.ScreenPos();
      if (sx < -20 || sy < -20 || sx > WIDTH || sy > HEIGHT) {
        Reset(&obj);
      }
    }

    std::sort(objs.begin(), objs.end(),
              [](const Obj &a, const Obj &b) {
                return a.z < b.z;
              });

    for (int o = 0; o < objs.size(); o++) {
      auto Dist = [&objs, o](int a) -> float {
          if (a == o) return 9999999.0f;
          float dx = objs[a].x - objs[o].x;
          float dy = objs[a].y - objs[o].y;
          float dz = objs[a].z - objs[o].z;
          return dx * dx + dy * dy + dz * dz;
        };

      auto Greater = [&Dist](int a, int b) {
          return Dist(a) < Dist(b);
        };
      // Nearest neighbors. Slow without any spatial
      // data structures!
      gtl::TopN<int, decltype(Greater)> tops(12, Greater);
      for (int b = 0; b < objs.size(); b++) {
        if (b != o) tops.push(b);
      }

      // Draw lines between them.
      // Could skip symmetric lines?
      std::unique_ptr<vector<int>> topv(tops.ExtractUnsorted());
      const auto [sx, sy] = objs[o].ScreenPos();

      for (int oi = 0; oi < topv->size(); oi++) {
        uint8 alpha = 128 * (1.0f - oi / (float)(topv->size() + 1));
        const auto [dx, dy] = objs[(*topv)[oi]].ScreenPos();
        img.BlendLineAA32(sx, sy, dx, dy, 0x3FFF3F00 | alpha);
      }
    }

    // draw text glow
    for (Obj &obj : objs) {
      uint32 color = obj.color;
      const auto [x, y] = obj.ScreenPos();
      for (int dx : {-2, -1, 0, 1, 2}) {
        for (int dy : {-2, -1, 0, 1, 2}) {
          if (dx != 0 || dy != 0) {
            img.BlendText2x32(x - 9 + dx, y - 9 + dy, 0x00000030, obj.s);
          }
        }
      }
    }

    for (Obj &obj : objs) {
      uint32 color = obj.color;
      const auto [x, y] = obj.ScreenPos();
      img.BlendText2x32(x - 9, y - 9, color, obj.s);
    }


    img.Save(StringPrintf("cyberspace/frame%d.png", i));

    if (i % 25 == 0) printf("%d/%d\n", i, FRAMES);
  }

  return 0;
}
#endif


int main(int argc, char **argv) {
  ArcFour rc("cyberspace");

  constexpr int FRAMES = 13 * 60;
  constexpr int NUM_OBJS = 1000;
  // positive speeds mean moving towards us
  constexpr double MIN_SPEED = 0.1;
  constexpr double MAX_SPEED = 8.0;

  constexpr double MIN_DEPTH = 100;
  [[maybe_unused]]
  constexpr double VIEW_PLANE = 500;

  auto Reset = [&rc](Obj *obj) {
      uint32 color = ((uint32)rc.Byte() << 24) |
        ((uint32)rc.Byte() << 16) |
        ((uint32)rc.Byte() << 8) | 0xAA;

      obj->x = RandDouble(&rc) * WIDTH - WIDTH/2.0f;
      obj->y = RandDouble(&rc) * HEIGHT - HEIGHT/2.0f;
      obj->z = RandDouble(&rc) * MIN_DEPTH;
      obj->dz = MIN_SPEED + RandDouble(&rc) * MAX_SPEED;
      obj->color = color;
      if (rc.Byte() < 7) {
        obj->s = "cyberspace";
      } else {
        obj->s = (rc.Byte() & 1) ? "1" : "0";
      }
    };

  vector<Obj> objs;
  vector<int> byz;
  for (int i = 0; i < NUM_OBJS; i++) {
    Obj obj;
    Reset(&obj);
    objs.push_back(obj);
    byz.push_back(i);
  }

  // This has got to be buggy, but I don't see it?
  auto MakeFriends = [&objs](int o) {
      auto Dist = [&objs, o](int a) -> float {
          if (a == o) return 9999999.0f;
          #if 0
          float dx = objs[a].x - objs[o].x;
          float dy = objs[a].y - objs[o].y;
          float dz = objs[a].z - objs[o].z;
          return dx * dx + dy * dy + dz * dz;
          #else
          auto [ax, ay] = objs[a].ScreenPos();
          auto [ox, oy] = objs[o].ScreenPos();
          int dx = ax - ox;
          int dy = ay - oy;
          return dx * dx + dy + dy;
          #endif
        };

      auto Greater = [&Dist](int a, int b) {
          return Dist(a) < Dist(b);
        };
      // Nearest neighbors. Slow without any spatial
      // data structures!
      gtl::TopN<int, decltype(Greater)> tops(6, Greater);
      for (int b = 0; b < objs.size(); b++) {
        if (b != o) tops.push(b);
      }

      // Draw lines between them.
      // Could skip symmetric lines?
      std::unique_ptr<vector<int>> topv(tops.ExtractUnsorted());
      objs[o].friends = *topv;
    };

  for (int i = 0; i < NUM_OBJS; i++) {
    MakeFriends(i);
  }

  for (int i = 0; i < FRAMES; i++) {
    ImageRGBA img(WIDTH, HEIGHT);
    img.Clear32(0x000000FF);

    for (int oi = 0; oi < objs.size(); oi++) {
      Obj &obj = objs[oi];
      obj.z += obj.dz;

      auto [sx, sy] = obj.ScreenPos();
      if (sx < -200 || sy < -200 || sx > WIDTH + 200 || sy > HEIGHT + 200) {
        Reset(&obj);
        MakeFriends(oi);
      }
    }

    std::sort(byz.begin(), byz.end(),
              [&objs](int a, int b) {
                return objs[a].z < objs[b].z;
              });

    // draw friendlines
    for (int oi : byz) {
      const Obj &obj = objs[oi];
      const auto [sx, sy] = objs[oi].ScreenPos();
      for (int fi : obj.friends) {
        // uint8 alpha = 128 * (1.0f - oi / (float)(topv->size() + 1));
        const auto [dx, dy] = objs[fi].ScreenPos();
        img.BlendLineAA32(sx, sy, dx, dy, 0x3FFF3F47);
      }
    }

    #if 0
    // draw text glow
    for (int oi : byz) {
      const Obj &obj = objs[oi];
      uint32 color = obj.color;
      const auto [x, y] = obj.ScreenPos();
      for (int dx : {-2, -1, 0, 1, 2}) {
        for (int dy : {-2, -1, 0, 1, 2}) {
          if (dx != 0 || dy != 0) {
            img.BlendText2x32(x - 9 + dx, y - 9 + dy, 0x00000030, obj.s);
          }
        }
      }
    }
#endif

    for (int oi : byz) {
      const Obj &obj = objs[oi];
      uint32 color = obj.color;
      const auto [x, y] = obj.ScreenPos();
      for (int dx : {-2, -1, 0, 1, 2}) {
        for (int dy : {-2, -1, 0, 1, 2}) {
          if (dx != 0 || dy != 0) {
            img.BlendText2x32(x - 9 + dx, y - 9 + dy, 0x00000030, obj.s);
          }
        }
      }

      img.BlendText2x32(x - 9, y - 9, color, obj.s);
    }


    img.ScaleBy(2).Save(StringPrintf("cyberspace/frame%d.png", i));

    if (i % 25 == 0) printf("%d/%d\n", i, FRAMES);
  }

  return 0;
}

