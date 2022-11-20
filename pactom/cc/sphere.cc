
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>
#include <utility>
#include <optional>
#include <functional>
#include <cmath>
#include <numbers>

#include "yocto_matht.h"
#include "yocto_geometryt.h"
#include "threadutil.h"
#include "image.h"
#include "color-util.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "timer.h"
#include "randutil.h"
#include "arcfour.h"
#include "opt/opt.h"
#include "pactom.h"
#include "lines.h"
#include "image-frgba.h"

static constexpr double PI = std::numbers::pi;

static constexpr int THREADS = 24;

static constexpr double EARTH_RADIUS = 2.8;
static constexpr double MILES = 2.8 / 3963.19;
// static constexpr double SUN_DISTANCE = 23214.0f * EARTH_RADIUS;
static constexpr double SUN_DISTANCE = 1000.0;
static constexpr double SUN_RADIUS = 108.15 * EARTH_RADIUS;

static constexpr double FAR_DIST = 100.0;
static constexpr double FAR_WIDTH = 100.0;
// static constexpr double NEAR_WIDTH = 0.01f;
static constexpr double NEAR_WIDTH = 0.005;

// of inner earth
static constexpr double TOTAL_MILES = 800 + 1400 + 1800 + 25;
// Normalized to [0, 1].
static constexpr ColorUtil::Gradient INNER_EARTH {
  GradRGB(0.0f / TOTAL_MILES, 0xEEEECC),
  GradRGB(790.0f / TOTAL_MILES, 0xEEEECC),
  GradRGB(801.0f / TOTAL_MILES, 0xf7dd24),
  GradRGB(2200.0f / TOTAL_MILES, 0xf7dd24),
  GradRGB(2201.0f / TOTAL_MILES, 0xf7dd24),
  GradRGB(4000.0f / TOTAL_MILES, 0xcf1857),
  GradRGB(4001.0f / TOTAL_MILES, 0x724719),
  GradRGB(4024.0f / TOTAL_MILES, 0x724719),
  GradRGB(1.0f, 0x046374),
};

// Made up
/*
static constexpr ColorUtil::Gradient ATMOSPHERE {
  GradRGB(0.0f, 0xe7181c),
  GradRGB(0.3f, 0x7a3a33),
  // GradRGB(0.9f, 0x595120),
  GradRGB(0.9f, 0x00FF00),
  GradRGB(0.975f, 0x8b7ace),
  // GradRGB(1.0f, 0x8bc0ff),
  GradRGB(0x985f, 0x0000FF),
  GradRGB(0.99f, 0x440cff),
  GradRGB(1.0f, 0x6600ff),
};
*/

// Made up. From outside (0.0) to inside (1.0).
static constexpr ColorUtil::Gradient ATMOSPHERE {
  GradRGB(0.0f, 0x7700FF),
  GradRGB(0.15f, 0xFF77FF),
  GradRGB(0.25f, 0xffa21e),
  GradRGB(1.0f, 0x000000),
};


/*
  GradRGB(0.0f, 0xe7e8ec),
  GradRGB(0.1f, 0x7a9ac3),
  GradRGB(0.35f, 0x5991e0),
  GradRGB(0.75f, 0x8b9aee),
  GradRGB(1.0f, 0x8bc0ff),
*/

using namespace yocto;

PacTom *pactom = nullptr;

struct Convert {
  // uv coordinates here are given in terms of the earth
  // texture map image (already flipped horizontally).

  // Wellsville
  static constexpr double xu0 = 11921.0 / (21600.0 * 2.0);
  static constexpr double yu0 = 5928.0 / 21600.0;

  static constexpr double lat0 = 40.600658;
  static constexpr double lon0 = -80.646501;

  // Confluence
  static constexpr double xu1 = 11999.0 / (21600.0 * 2.0);
  static constexpr double yu1 = 5947.0 / 21600.0;
  static constexpr double lat1 = 40.442487;
  static constexpr double lon1 = -80.015946;

  static constexpr double lattoy = (yu1 - yu0) / (lat1 - lat0);
  static constexpr double lontox = (xu1 - xu0) / (lon1 - lon0);

  static constexpr double ytolat = (lat1 - lat0) / (yu1 - yu0);
  static constexpr double xtolon = (lon1 - lon0) / (xu1 - xu0);

  // Near Pittsburgh. Assumes world is flat, which is of
  // course not true. Remember lat,lon is y,x.
  static constexpr std::pair<double, double> ToUV(double lat, double lon) {
    return make_pair((lon - lon0) * lontox + xu0,
                     (lat - lat0) * lattoy + yu0);
  }

