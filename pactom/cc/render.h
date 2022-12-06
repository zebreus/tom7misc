#ifndef _PACTOM_RENDER_H
#define _PACTOM_RENDER_H

#include <cmath>
#include <algorithm>

#include "yocto_matht.h"
#include "yocto_geometryt.h"

struct ConvertUV {
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

struct WideTile {
  // pulaski twp
  static constexpr double lat0 = 41.069922;
  static constexpr double lon0 = -80.480119;

  // londonderry twp
  static constexpr double lat1 = 39.769658;
  static constexpr double lon1 = -78.745817;

  static constexpr auto tile_top = ConvertUV::ToUV(lat0, lon0);
  static constexpr auto tile_bot = ConvertUV::ToUV(lat1, lon1);

  static constexpr double center_u =
    (tile_top.first + tile_bot.first) * 0.5;
  static constexpr double center_v =
    (tile_top.second + tile_bot.second) * 0.5;

  static constexpr double width_uv =
    abs(tile_top.first - tile_bot.first);
  static constexpr double height_uv =
    abs(tile_top.second - tile_bot.second);

  WideTile() {
    image.reset(ImageRGBA::Load("tilewide.png"));
    CHECK(image.get() != nullptr);
    // proj = LatLon::Linear(LatLon::FromDegs(lat0, lon0),
    //                       LatLon::FromDegs(lat1, lon1));
    Recolor();
  }

  void Recolor() {
    for (int y = 0; y < image->Height(); y++) {
      for (int x = 0; x < image->Width(); x++) {
        uint32 c = image->GetPixel32(x, y);
        const auto [r, g, b] = RecolorPixel(c);
        image->SetPixel32(x, y, ColorUtil::FloatsTo32(r, g, b, 1.0f));
      }
    }
  }

  static inline std::tuple<double, double, double>
  RecolorPixel(uint32 color) {
    static constexpr double ho = -.01; // 0.0; // -0.02143107475;
    static constexpr double so = 0.34924340224;
    static constexpr double vo = -0.025; // -0.05219921795;

    auto [ro, go, bo, ao_] = ColorUtil::U32ToFloats(color);
    float h, s, v;
    std::tie(h, s, v) = ColorUtil::RGBToHSV(ro, go, bo);

    // Hue should wrap around.
    h += ho;
    if (h < 0.0) h += 1.0;
    if (h > 1.0) h -= 1.0;
    // But these should saturate.
    s = std::clamp(s + so, 0.0, 1.0);
    v = std::clamp(v + vo, 0.0, 1.0);

    return ColorUtil::HSVToRGB(h, s, v);
  }


  std::unique_ptr<ImageRGBA> image;
};

struct Tile {
  static constexpr double lat0 = 40.577355;
  static constexpr double lon0 = -80.183547;

  static constexpr double lat1 = 40.289646;
  static constexpr double lon1 = -79.516107;

  static constexpr auto tile_top = ConvertUV::ToUV(lat0, lon0);
  static constexpr auto tile_bot = ConvertUV::ToUV(lat1, lon1);

  // in uv
  static constexpr double center_u =
    (tile_top.first + tile_bot.first) * 0.5;
  static constexpr double center_v =
    (tile_top.second + tile_bot.second) * 0.5;

  static constexpr double width_uv =
    abs(tile_top.first - tile_bot.first);
  static constexpr double height_uv =
    abs(tile_top.second - tile_bot.second);

  LatLon::Projection proj;

  // TODO: shift colors to match marble image
  Tile() {
    image.reset(ImageRGBA::Load("tile.png"));
    CHECK(image.get() != nullptr);
    proj = LatLon::Linear(LatLon::FromDegs(lat0, lon0),
                          LatLon::FromDegs(lat1, lon1));
    Recolor();
  }

  std::pair<double, double> ToXY(LatLon ll) {
    const auto [fx, fy] = proj(ll);
    return std::make_pair(fx * image->Width(),
                          fy * image->Height());
  }

  void Recolor() {
    for (int y = 0; y < image->Height(); y++) {
      for (int x = 0; x < image->Width(); x++) {
        uint32 c = image->GetPixel32(x, y);
        const auto [r, g, b] = RecolorPixel(c);
        image->SetPixel32(x, y, ColorUtil::FloatsTo32(r, g, b, 1.0f));
      }
    }
  }

  static inline std::tuple<double, double, double>
  RecolorPixel(uint32 color) {
    static constexpr double ho = -0.02143107475;
    static constexpr double so = 0.34924340224;
    static constexpr double vo = -0.05219921795;

    auto [ro, go, bo, ao_] = ColorUtil::U32ToFloats(color);
    float h, s, v;
    std::tie(h, s, v) = ColorUtil::RGBToHSV(ro, go, bo);

    // Hue should wrap around.
    h += ho;
    if (h < 0.0) h += 1.0;
    if (h > 1.0) h -= 1.0;
    // But these should saturate.
    s = std::clamp(s + so, 0.0, 1.0);
    v = std::clamp(v + vo, 0.0, 1.0);

    return ColorUtil::HSVToRGB(h, s, v);
  }

