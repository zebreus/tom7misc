
#include "ruperts-util.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "bounds.h"
#include "dirty.h"
#include "geom/hull-2d.h"
#include "geom/polyhedra.h"
#include "image.h"
#include "randutil.h"
#include "rendering.h"
#include "yocto-math.h"

double IsNear(double a, double b) {
  return std::abs(a - b) < 0.0000001;
}

#define CHECK_NEAR(f, g) do {                                           \
  const double fv = (f);                                                \
  const double gv = (g);                                                \
  const double e = std::abs(fv - gv);                                   \
  CHECK(e < 0.0000001) << "Expected " << #f << " and " << #g <<         \
    " to be close, but got: " <<                                        \
    std::format("{:.17g} and {:.17g}, with err {:.17g}", fv, gv, e);   \
  } while (0)


static void TestCircle() {
  ArcFour rc("circle");

  for (int iters = 0; iters < 1000; iters++) {
    std::vector<vec2> points;
    for (int j = 0; j < 15; j++) {
      points.emplace_back(RandDouble(&rc) * 4.0 - 2.0,
                          RandDouble(&rc) * 4.0 - 2.0);
    }

    std::vector<int> hull = Hull2D::QuickHull(points);
    if (!PointInPolygon(vec2{0.0, 0.0}, points, hull)) {
      // This is a precondition, so try again.
      iters--;
      continue;
    }

    HullCircumscribedCircle circumscribed(points, hull);
    HullInscribedCircle inscribed(points, hull);

    // Test a bunch of points.
    for (int j = 0; j < 100; j++) {
      vec2 pt{
        .x = RandDouble(&rc) * 4.0 - 2.0,
        .y = RandDouble(&rc) * 4.0 - 2.0
      };

      if (circumscribed.DefinitelyOutside(pt)) {
        CHECK(!PointInPolygon(pt, points, hull)) << VecString(pt);
      }

      if (inscribed.DefinitelyInside(pt)) {
        CHECK(PointInPolygon(pt, points, hull)) << VecString(pt);
      }
    }
  }
}

static void TestUnpackRot() {
  ArcFour rc("unpack");
  for (int i = 0; i < 1000; i++) {
    const quat4 qi = RandomQuaternion(&rc);
    const frame3 frame = yocto::rotation_frame(qi);
    quat4 qo;
    vec3 trans;
    std::tie(qo, trans) = UnpackFrame(frame);
    CHECK_NEAR(trans.x, 0.0);
    CHECK_NEAR(trans.y, 0.0);
    CHECK_NEAR(trans.z, 0.0);

    /*
    Print("{}\n ** to **\n {}\n",
          QuatString(qi),
          QuatString(qo));
    */

    // the quaternion could either be qi or -qi.
    if (IsNear(qi.x, -qo.x) &&
        IsNear(qi.y, -qo.y) &&
        IsNear(qi.z, -qo.z) &&
        IsNear(qi.w, -qo.w)) {
      qo = quat4{.x = -qo.x, .y = -qo.y, .z = -qo.z, .w = -qo.w};
    }

    CHECK_NEAR(qi.x, qo.x);
    CHECK_NEAR(qi.y, qo.y);
    CHECK_NEAR(qi.z, qo.z);
    CHECK_NEAR(qi.w, qo.w);
  }
}

static void TestUnpackFull() {
  ArcFour rc("unpack");
  for (int i = 0; i < 1000; i++) {
    const quat4 qi = RandomQuaternion(&rc);

    const vec3 ti = vec3(RandDouble(&rc) * 2.0 - 1.0,
                         RandDouble(&rc) * 2.0 - 1.0,
                         RandDouble(&rc) * 2.0 - 1.0);

    const frame3 frame = yocto::translation_frame(ti) *
      yocto::rotation_frame(qi);
    quat4 qo;
    vec3 trans;
    std::tie(qo, trans) = UnpackFrame(frame);
    CHECK_NEAR(trans.x, ti.x);
    CHECK_NEAR(trans.y, ti.y);
    CHECK_NEAR(trans.z, ti.z);

    // the quaternion could either be qi or -qi.
    if (IsNear(qi.x, -qo.x) &&
        IsNear(qi.y, -qo.y) &&
        IsNear(qi.z, -qo.z) &&
        IsNear(qi.w, -qo.w)) {
      qo = quat4{.x = -qo.x, .y = -qo.y, .z = -qo.z, .w = -qo.w};
    }

    CHECK_NEAR(qi.x, qo.x);
    CHECK_NEAR(qi.y, qo.y);
    CHECK_NEAR(qi.z, qo.z);
    CHECK_NEAR(qi.w, qo.w);
  }
}

// Tests a bug with the loss function that assumed the origin was
// in the polyhedron (thx dwrensha).
static void TestLossRegression() {

  const Polyhedron poly = []() {
      auto opoly = PolyhedronFromConvexVertices(
          std::vector<vec3>{
            {-0.7888254090932487,0.1809666339064286,0.5873717318542367},
            {-0.7275239611943676,-0.5176379718006908,-0.45028859194756815},
            {-0.46761652842176743,-8.458653528836922e-05,0.8839314312727501},
            {0.012255643227897352,0.6097001740588195,0.7925373789050683},
            {-0.39543790974206744,-0.460739697734118,0.7945739679039946},
            {-0.6113020439400112,0.2438926374222257,-0.752878604083019}
          });

      CHECK(opoly.has_value());
      return opoly.value();
    }();

  frame3 outer_frame =
    frame3{vec3{-0.28122289782544885,0.8771668061830272,-0.3892198297483371},
           vec3{0.9013115757915298,0.10217934077188853,-0.4209475331482672},
           vec3{-0.3294709776032578,-0.46918842318421455,-0.8193357666226122},
           vec3{0,0,0}};
  frame3 inner_frame =
    frame3{vec3{-0.9900711776109041,0.10560397200855935,0.09277318772238792},
           vec3{-0.10077001875482698,-0.9933693480580881,0.05534204241624576},
           vec3{0.09800238050328079,0.04544380523971492,0.9941480744743338},
           vec3{-0.4725374179836337,-0.20260639570538497,0}};

  double loss = LossFunction(poly, outer_frame, inner_frame);
  CHECK(loss > 0.0);

  std::optional<double> oclearance =
    GetClearance(poly, outer_frame, inner_frame);
  CHECK(!oclearance.has_value()) << oclearance.value();
}

int main(int argc, char **argv) {
  ANSI::Init();
  Print("\n");

  TestCircle();

  TestUnpackRot();
  TestUnpackFull();

  TestLossRegression();

  Print("OK\n");
  return 0;
}