  static constexpr std::pair<double, double> ToLatLon(double u, double v) {
    return make_pair((u - xu0) * xtolon + lon0,
                     (v - yu0) * ytolat + lat0);
  }
};

static ImageRGBA *bluemarble = nullptr;
// static ImageRGBA *stars = nullptr;
static ImageFRGBA *stars = nullptr;

using mat3d = yocto::mat<double, 3>;
using vec3d = yocto::vec<double, 3>;
using ray3d = yocto::ray<double, 3>;
using frame3d = yocto::frame<double, 3>;
using prim_isect_d = yocto::prim_intersection<double>;


mat3d RotYaw(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);
  return mat3d
    {cosa, -sina, 0.0,
     sina, cosa,  0.0,
     0.0, 0.0,  1.0};
}

mat3d RotPitch(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);

  return mat3d
    {cosa,  0.0, sina,
     0.0,  1.0, 0.0,
     -sina, 0.0, cosa};
}

mat3d RotRoll(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);

  return mat3d
    {1.0, 0.0, 0.0,
     0.0, cosa, -sina,
     0.0, sina, cosa};
}

mat3d Rot(double yaw, double pitch, double roll) {
  mat3d mr = RotRoll(roll);
  mat3d mp = RotPitch(pitch);
  mat3d my = RotYaw(yaw);
  mat3d m = mp * my;
  mat3d n = mr * m;
  return n;
}

struct Tetrahedron {
  vec3d p0, p1, p2, p3;
};

// TODO: Doesn't tell us which face it hit; same UV coordinates no matter
// what.
inline prim_isect_d intersect_tetrahedron(
    const ray3d& ray, const Tetrahedron &tet) {

  //    1---3
  //   / \ /
  //  0---2

  // TODO: These all have the same winding order, but probably we want
  // to pick them so that they have consistent UV coordinates or something.
  prim_isect_d p0 = intersect_triangle(ray, tet.p0, tet.p1, tet.p2);
  prim_isect_d p1 = intersect_triangle(ray, tet.p1, tet.p3, tet.p2);
  prim_isect_d p2 = intersect_triangle(ray, tet.p0, tet.p3, tet.p1);
  prim_isect_d p3 = intersect_triangle(ray, tet.p2, tet.p3, tet.p0);

  // take the closest one
  for (const auto &p : {p1, p2, p3}) {
    if (p.hit && p.distance < p0.distance) p0 = p;
  }

  return p0;
}

struct Sphere {
  vec3d origin = {0.0f, 0.0f, 0.0f};
  double radius = 0.0f;
};

struct Prim {
  std::variant<Sphere, Tetrahedron> v;
};

// From yocto_geometry.h, but fixing a bug (?) where the UV coordinates
// are always from the back of the sphere
inline prim_isect_d intersect_sphere_front(
    const ray3d& ray, const vec3d& p, double r) {
  // compute parameters
  auto a = dot(ray.d, ray.d);
  auto b = 2 * dot(ray.o - p, ray.d);
  auto c = dot(ray.o - p, ray.o - p) - r * r;

  // check discriminant
  auto dis = b * b - 4 * a * c;
  if (dis < 0) return {};

  auto MaybeIntersect = [&](double t) -> prim_isect_d {
      prim_isect_d isect;
      isect.hit = false;
      if (t < ray.tmin || t > ray.tmax) return isect;

      // compute local point for uvs
      auto plocal = ((ray.o + ray.d * t) - p) / r;
      auto u      = atan2(plocal.y, plocal.x) / (2 * PI);
      if (u < 0) u += 1;
      auto v = acos(clamp(plocal.z, -1.0, 1.0)) / PI;

      // intersect front
      isect.hit = true;
      isect.uv.x = u;
      isect.uv.y = v;
      isect.distance = t;
      return isect;
    };

  prim_isect_d front = MaybeIntersect((-b - sqrt(dis)) / (2 * a));
  if (front.hit) return front;

  return MaybeIntersect((-b + sqrt(dis)) / (2 * a));
}