  template<int RADIUS>
  void DrawThickLine(int x0, int y0, int x1, int y1, uint32_t color) {
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

  void DrawHoods(PacTom *pactom) {
    static constexpr int RADIUS = 4;
    CHECK(pactom != nullptr);

    for (const auto &[name, path] : pactom->hoods) {
      constexpr uint32 color = 0xFFFFAA;
      for (int i = 0; i < path.size() - 1; i++) {
        const LatLon latlon0 = path[i];
        const LatLon latlon1 = path[i + 1];
        auto [x0, y0] = ToXY(latlon0);
        auto [x1, y1] = ToXY(latlon1);

        DrawThickLine<RADIUS>(x0, y0, x1, y1, color);
      }
    }
  }

  static bool DrawRoad(OSM::Highway highway) {
    return highway != OSM::NONE;
  }

  void DrawStreets(const PacTom &pactom,
                   const OSM &osm) {
    static constexpr int RADIUS = 2;

    for (const auto &[way_id, way] : osm.ways) {
      if (DrawRoad(way.highway)) {
        const uint32 color = 0x55101077;
        for (int i = 0; i < way.points.size() - 1; i++) {
          const uint64_t id0 = way.points[i];
          const uint64_t id1 = way.points[i + 1];

          auto it0 = osm.nodes.find(id0);
          auto it1 = osm.nodes.find(id1);
          if (it0 != osm.nodes.end() &&
              it1 != osm.nodes.end()) {
            const LatLon latlon0 = it0->second;
            const LatLon latlon1 = it1->second;

            auto [x0, y0] = ToXY(latlon0);
            auto [x1, y1] = ToXY(latlon1);

            if (-1 != pactom.InNeighborhood(latlon0) &&
                -1 != pactom.InNeighborhood(latlon1)) {

              DrawThickLine<RADIUS>(x0, y0, x1, y1, color);
            }
          }
        }
      }
    }
  }

  std::unique_ptr<ImageRGBA> image;
};

using mat3d = yocto::mat<double, 3>;
using vec3d = yocto::vec<double, 3>;
using ray3d = yocto::ray<double, 3>;
using frame3d = yocto::frame<double, 3>;
using prim_isect_d = yocto::prim_intersection<double>;


inline mat3d RotYaw(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);
  return mat3d
    {cosa, -sina, 0.0,
     sina, cosa,  0.0,
     0.0, 0.0,  1.0};
}

inline mat3d RotPitch(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);

  return mat3d
    {cosa,  0.0, sina,
     0.0,  1.0, 0.0,
     -sina, 0.0, cosa};
}

inline mat3d RotRoll(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);

  return mat3d
    {1.0, 0.0, 0.0,
     0.0, cosa, -sina,
     0.0, sina, cosa};
}

inline mat3d Rot(double yaw, double pitch, double roll) {
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
  static constexpr double PI = std::numbers::pi;

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
      auto v = acos(std::clamp(plocal.z, -1.0, 1.0)) / PI;

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

struct Scene {
  std::vector<Prim> prims;

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
    isect.distance = yocto::flt_max;
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

template<class TILE>
std::tuple<float, float, float> BlendIfInside(const TILE *tile,
                                              double ux, double uy,
                                              double distance,
                                              float r, float g, float b) {
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

  if (auto po = In(TILE::tile_top, TILE::tile_bot)) {
    auto [x, y] = po.value();
    const auto [rr, gg, bb, a_] = tile->image->SampleBilinear(
        // XXX maybe don't bother scaling down?
        x * tile->image->Width(),
        y * tile->image->Height());

    // distance from center.
    double dx = (ux - TILE::center_u);
    double dy = (uy - TILE::center_v);
    const double rdist = sqrt((dx * dx) + (dy * dy));

    static constexpr double radius =
      std::min(TILE::width_uv,
               TILE::height_uv) * 0.45;
    // printf("%.17g radius\n", radius);
    // CHECK(false);

    // XXX parameterized
    // Fade out when we get far away.
    static constexpr double distance_close = 0.00015;
    static constexpr double distance_far = 0.00463;

    float mix_frac = distance < distance_close ? 1.0f :
                                distance >= distance_far ? 0.0f :
      (1.0f - ((distance - distance_close) / (distance_far - distance_close)));

    // Fade towards edges of tile.
    if (rdist > radius) {
      // XXX scale should be param here too
      mix_frac *= (1.0 - tanh(10.0 * ((rdist / radius) - 1.0)));
    }

    r = std::lerp(r, rr, mix_frac);
    g = std::lerp(g, gg, mix_frac);
    b = std::lerp(b, bb, mix_frac);
  }

  return std::make_tuple(r, g, b);
}

template<class T>
static inline int Sign(T val) {
  return (T(0) < val) - (val < T(0));
}

inline bool InTetrahedron(const vec3d &pt,
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

inline bool InSphere(const vec3d &pt,
                     const Sphere &sphere) {
  return length(pt - sphere.origin) < sphere.radius;
}


#endif