double OptimizeMe(double a1, double a2, double a3, double distance) {
  const double frame_width = 1920;
  const double frame_height = 1080;
  const double aspect = frame_width / (double)frame_height;

  const double near_height = NEAR_WIDTH / aspect;
  const double far_height = FAR_WIDTH / aspect;

  Sphere earth;
  earth.radius = EARTH_RADIUS;
  earth.origin = {0.0f, 0.0f, 0.0f};

  double TEX_WIDTH = 21600 * 2;
  double TEX_HEIGHT = 21600;
  double ux = 11973.0 / TEX_WIDTH;
  double uy = 5930.0 / TEX_HEIGHT;
  double uw = 65.0 / TEX_WIDTH;
  double uh = 37.0 / TEX_HEIGHT;

  auto GetUV = [&](double xf, double yf) ->
    std::optional<std::pair<float, float>> {
      double far_dist = FAR_DIST;
      double near_dist = -distance;
      double near_x = std::lerp(-NEAR_WIDTH * 0.5, NEAR_WIDTH * 0.5, xf);
      double near_y = std::lerp(-near_height * 0.5, near_height * 0.5, yf);

      double far_x = std::lerp(-FAR_WIDTH * 0.5, FAR_WIDTH * 0.5, xf);
      double far_y = std::lerp(-far_height * 0.5, far_height * 0.5, yf);

      vec3d near_pt = {near_x, near_y, -near_dist};
      vec3d far_pt = {far_x, far_y, -far_dist};

      ray3d ray;
      ray.o = near_pt;
      ray.d = far_pt - near_pt;
      ray.tmin = 0.000000001f;
      ray.tmax = 10.0f;

      mat3d rot = Rot(a1, a2, a3);
      frame3d rot_frame = make_frame(rot, {0.0, 0.0, 0.0});
      ray = transform_ray(rot_frame, ray);

      prim_isect_d pi = intersect_sphere_front(
          ray, earth.origin, earth.radius);

      if (!pi.hit) return std::nullopt;
      return {make_pair(1.0f - pi.uv.x, pi.uv.y)};
    };

  auto pi00 = GetUV(0.0f, 0.0f);
  if (!pi00.has_value()) return 9999999.0f;

  auto pi10 = GetUV(1.0f, 0.0f);
  if (!pi10.has_value()) return 9999999.0f;

  auto pi01 = GetUV(0.0f, 1.0f);
  if (!pi01.has_value()) return 9999999.0f;

  auto pi11 = GetUV(1.0f, 1.0f);
  if (!pi11.has_value()) return 9999999.0f;

  auto [ux00, uy00] = pi00.value();
  auto [ux10, uy10] = pi10.value();
  auto [ux01, uy01] = pi01.value();
  auto [ux11, uy11] = pi11.value();

  auto SqDist = [](double x0, double y0,
                   double x1, double y1) {
      double xx = x0 - x1;
      double yy = y0 - y1;
      return xx * xx + yy * yy;
    };

  return
    SqDist(ux00, uy00, ux, uy) +
    SqDist(ux10, uy10, ux + uw, uy) +
    SqDist(ux01, uy01, ux, uy + uh) +
    SqDist(ux11, uy11, ux + uw, uy + uh);
}

static std::tuple<double, double, double, double> Optimize() {
  static constexpr double d = 2.801;
  auto [a1, a2, a3] =
    Opt::Minimize3D([](double a1, double a2, double a3) {
        return OptimizeMe(a1, a2, a3, d);
      },
    std::make_tuple(0.0, 0.0, 0.0),
    std::make_tuple(2 * PI, 2 * PI, 2 * PI),
    10000, 1, 1000).first;
  printf("Best:\n"
         "const double a1 = %.11f;\n"
         "const double a2 = %.11f;\n"
         "const double a3 = %.11f;\n"
         "const double distance = %.11f\n",
         a1, a2, a3, d);

  return std::make_tuple(a1, a2, a3, d);
}

struct Scene {
  std::vector<Prim> prims;

  // Could do an actual 3D rotation here, but the distance is made up
  // anyway.
# define EARTH_TILT_RAD 0.41f
# define SUN_ANGLE (2.0)
  Sphere sun = {.origin = {double(SUN_DISTANCE * sin(EARTH_TILT_RAD)),
                           double(SUN_DISTANCE * sin(SUN_ANGLE)),
                           double(SUN_DISTANCE * cos(SUN_ANGLE))},
                .radius = SUN_RADIUS};

  std::vector<std::pair<int, prim_isect_d>> AllIntersections(
      const ray3d &ray_in) const {
    ray3d ray = ray_in;
    std::vector<std::pair<int, prim_isect_d>> hits;
    for (;;) {
      auto p = NextIntersection(&ray);
      if (!p.second.hit) return hits;
      else hits.push_back(p);
    }
  }

  // Updates ray with distance of next intersection (if any).
  std::pair<int, prim_isect_d> NextIntersection(
      ray3d *ray) const {
    // Obviously this should use spatial data structures if the
    // scene is big!
    int isect_idx = -1;
    prim_isect_d isect;
    isect.distance = flt_max;
    isect.hit = false;
    for (int idx = 0; idx < prims.size(); idx++) {
      const Prim &p = prims[idx];
      if (const Sphere *sphere = std::get_if<Sphere>(&p.v)) {
        prim_isect_d pi =
          intersect_sphere_front(*ray, sphere->origin, sphere->radius);
        if (pi.hit && pi.distance < isect.distance) {
          isect = pi;
          isect_idx = idx;
        }

      } else if (const Tetrahedron *tet = std::get_if<Tetrahedron>(&p.v)) {
        prim_isect_d pi =
          intersect_tetrahedron(*ray, *tet);
        if (pi.hit && pi.distance < isect.distance) {
          isect = pi;
          isect_idx = idx;
        }

      } else {
        CHECK(false) << "Unknown prim??";
      }
    }

    // XXX some more principled epsilon; nextafter?
    if (isect.hit) ray->tmin = isect.distance + 0.000001;
    return make_pair(isect_idx, isect);
  }
};

struct Tile {
  // old
  // static constexpr double lat0 = 40.576817;
  // static constexpr double lon0 = -80.179213;
  // static constexpr double lat1 = 40.293648;
  // static constexpr double lon1 = -79.515399;

  static constexpr double lat0 = 40.577355;
  static constexpr double lon0 = -80.183547;

  static constexpr double lat1 = 40.289646;
  static constexpr double lon1 = -79.516107;

  static constexpr auto tile_top = Convert::ToUV(lat0, lon0);
  static constexpr auto tile_bot = Convert::ToUV(lat1, lon1);

  LatLon::Projection proj;

  // TODO: shift colors to match marble image
  Tile() {
    image.reset(ImageRGBA::Load("tile-recolored.png"));
    CHECK(image.get() != nullptr);
    proj = LatLon::Linear(LatLon::FromDegs(lat0, lon0),
                          LatLon::FromDegs(lat1, lon1));
  }

  std::pair<double, double> ToXY(LatLon ll) {
    const auto [fx, fy] = proj(ll);
    return std::make_pair(fx * image->Width(),
                          fy * image->Height());
  }

  void Drawhoods(PacTom *pactom) {
    static constexpr int RADIUS = 4;
    CHECK(pactom != nullptr);

    for (const auto &[name, path] : pactom->hoods) {
      constexpr uint32 color = 0xFFFFAA;
      for (int i = 0; i < path.size() - 1; i++) {
        const LatLon latlon0 = path[i];
        const LatLon latlon1 = path[i + 1];
        auto [x0, y0] = ToXY(latlon0);
        auto [x1, y1] = ToXY(latlon1);

        for (const auto [x, y] :
               Line<int>{(int)x0, (int)y0, (int)x1, (int)y1}) {
          for (int dy = -RADIUS; dy <= RADIUS; dy++) {
            const int ddy = dy * dy;
            for (int dx = -RADIUS; dx <= RADIUS; dx++) {
              const int ddx = dx * dx;
              if (ddy + ddx <= RADIUS * RADIUS) {
                image->BlendPixel32(x + dx, y + dy, color);
              }
            }
          }
        }

      }
    }

  }

  std::unique_ptr<ImageRGBA> image;
};

Tile *tile = nullptr;

// distance is distance from camera to surface
uint32_t EarthColor(double ux, double uy, double distance) {

  auto In = [ux, uy](std::pair<double, double> a,
                     std::pair<double, double> b) ->
    std::optional<std::pair<double, double>> {

    auto InOrder = [](double a, double b, double c) {
        return a < c ? (a <= b && b < c) : (c <= b && b < a);
      };

    if (InOrder(a.first, ux, b.first) &&
        InOrder(a.second, uy, b.second)) {
        return std::make_pair((ux - a.first) / (b.first - a.first),
                              (uy - a.second) / (b.second - a.second));
      } else {
        return std::nullopt;
      }
    };

  float r, g, b, a_;
  std::tie(r, g, b, a_) = bluemarble->SampleBilinear(
      ux * bluemarble->Width(),
      uy * bluemarble->Height());

  // TODO: Fade towards edges of tile.
  if (auto po = In(Tile::tile_top, Tile::tile_bot)) {
    auto [x, y] = po.value();
    const auto [rr, gg, bb, a_] = tile->image->SampleBilinear(
        // XXX maybe don't bother scaling down?
        x * tile->image->Width(),
        y * tile->image->Height());

    // Fade out when we get far away.
    static constexpr double distance_close = 0.00015;
    static constexpr double distance_far = 0.00463;

    float mix_frac = distance < distance_close ? 1.0f :
                                distance >= distance_far ? 0.0f :
      (1.0f - ((distance - distance_close) / (distance_far - distance_close)));

    r = std::lerp(r, rr, mix_frac);
    g = std::lerp(g, gg, mix_frac);
    b = std::lerp(b, bb, mix_frac);
  }

  return ColorUtil::FloatsTo32(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

template<class T>
static inline int Sign(T val) {
  return (T(0) < val) - (val < T(0));
}

static bool InTetrahedron(const vec3d &pt,
                          const Tetrahedron &tet) {
  auto SameSide = [&pt](const vec3d &v0, const vec3d &v1,
                        const vec3d &v2, const vec3d &v3) {
      vec3d normal = cross(v1 - v0, v2 - v0);
      double dot30 = dot(normal, v3 - v0);
      double dotp = dot(normal, pt - v0);
      return Sign(dot30) == Sign(dotp);
    };

  return SameSide(tet.p0, tet.p1, tet.p2, tet.p3) &&
    SameSide(tet.p1, tet.p2, tet.p3, tet.p0) &&
    SameSide(tet.p2, tet.p3, tet.p0, tet.p1) &&
    SameSide(tet.p3, tet.p0, tet.p1, tet.p2);
}

static bool InSphere(const vec3d &pt,
                     const Sphere &sphere) {
  return length(pt - sphere.origin) < sphere.radius;
}

// 0.0f = fully shadow
inline static float Illumination(const Scene &scene,
                                 const Tetrahedron &mouth,
                                 vec3d start) {
  // XXX
  return 1.0f;

  // TODO: Sample sun as disc
  ray3d shadow_ray;
  vec3d dir = scene.sun.origin - start;
  shadow_ray.o = start;
  shadow_ray.d = dir;
  shadow_ray.tmin = 0.00001f;
  shadow_ray.tmax = SUN_DISTANCE * 2.0f;

  const std::vector<std::pair<int, prim_isect_d>> shadow_hits =
    scene.AllIntersections(shadow_ray);

  bool s_in_mouth = InTetrahedron(shadow_ray.o, mouth);

  for (const auto &[idx, pi] : shadow_hits) {
    if (idx == 0) {
      // sphere
      if (!s_in_mouth) return 0.0f;
    } else if (idx == 1) {
      if (s_in_mouth) {
        // Exit mouth.
        // Is this the inner surface of the sphere?
        vec3d pt = shadow_ray.o + pi.distance * shadow_ray.d;
        // XXX hard coded location of earth
        double r = length(pt);
        if (r <= EARTH_RADIUS) {
          return 0.0f;
        } else {
          // Exiting to free space
          s_in_mouth = false;
        }

      } else {
        // Enter mouth.
        s_in_mouth = true;
      }
    } else if (idx == 2) {
      // ignore skybox
    } else if (idx == 3) {
      // could scatter light through atmosphere?
    } else {
      CHECK(false) << "unknown prim";
    }
  }

  // reaches sun
  return 1.0f;
}

static ImageRGBA RenderFrame(
    int frame_width,
    int frame_height,
    int oversample,
    // Distance from Earth
    double distance,
    // Rotation of Earth along its axis
    double a1, double a2, double a3) {

  ImageRGBA img(frame_width * oversample, frame_height * oversample);

  // Right-handed:
  //
  //   ^ +y   -z (into scene)
  //   |    /
  //   |  /
  //   |/
  //   *-------> +x
  //  /
  // +z

  Scene scene;
  Sphere earth;
  earth.radius = EARTH_RADIUS;
  earth.origin = {0.0f, 0.0f, 0.0f};
  scene.prims.emplace_back(earth);
  vec3d tp1 = {0, -5, 0};
  vec3d tp2 = {0, +5, 0};
  vec3d tp3 = {5, 0, +5};
  vec3d tp4 = {5, 0, -5};

  // Don't just comment stuff out, as we use the indices
  // in the rendering logic!
  Tetrahedron mouth(tp1, tp2, tp3, tp4);
  scene.prims.emplace_back(mouth);

  Sphere starbox;
  starbox.radius = 90.0;
  starbox.origin = {0.0f, 0.0f, 0.0f};
  scene.prims.emplace_back(starbox);

  Sphere atmosphere;
  atmosphere.radius = EARTH_RADIUS + (9 + 31 + 53 + 372) * MILES;
  atmosphere.origin = {0.0f, 0.0f, 0.0f};
  scene.prims.emplace_back(atmosphere);

  const double aspect = frame_width / (double)frame_height;

  const double near_height = NEAR_WIDTH / aspect;
  const double far_height = FAR_WIDTH / aspect;

  bool first = true;
  ParallelComp(
      /* img.Width(), */ img.Height(),
      [&first, &mouth, &atmosphere,
       distance, a1, a2, a3, near_height, far_height,
       &img, &scene](int py) {
      double yf = py / (double)img.Height();
      for (int px = 0; px < img.Width(); px++) {
        double xf = px / (double)img.Width();

        // image plane and far plane are parallel to the
        // xy plane.
        double far_dist = FAR_DIST;
        // (maybe we want much less to zoom to a
        // region on the planet?)
        double near_dist = -distance;

        double near_x = std::lerp(-NEAR_WIDTH * 0.5, NEAR_WIDTH * 0.5, xf);
        double near_y = std::lerp(-near_height * 0.5, near_height * 0.5, yf);

        double far_x = std::lerp(-FAR_WIDTH * 0.5, FAR_WIDTH * 0.5, xf);
        double far_y = std::lerp(-far_height * 0.5, far_height * 0.5, yf);

        vec3d near_pt = {near_x, near_y, -near_dist};
        vec3d far_pt = {far_x, far_y, -far_dist};

        ray3d ray;
        ray.o = near_pt;
        ray.d = far_pt - near_pt;
        ray.tmin = 0.000000001f;
        ray.tmax = 10.0f;

        // Rotate...
        mat3d rot = Rot(a1, a2, a3);
        // Rotate around origin.
        frame3d rot_frame = make_frame(rot, {0.0, 0.0, 0.0});
        ray = transform_ray(rot_frame, ray);


        const std::vector<std::pair<int, prim_isect_d>> hits =
          scene.AllIntersections(ray);

        // Translucent elements we traced through.
        std::vector<uint32_t> blend;
        auto EmitColor = [&img, px, py, &blend](uint32_t color) {
            img.SetPixel32(px, py, color);
            for (int i = blend.size() - 1; i >= 0; i--)
              img.BlendPixel32(px, py, blend[i]);
          };

        bool in_mouth = InTetrahedron(ray.o, mouth);
        // point where we entered atmosphere
        std::optional<vec3d> in_atmosphere = InSphere(ray.o, atmosphere) ?
          std::optional<vec3d>{ray.o} : std::nullopt;

        auto Shade = [&](vec3d pt, uint32_t color) {
            float light = std::clamp(Illumination(scene, mouth, pt),
                                     0.25f, 1.0f);
            auto [r, g, b, a_] = ColorUtil::U32ToFloats(color);
            return ColorUtil::FloatsTo32(light * r,
                                         light * g,
                                         light * b,
                                         1.0f);
          };

        auto Trace = [&](){
          for (const auto &[idx, pi] : hits) {
            CHECK(pi.hit);

            if (idx == 0) {
              // sphere
              if (in_mouth) {
                // Ignore the surface of the sphere while
                // inside the mouth cutout.
              } else {
                // Normal hit on sphere.
                if (first) {
                  // printf("%.5f\n", pi.distance);
                  first = false;
                }
                uint32_t color = EarthColor(1.0f - pi.uv.x, pi.uv.y,
                                            pi.distance);

                vec3d pt = ray.o + pi.distance * ray.d;
                color = Shade(pt, color);

                EmitColor(color);
                return;
              }
            } else if (idx == 1) {

              if (in_mouth) {
                // Exit mouth.
                // Is this the inner surface of the sphere?
                vec3d pt = ray.o + pi.distance * ray.d;
                // XXX hard coded location of earth
                double r = length(pt);
                if (r <= EARTH_RADIUS) {
                  uint32_t color = ColorUtil::LinearGradient32(
                      INNER_EARTH, r / EARTH_RADIUS);

                  // But make it darker
                  // img.BlendPixel32(px, py, 0x48270677);
                  color = Shade(pt, color);
                  EmitColor(color);
                  in_mouth = false;
                  return;
                } else {
                  // Exiting to free space
                  in_mouth = false;
                }

              } else {
                // Enter mouth.
                in_mouth = true;
              }
            } else if (idx == 2) {
              // Hit stars

              double x = pi.uv.x * stars->Width();
              double y = pi.uv.y * stars->Height();
              float r, g, b, a_;
              std::tie(r, g, b, a_) = stars->SampleBilinear(x, y);
              r = sqrtf(r);
              g = sqrtf(g);
              b = sqrtf(b);
              uint32_t color = ColorUtil::FloatsTo32(r, g, b, 1.0f);
              EmitColor(color);
              return;
            } else if (idx == 3) {
              // Atmosphere
              vec3d pt = ray.o + pi.distance * ray.d;
              if (!in_atmosphere.has_value()) {
                in_atmosphere = {pt};
                // printf("enter atmosphere\n");
              } else {
                vec3d chord = pt - in_atmosphere.value();
                double len = length(chord);

                double max_len = atmosphere.radius * 2.0;
                // But most chords are only a tiny fraction.
                double frac = len / max_len;
                frac = pow(frac, 2);


                // XXX based on chord length!
                // blend.push_back(0x0000FF22);

                const auto [r, g, b] = ColorUtil::LinearGradient(
                    ATMOSPHERE, frac);

                uint32 color = ColorUtil::FloatsTo32(
                    r, g, b, frac);

                blend.push_back(color);

                in_atmosphere = std::nullopt;
                // printf("exit atmosphere.\n");
              }


            } else {
              CHECK(false) << "unknown prim";
            }
          }

          // did not hit solid, so render error color
          img.SetPixel32(px, py, 0xFF1100FF);
        };

        Trace();

      }
      }, THREADS);

  ImageRGBA down = img.ScaleDownBy(oversample);
  down.BlendText32(5, 5, 0xFFFF22FF,
                   StringPrintf("%.9f,%.9f,%.9f", a1, a2, a3));
  down.BlendText32(5, 15, 0xFF22FFFF,
                   StringPrintf("%.9f", distance));
  return down;
}

int main(int argc, char **argv) {

  auto pt = PacTom::FromFiles({"../pac.kml", "../pac2.kml"},
                              "../neighborhoods.kml");
  CHECK(pt.get() != nullptr);
  pactom = pt.release();

  tile = new Tile;
  tile->Drawhoods(pactom);

  //  const auto [a1, a2, a3, d_] = Optimize();
  const double a1 = 0.117112122;
  const double a2 = 3.008712215;
  const double a3 = 2.284319047;
  const double distance = 2.80100000000;

  printf("Loading marble...\n");
  // bluemarble = ImageRGBA::Load("world_shaded_43k.jpg");
  // bluemarble = ImageRGBA::Load("bluemarble.png");

  // stb_image can't decode the original (integer overflow) but two
  // hemispheres do fit. So load them individually and blit them into
  // the full sphere texture.
  string west_file = "land_shallow_topo_west.jpg";
  string east_file = "land_shallow_topo_east.jpg";

  std::unique_ptr<ImageRGBA> west, east;

  #if 1
  InParallel(
      [&](){ stars = ImageFRGBA::Load("starmap_2020_64k.exr"); },
      [&](){ west.reset(ImageRGBA::Load(west_file)); },
      [&](){ east.reset(ImageRGBA::Load(east_file)); });
  CHECK(west.get() != nullptr);
  CHECK(east.get() != nullptr);
  CHECK(stars != nullptr);

  CHECK(west->Height() == east->Height());
  CHECK(west->Width() == east->Width());
  Timer alloc_timer;
  bluemarble = new ImageRGBA(west->Width() * 2, west->Height());
  printf("Alloc in %.4fs\n", alloc_timer.Seconds());
  CHECK(bluemarble != nullptr);

  Timer copy_timer;
  bluemarble->CopyImage(0, 0, *west);
  bluemarble->CopyImage(west->Width(), 0, *east);
  printf("Copy in %.4fs\n", copy_timer.Seconds());

  west.reset();
  east.reset();

  // XXX
  // ImageFRGBA sstars = stars->ScaleDownBy(4);
  // sstars.ToRGBA().Save("sstars.png");

  #else

  bluemarble = ImageRGBA::Load("bluemarble.png");
  {
    // Load smaller file but convert to FRGBA
    // (We could use an even smaller placeholder here...)
    ImageRGBA *starsjpg = ImageRGBA::Load("sstars.png"); // "stars.jpg");
    CHECK(starsjpg != nullptr);
    printf("New from JPG:\n");
    stars = new ImageFRGBA(*starsjpg);
    delete starsjpg;
  }
  #endif

  CHECK(stars != nullptr);
  CHECK(bluemarble != nullptr);

  printf("Done. Earth texture %d x %d\n Stars %lld x %lld\n",
         bluemarble->Width(), bluemarble->Height(),
         stars->Width(), stars->Height());

  auto FrameDistance = [](double f) {
      return std::lerp(2.80000428964, 14.0, f * f * f);
    };
  auto FrameAngle = [a1, a2, a3](double f) {
      return std::make_tuple(a1, a2 + 0.002 * f, a3 - 0.8 * f);
    };

  enum RenderMode {
    FRAMES,
    ONE_FRAME,
    MOSAIC,
    FRAME_VARIANTS,
  };

  RenderMode mode = FRAMES;
  const int target_frame = 91;
  static constexpr int FRAME_WIDTH = 2880;
  static constexpr int FRAME_HEIGHT = 1620;
  static constexpr int OVERSAMPLE = 2;
  static constexpr int NUM_FRAMES = 120;

  switch (mode) {
  case FRAMES: {
    Timer run_timer;
    Asynchronously async(8);
    // static constexpr int FRAME_WIDTH = 1920;
    // static constexpr int FRAME_HEIGHT = 1080;
    for (int i = 0; i < NUM_FRAMES; i++) {
      double f = i / (double)(NUM_FRAMES - 1);
      double distance = FrameDistance(f);
      const auto [fa1, fa2, fa3] = FrameAngle(f);
      ImageRGBA img = RenderFrame(FRAME_WIDTH, FRAME_HEIGHT, OVERSAMPLE,
                                  distance, fa1, fa2, fa3);
      async.Run([i, img = std::move(img)]() {
          img.Save(StringPrintf("sphere%d.png", i));
          printf("%d\n", i);
        });
    }
    double sec = run_timer.Seconds();
    printf("Wrote %d frames in %.1f sec (%.3f sec/frame).\n",
           NUM_FRAMES, sec, sec / NUM_FRAMES);

    break;
  }

  case MOSAIC: {
    static constexpr int ACROSS = 5;
    static constexpr int DOWN = 5;
    static constexpr int ONEW = 288 * 2;
    static constexpr int ONEH = 162 * 2;
    static constexpr int OVERSAMPLE = 2;

    ImageRGBA mosaic(ONEW * ACROSS, ONEH * DOWN);
    for (int y = 0; y < DOWN; y++) {
      for (int x = 0; x < ACROSS; x++) {
        int i = y * DOWN + x;
        double f = i / (double)(ACROSS * DOWN - 1);
        double distance = FrameDistance(f);
        const auto [fa1, fa2, fa3] = FrameAngle(f);
        ImageRGBA img = RenderFrame(ONEW, ONEH, OVERSAMPLE,
                                    distance,
                                    fa1, fa2, fa3);
        mosaic.CopyImage(x * ONEW, y * ONEH, img);
      }
    }
    mosaic.Save("mosaic.png");
    printf("Wrote mosaic.png\n");
    break;
  }

  case FRAME_VARIANTS: {

    ArcFour rc(StringPrintf("%lld", time(nullptr)));
    RandomGaussian gauss(&rc);
    static constexpr int ACROSS = 5;
    static constexpr int DOWN = 5;
    static constexpr int ONEW = 288 * 2;
    static constexpr int ONEH = 162 * 2;
    static constexpr int OVERSAMPLE = 2;

    ImageRGBA variants(ONEW * ACROSS, ONEH * DOWN);
    for (int y = 0; y < DOWN; y++) {
      for (int x = 0; x < ACROSS; x++) {
        int i = y * DOWN + x;
        double f = target_frame / (double)(ACROSS * DOWN - 1);
        double distance = FrameDistance(f);
        double aa1, aa2, aa3;
        std::tie(aa1, aa2, aa3) = FrameAngle(f);

        double scale = 0.0001;

        if (i > 0) {
          aa1 += gauss.Next() * scale;
          aa2 += gauss.Next() * scale;
          aa3 += gauss.Next() * scale;
          distance += gauss.Next() * scale;
        }

        ImageRGBA img = RenderFrame(ONEW, ONEH, OVERSAMPLE,
                                    distance,
                                    aa1, aa2, aa3);
        variants.CopyImage(x * ONEW, y * ONEH, img);
      }
    }
    string filename = StringPrintf("variants%d.png", target_frame);
    variants.Save(filename);
    printf("Wrote %s\n", filename.c_str());

    break;
  }

  case ONE_FRAME: {

    Timer run_timer;
    double f = target_frame / (double)(NUM_FRAMES - 1);
    double distance = FrameDistance(f);
    const auto [fa1, fa2, fa3] = FrameAngle(f);
    ImageRGBA img = RenderFrame(FRAME_WIDTH, FRAME_HEIGHT, OVERSAMPLE,
                                distance, fa1, fa2, fa3);
    img.Save(StringPrintf("frame%d.png", target_frame));
    double sec = run_timer.Seconds();
    printf("Wrote frame in %.1f sec.\n", sec);

    break;
  }
  }

  delete bluemarble;
  delete stars;
  return 0;
}
