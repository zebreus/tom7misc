
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
#include "osm.h"
#include "render.h"
#include "textsvg.h"
#include "util.h"

static constexpr double PI = std::numbers::pi;

static constexpr int THREADS = 24;

static constexpr double EARTH_RADIUS = 2.8;
static constexpr double MILES = 2.8 / 3963.19;
static constexpr double SUN_DISTANCE = 1000.0;
static constexpr double SUN_RADIUS = 108.15 * EARTH_RADIUS;

static constexpr double FAR_DIST = 100.0;
static constexpr double FAR_WIDTH = 100.0;
// static constexpr double NEAR_WIDTH = 0.01f;
static constexpr double NEAR_WIDTH = 0.005;

static constexpr double PLANET_SPACING = EARTH_RADIUS * 5.0;
static constexpr vec3d PLANET_VEC = {0.0, PLANET_SPACING, 0.0};

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

// Made up. From outside (0.0) to inside (1.0).
static constexpr ColorUtil::Gradient EARTH_ATMOSPHERE_GRADIENT {
  GradRGB(0.0f, 0x7700FF),
  GradRGB(0.15f, 0xFF77FF),
  GradRGB(0.25f, 0xffa21e),
  GradRGB(1.0f, 0x000000),
};

static constexpr ColorUtil::Gradient MARS_ATMOSPHERE_GRADIENT {
  GradRGB(0.0f, 0x770022),
  GradRGB(0.15f, 0xFF3333),
  GradRGB(0.25f, 0xff5200),
  GradRGB(1.0f, 0x000000),
};

static constexpr ColorUtil::Gradient JUPITER_ATMOSPHERE_GRADIENT {
  GradRGB(0.0f, 0x770077),
  GradRGB(0.15f, 0x992233),
  GradRGB(0.25f, 0xff5200),
  GradRGB(1.0f, 0x000000),
};

static constexpr ColorUtil::Gradient SATURN_ATMOSPHERE_GRADIENT {
  GradRGB(0.0f, 0x770077),
  GradRGB(0.15f, 0x992233),
  GradRGB(0.25f, 0xff5200),
  GradRGB(1.0f, 0x000000),
};

static constexpr ColorUtil::Gradient HEAVEN_ATMOSPHERE_GRADIENT {
  GradRGB(0.0f, 0xFF00FF),
  GradRGB(0.15f, 0xAA77FF),
  GradRGB(0.25f, 0x5577FF),
  GradRGB(1.0f, 0x000000),
};

using namespace yocto;

static PacTom *pactom = nullptr;

static ImageRGBA *heaventexture = nullptr;
static ImageRGBA *jupitertexture = nullptr;
static ImageRGBA *marstexture = nullptr;
static ImageRGBA *saturntexture = nullptr;
static ImageRGBA *venustexture = nullptr;
static ImageRGBA *bluemarble = nullptr;
static ImageFRGBA *stars = nullptr;

static WideTile *widetile = nullptr;
static Tile *tile = nullptr;

// distance is distance from camera to surface
uint32_t EarthColor(double ux, double uy, double distance) {

  CHECK(bluemarble != nullptr);
  CHECK(widetile != nullptr);
  CHECK(tile != nullptr);

  float r, g, b, a_;
  std::tie(r, g, b, a_) = bluemarble->SampleBilinear(
      ux * bluemarble->Width(),
      uy * bluemarble->Height());

  // Two zoom levels; wider goes behind narrower.
  std::tie(r, g, b) = BlendIfInside(widetile, ux, uy, distance, r, g, b);
  std::tie(r, g, b) = BlendIfInside(tile, ux, uy, distance, r, g, b);

  return ColorUtil::FloatsTo32(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

// distance is distance from camera to surface
uint32_t PlanetColor(ImageRGBA *texture,
                     double ux, double uy, double distance) {
  CHECK(texture != nullptr);

  float r, g, b, a_;
  std::tie(r, g, b, a_) = texture->SampleBilinear(
      ux * texture->Width(),
      uy * texture->Height());

  return ColorUtil::FloatsTo32(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

// Camera position.
struct CameraPosition {
  vec3d pos;
  vec3d up;

  vec3d look_at;
};

// for t in [0, 1]
static vec3d EvaluateCubicBezier(vec3d start, vec3d c1, vec3d c2, vec3d end,
                                 double t) {
  const double omt = 1.0 - t;
  const double omt_squared = omt * omt;
  const double omt_cubed = omt_squared * omt;
  const double t_squared = t * t;
  const double t_cubed = t_squared * t;

  return omt_cubed * start +
    3.0 * omt_squared * t * c1 +
    3.0 * omt * t_squared * c2 +
    t_cubed * end;
}

// We can be smarter by tesselating this, but for this problem just
// sampling a bunch of points is plenty.
static double CubicBezierLength(vec3d start, vec3d c1, vec3d c2, vec3d end) {
  static constexpr int SAMPLES = 1024;
  double total_length = 0.0;
  vec3d prev = start;
  for (int i = 0; i < SAMPLES; i++) {
    const double t = (i + 1) / SAMPLES;
    vec3d pt = EvaluateCubicBezier(start, c1, c2, end, t);
    total_length += length(pt - prev);
    prev = pt;
  }
  return total_length;
}

static CameraPosition InterpolatePos(const CameraPosition &a,
                                     const CameraPosition &b,
                                     double f) {

  CameraPosition ret;
  ret.pos = InterpolateVec(a.pos, b.pos, f);
  ret.up = InterpolateVec(a.up, b.up, f);
  ret.look_at = InterpolateVec(a.look_at, b.look_at, f);

  return ret;
}

struct Shot {
  // Assuming 60fps.
  int num_frames = 60;

  // At time in [0, 1].
  std::function<CameraPosition(double)> getpos = [](double f) {
      CHECK(false) << "unimplemented";
      return CameraPosition();
    };

  // Time remapping
  std::function<double(double)> easing = [](double f) { return f; };
};

std::function<CameraPosition(double)> LinearCamera(
    CameraPosition start,
    CameraPosition end) {
  return [start, end](double f) {
      return InterpolatePos(start, end, f);
    };
}

struct CameraSwing {
  // Cubic bezier for camera pos.
  vec3d c1, c2, end;
  // at the end. Linear interpolation.
  vec3d look_at;
  vec3d up;
};

struct Path {
  CameraPosition start;
  std::vector<CameraSwing> swings;
};

std::function<CameraPosition(double)> PathCamera(Path path) {
  // If degenerate...
  if (path.swings.empty())
    return [s = path.start](double f) {
      return s;
    };

  return [path](double f) {
      double scaled_f = f * path.swings.size();
      // truncating
      int idx = scaled_f;
      if (idx < 0) return path.start;
      if (idx >= path.swings.size()) {
        CameraPosition end;
        const CameraSwing &swing = path.swings.back();
        end.pos = swing.end;
        end.up = swing.up;
        end.look_at = swing.look_at;
        return end;
      }

      const double t = scaled_f - idx;
      // This could be cleaner if we used CameraPosition in Swing?
      const vec3d start_pos =
        idx == 0 ? path.start.pos : path.swings[idx - 1].end;
      const vec3d start_up =
        idx == 0 ? path.start.up : path.swings[idx - 1].up;
      const vec3d start_la =
        idx == 0 ? path.start.look_at : path.swings[idx - 1].look_at;
      const CameraSwing &swing = path.swings[idx];
      CameraPosition cp;
      cp.pos = EvaluateCubicBezier(start_pos, swing.c1, swing.c2, swing.end,
                                   t);
      // cp.pos = InterpolateVec(start_pos, swing.end, t);
      cp.up = InterpolateVec(start_up, swing.up, t);
      cp.look_at = InterpolateVec(start_la, swing.look_at, t);
      return cp;
    };
}


static double EaseOutQuart(double f) {
  double sf = (1.0 - f) * (1.0 - f);
  return 1.0 - (sf * sf);
}

static double EaseInOutSin(double f) {
  return 0.5 * (1.0 + sin(3.141592653589 * (f - 0.5)));
}

struct Atmosphere {
  ColorUtil::Gradient gradient;
};

struct System {

  System() {
    // Right-handed:
    //
    //   ^ +y   -z (into scene)
    //   |    /
    //   |  /
    //   |/
    //   *-------> +x
    //  /
    // +z

    earth.radius = EARTH_RADIUS;
    earth.origin = {0.0f, 0.0f, 0.0f};
    EARTH_PRIM = scene.prims.size();
    scene.prims.emplace_back(earth);

    earth_atmosphere.radius = EARTH_RADIUS + (9 + 31 + 53 + 372) * MILES;
    earth_atmosphere.origin = {0.0f, 0.0f, 0.0f};
    EARTH_ATMOSPHERE_PRIM = scene.prims.size();
    atmos[EARTH_ATMOSPHERE_PRIM].gradient = EARTH_ATMOSPHERE_GRADIENT;
    scene.prims.emplace_back(earth_atmosphere);

    vec3d tp1 = RotYaw(PI / -2) * (earth.origin + vec3d{0, -5, 0});
    vec3d tp2 = RotYaw(PI / -2) * (earth.origin + vec3d{0, +5, 0});
    vec3d tp3 = RotYaw(PI / -2) * (earth.origin + vec3d{5, 0, +5});
    vec3d tp4 = RotYaw(PI / -2) * (earth.origin + vec3d{5, 0, -5});

    mouth = Tetrahedron(tp1, tp2, tp3, tp4);
    MOUTH_PRIM = scene.prims.size();
    scene.prims.emplace_back(mouth);

    constexpr double MARS_RADIUS = EARTH_RADIUS * 0.8;

    mars.radius = MARS_RADIUS;
    mars.origin = 1 * PLANET_VEC;
    MARS_PRIM = scene.prims.size();
    scene.prims.emplace_back(mars);

    mars_atmosphere.radius = mars.radius * 1.1;
    mars_atmosphere.origin = mars.origin;
    MARS_ATMOSPHERE_PRIM = scene.prims.size();
    atmos[MARS_ATMOSPHERE_PRIM].gradient = MARS_ATMOSPHERE_GRADIENT;
    scene.prims.emplace_back(mars_atmosphere);

    constexpr double JUPITER_RADIUS = EARTH_RADIUS * 2.0;

    jupiter.radius = JUPITER_RADIUS;
    jupiter.origin = 2 * PLANET_VEC;
    JUPITER_PRIM = scene.prims.size();
    scene.prims.emplace_back(jupiter);

    jupiter_atmosphere.radius = jupiter.radius * 1.2;
    jupiter_atmosphere.origin = jupiter.origin;
    JUPITER_ATMOSPHERE_PRIM = scene.prims.size();
    atmos[JUPITER_ATMOSPHERE_PRIM].gradient = JUPITER_ATMOSPHERE_GRADIENT;
    scene.prims.emplace_back(jupiter_atmosphere);

    constexpr double SATURN_RADIUS = JUPITER_RADIUS * 0.8;
    saturn.radius = SATURN_RADIUS;
    saturn.origin = 3 * PLANET_VEC;
    SATURN_PRIM = scene.prims.size();
    scene.prims.emplace_back(saturn);

    saturn_atmosphere.radius = saturn.radius * 1.2;
    saturn_atmosphere.origin = saturn.origin;
    SATURN_ATMOSPHERE_PRIM = scene.prims.size();
    atmos[SATURN_ATMOSPHERE_PRIM].gradient = SATURN_ATMOSPHERE_GRADIENT;
    scene.prims.emplace_back(saturn_atmosphere);

    heaven.radius = EARTH_RADIUS;
    heaven.origin = 4 * PLANET_VEC;
    HEAVEN_PRIM = scene.prims.size();
    scene.prims.emplace_back(heaven);

    heaven_atmosphere.radius = heaven.radius * 1.2;
    heaven_atmosphere.origin = heaven.origin;
    HEAVEN_ATMOSPHERE_PRIM = scene.prims.size();
    atmos[HEAVEN_ATMOSPHERE_PRIM].gradient = HEAVEN_ATMOSPHERE_GRADIENT;
    scene.prims.emplace_back(heaven_atmosphere);

    starbox.radius = 250.0;
    starbox.origin = {0.0f, 0.0f, 0.0f};
    STARS_PRIM = scene.prims.size();
    scene.prims.emplace_back(starbox);

  }

  Scene scene;
  std::unordered_map<int, Atmosphere> atmos;
  Sphere earth, mars, jupiter, saturn, heaven, starbox;
  Sphere earth_atmosphere, mars_atmosphere, jupiter_atmosphere,
    saturn_atmosphere, heaven_atmosphere;
  Tetrahedron mouth;

  int EARTH_PRIM, MARS_PRIM, JUPITER_PRIM, SATURN_PRIM, HEAVEN_PRIM,
    STARS_PRIM;
  int EARTH_ATMOSPHERE_PRIM, MARS_ATMOSPHERE_PRIM, JUPITER_ATMOSPHERE_PRIM,
    SATURN_ATMOSPHERE_PRIM, HEAVEN_ATMOSPHERE_PRIM;
  int MOUTH_PRIM;

  void ToSVG(const string &filename) {
    // planets are stacked along y axis, so leave out z
    constexpr double SIZE = EARTH_RADIUS * 24;
    string out = TextSVG::Header(SIZE, SIZE);
    for (const auto &prim : scene.prims) {
      if (const Sphere *sphere = std::get_if<Sphere>(&prim.v)) {
        StringAppendF(&out,
                      "<circle fill=\"none\" stroke=\"#000\" "
                      "cx=\"%s\" cy=\"%s\" r=\"%s\" />\n",
                      TextSVG::Rtos(sphere->origin.x).c_str(),
                      TextSVG::Rtos(sphere->origin.y).c_str(),
                      TextSVG::Rtos(sphere->radius).c_str());
      }
    }

    out += TextSVG::Footer();
    Util::WriteFile(filename, out);
    printf("Wrote %s\n", filename.c_str());
  }

};


static ImageRGBA RenderFrame(
    int frame_width,
    int frame_height,
    int oversample,
    CameraPosition pos) {

  const System system;

  static constexpr bool draw_mouth = true;
  static constexpr bool draw_atmosphere = true;

  ImageRGBA img(frame_width * oversample, frame_height * oversample);


  const double aspect = frame_width / (double)frame_height;

  const double near_height = NEAR_WIDTH / aspect;
  const double far_height = FAR_WIDTH / aspect;

  const vec3d forward = normalize(pos.look_at - pos.pos);
  const vec3d up = normalize(pos.up);


  const frame3d camera = lookat_frame(pos.pos, pos.look_at, pos.up);

  bool first = true;
  ParallelComp(
      img.Height(),
      [&](int py) {
      double yf = py / (double)img.Height();
      for (int px = 0; px < img.Width(); px++) {
        double xf = px / (double)img.Width();

        ray3d ray =
          camera_ray(camera, 100.0, aspect, FAR_WIDTH, vec2d{xf, yf});

        const std::vector<std::pair<int, prim_isect_d>> hits =
          system.scene.AllIntersections(ray);

        // Translucent elements we traced through.
        std::vector<uint32_t> blend;
        auto EmitColor = [&img, px, py, &blend](uint32_t color) {
            img.SetPixel32(px, py, color);
            for (int i = blend.size() - 1; i >= 0; i--)
              img.BlendPixel32(px, py, blend[i]);
          };

        bool in_mouth = draw_mouth && InTetrahedron(ray.o, system.mouth);

        // atmospheres do not intersect; we are in just one at a
        // time.
        // point where we entered atmosphere, prim index of atmosphere
        std::optional<std::pair<vec3d, int>> in_atmosphere;
        for (const auto &[prim_idx, gradient_] : system.atmos) {
          const Sphere *sphere =
            std::get_if<Sphere>(&system.scene.prims[prim_idx].v);
          CHECK(sphere != nullptr);
          if (InSphere(ray.o, *sphere)) {
            in_atmosphere.emplace(ray.o, prim_idx);
            break;
          }
        }

        auto Shade = [&](vec3d pt, uint32_t color) {
            const float light = 1.0f;
            auto [r, g, b, a_] = ColorUtil::U32ToFloats(color);
            return ColorUtil::FloatsTo32(light * r,
                                         light * g,
                                         light * b,
                                         1.0f);
          };

        auto Trace = [&](){

          for (const auto &[idx, pi] : hits) {
            CHECK(pi.hit);

            auto ExitAtmosphere = [&]() {
                if (!in_atmosphere.has_value()) return;
                const auto &[enter_pt, prim_idx] = in_atmosphere.value();
                const Sphere *sphere =
                  std::get_if<Sphere>(&system.scene.prims[prim_idx].v);
                CHECK(sphere != nullptr);

                auto ait = system.atmos.find(prim_idx);
                CHECK(ait != system.atmos.end());
                const Atmosphere &atm = ait->second;

                vec3d pt = ray.o + pi.distance * ray.d;
                vec3d chord = pt - enter_pt;
                double len = length(chord);

                double max_len = sphere->radius * 2.0;
                // But most chords are only a tiny fraction.
                double frac = len / max_len;
                frac = pow(frac, 2);

                const auto [r, g, b] = ColorUtil::LinearGradient(
                    atm.gradient, frac);

                uint32 color = ColorUtil::FloatsTo32(
                    r, g, b, frac);

                blend.push_back(color);

                in_atmosphere = std::nullopt;
              };

            if (idx == system.EARTH_PRIM) {
              // sphere
              if (in_mouth) {
                // Ignore the surface of the sphere while
                // inside the mouth cutout.
              } else {
                // Normal hit on sphere.

                ExitAtmosphere();

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
            } else if (idx == system.MARS_PRIM) {
              ExitAtmosphere();
              uint32_t color = PlanetColor(marstexture,
                                           1.0f - pi.uv.x, pi.uv.y,
                                           pi.distance);

              vec3d pt = ray.o + pi.distance * ray.d;
              color = Shade(pt, color);

              EmitColor(color);
              return;

            } else if (idx == system.JUPITER_PRIM) {
              ExitAtmosphere();
              uint32_t color = PlanetColor(jupitertexture,
                                           1.0f - pi.uv.x, pi.uv.y,
                                           pi.distance);

              vec3d pt = ray.o + pi.distance * ray.d;
              color = Shade(pt, color);

              EmitColor(color);
              return;

            } else if (idx == system.HEAVEN_PRIM) {
              ExitAtmosphere();
              uint32_t color = PlanetColor(heaventexture,
                                           1.0f - pi.uv.x, pi.uv.y,
                                           pi.distance);

              vec3d pt = ray.o + pi.distance * ray.d;
              color = Shade(pt, color);

              EmitColor(color);
              return;

            } else if (idx == system.SATURN_PRIM) {
              ExitAtmosphere();
              uint32_t color = PlanetColor(saturntexture,
                                           1.0f - pi.uv.x, pi.uv.y,
                                           pi.distance);

              vec3d pt = ray.o + pi.distance * ray.d;
              color = Shade(pt, color);

              EmitColor(color);
              return;

            } else if (idx == system.MOUTH_PRIM) {

              if (in_mouth) {
                // Exit mouth.
                // Is this the inner surface of the sphere?
                vec3d pt = ray.o + pi.distance * ray.d;
                double r = length(pt - system.earth.origin);
                if (r <= EARTH_RADIUS) {
                  ExitAtmosphere();

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
                if (draw_mouth) {
                  in_mouth = true;
                }
              }
            } else if (idx == system.STARS_PRIM) {
              // Hit stars

              const double wrap_x = fmod(pi.uv.x + 0.5, 1.0);

              CHECK(stars != nullptr);
              double x = wrap_x * stars->Width();
              double y = pi.uv.y * stars->Height();
              float r, g, b, a_;
              std::tie(r, g, b, a_) = stars->SampleBilinear(x, y);
              r = sqrtf(r);
              g = sqrtf(g);
              b = sqrtf(b);
              uint32_t color = ColorUtil::FloatsTo32(r, g, b, 1.0f);
              EmitColor(color);
              return;
            } else if (idx == system.EARTH_ATMOSPHERE_PRIM ||
                       idx == system.MARS_ATMOSPHERE_PRIM ||
                       idx == system.JUPITER_ATMOSPHERE_PRIM ||
                       idx == system.SATURN_ATMOSPHERE_PRIM ||
                       idx == system.HEAVEN_ATMOSPHERE_PRIM) {
              // Atmosphere
              vec3d pt = ray.o + pi.distance * ray.d;
              if (!in_atmosphere.has_value()) {
                in_atmosphere.emplace(pt, idx);
              } else {
                ExitAtmosphere();
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
#if 0
  down.BlendText32(5, 5, 0xFFFF22FF,
                   StringPrintf("%.9f,%.9f,%.9f",
                                pos.a1, pos.a2, pos.a3));
  down.BlendText32(5, 15, 0xFF22FFFF,
                   StringPrintf("%.9f", pos.distance));
#endif
  return down;
}

// Load textures into global variables.
template<bool HUGE_TEXTURES>
static void LoadTextures() {
  printf("Loading textures...\n");

  InParallel(
      [&]() { jupitertexture = ImageRGBA::Load("jupiter.jpg"); },
      [&]() { heaventexture = ImageRGBA::Load("heaven.png"); },
      [&]() { marstexture = ImageRGBA::Load("mars.jpg"); },
      [&]() { saturntexture = ImageRGBA::Load("saturn.jpg"); },
      [&]() { venustexture = ImageRGBA::Load("venus.jpg"); }
             );

  // stb_image can't decode the original (integer overflow) but two
  // hemispheres do fit. So load them individually and blit them into
  // the full sphere texture.

  std::unique_ptr<ImageRGBA> west, east;

  if constexpr (HUGE_TEXTURES) {
    string west_file = "land_shallow_topo_west.jpg";
    string east_file = "land_shallow_topo_east.jpg";
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

  } else {

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
  }


  // Different underlying image types but all have Width, Height.
#define SHOWSIZE(img) do { \
  CHECK(img != nullptr) << #img; \
  printf(#img " size %lld x %lld\n", \
         (int64)img->Width(), (int64)img->Height()); \
  } while (0)

  SHOWSIZE(bluemarble);
  SHOWSIZE(stars);
  SHOWSIZE(jupitertexture);
  SHOWSIZE(marstexture);
  SHOWSIZE(saturntexture);
  SHOWSIZE(venustexture);
#undef SHOWSIZE
}

int main(int argc, char **argv) {

  OSM osm;
  #if 0
  auto pt = PacTom::FromFiles({"../pac.kml", "../pac2.kml"},
                              "../neighborhoods.kml");
  CHECK(pt.get() != nullptr);
  pactom = pt.release();

  for (const string osmfile : {
        "../pittsburgh-center.osm",
        "../pittsburgh-northeast.osm",
        "../pittsburgh-north.osm",
        "../pittsburgh-south.osm",
        "../pittsburgh-southwest.osm",
        "../pittsburgh-west.osm",
       }) osm.AddFile(osmfile);
  #endif

  InParallel(
      [&](){
        tile = new Tile;
        if (pactom != nullptr) {
          tile->DrawStreets(*pactom, osm);
          tile->DrawHoods(pactom);
        }
      },
      [](){
        widetile = new WideTile;
      });

  LoadTextures<false>();

  enum RenderMode {
    FRAMES,
    ONE_FRAME,
    MOSAIC,
    FRAME_VARIANTS,
  };

  RenderMode mode = FRAMES;
  const int target_shot = 1;
  const int target_frame = 239;
  // static constexpr int FRAME_WIDTH = 2880;
  // static constexpr int FRAME_HEIGHT = 1620;
  static constexpr int FRAME_WIDTH = 1920;
  static constexpr int FRAME_HEIGHT = 1080;
  static constexpr int OVERSAMPLE = 4;

  System system;
  system.ToSVG("solar-system.svg");

  vec3d point = {-10.4,0,0};

  Path path;
  path.start.pos = point;
  path.start.look_at = {0.0, 0.0, 0.0};
  path.start.up = {0, 0, 1};

  // Absolute.
  auto CurveTo = [&path, &point](vec3d c1, vec3d c2, vec3d end,
                                 vec3d look_at) {
      CameraSwing swing;
      swing.c1 = c1;
      swing.c2 = c2;
      swing.end = end;
      swing.look_at = look_at;
      swing.up = {0, 0, 1};
      path.swings.push_back(swing);
      point = end;
    };

  auto CurveToRelative = [&point, &CurveTo](vec3d c1, vec3d c2, vec3d end,
                                            vec3d look_at) {
      c1 += point;
      c2 += point;
      end += point;

      CurveTo(c1, c2, end, look_at);
    };

  // capital is absolute
  // lowercase is relative

  CurveToRelative({1.7,2.2,0}, {2.3,4.8,0}, {9.9,6.7,0.0},
                  // Look at Mars
                  system.mars.origin);
  CurveTo({6.3,8.4,0}, {16,9.8,0}, {18.6,19.5,0},
          system.saturn.origin);
  CurveToRelative({1.2,4.5,0}, {2.7,24.8,0}, {-1.5,30.3,0},
                  system.heaven.origin);
  CurveToRelative({-3.9,5.2,0}, {-9.4,6.1,0}, {-14.3,6.2,0},
                  system.heaven.origin);

  // Now we can get the (non-length-normalized) position as
  // a function of t (index into the curves by 1/curves).
  // But we want to remap time so that we move along the full
  // path at a constant speed, so instead weight the segments
  // by their length.
  // TODO

#if 0
  CameraPosition start;
  start.pos = {EARTH_RADIUS * 3, 0, 0};
  start.look_at = {0, 0, 0};
  start.up = {0, 0, 1};

  CameraPosition look_at_mars;
  look_at_mars.pos = start.pos;
  look_at_mars.look_at = {0.0, +PLANET_SPACING, 0.0};
  look_at_mars.up = start.up;

  CameraPosition look_at_saturn;
  look_at_saturn.pos =
    {EARTH_RADIUS * -5, +PLANET_SPACING * 3, 0.0};
  look_at_saturn.look_at = {0.0, +PLANET_SPACING * 3, 0};
  look_at_saturn.up = start.up;

  CameraPosition look_at_heaven;
  look_at_heaven.pos =
    {EARTH_RADIUS * -5, +PLANET_SPACING * 4, 0.0};
  look_at_heaven.look_at = system.heaven.origin;
  look_at_heaven.up = start.up;
#endif

  constexpr int SCENE_FRAMES = 240;


  Shot shot1;
  shot1.getpos = PathCamera(path);
  shot1.num_frames = SCENE_FRAMES;
  // shot1.easing = EaseOutQuart;

//   Shot shot2;
//   shot2.getpos = LinearCamera(look_at_mars, look_at_heaven);
//   shot2.num_frames = SCENE_FRAMES;
//   shot2.easing = EaseOutQuart;

  std::vector<Shot> shots = {shot1};
  int total_frames = 0;
  for (const Shot &shot : shots) total_frames += shot.num_frames;

  switch (mode) {
  case FRAMES: {
    Timer run_timer;
    Asynchronously async(8);
    for (int s = 0; s < shots.size(); s++) {
      const Shot &shot = shots[s];
      for (int i = 0; i < shot.num_frames; i++) {
        double f = shot.easing(i / (double)(shot.num_frames - 1));
        CameraPosition pos = shot.getpos(f);
        ImageRGBA img = RenderFrame(FRAME_WIDTH, FRAME_HEIGHT, OVERSAMPLE,
                                    pos);
        async.Run([s, i, img = std::move(img)]() {
            img.Save(StringPrintf("solar/solar%d.%d.png", s, i));
            printf("%d.%d\n", s, i);
          });
      }
    }
    double sec = run_timer.Seconds();
    printf("Wrote %d frames in %.1f sec (%.3f sec/frame).\n",
           total_frames, sec, sec / total_frames);

    break;
  }

  case MOSAIC: {
    static constexpr int ACROSS = 8;
    static constexpr int DOWN = 8;
    static constexpr int ONEW = 192 * 2;
    static constexpr int ONEH = 108 * 2;
    static constexpr int OVERSAMPLE = 2;

    // Would be nice if this ensured rendering endpoints...
    ImageRGBA mosaic(ONEW * ACROSS, ONEH * DOWN);
    for (int y = 0; y < DOWN; y++) {
      for (int x = 0; x < ACROSS; x++) {
        int i = y * DOWN + x;
        double all_f = i / (double)(ACROSS * DOWN - 1);
        int global_idx = total_frames * all_f;

        for (int s = 0; s < shots.size(); s++) {
          const Shot &shot = shots[s];
          if (global_idx < shot.num_frames) {
            double f = global_idx / (double)(shot.num_frames - 1);
            CameraPosition pos = shot.getpos(f);
            ImageRGBA img = RenderFrame(ONEW, ONEH, OVERSAMPLE, pos);
            mosaic.CopyImage(x * ONEW, y * ONEH, img);
            break;
          } else {
            global_idx -= shot.num_frames;
          }
        }
      }
    }
    mosaic.Save("solar/mosaic.png");
    printf("Wrote solar/mosaic.png\n");
    break;
  }

  case FRAME_VARIANTS: {

    ArcFour rc(StringPrintf("%lld", time(nullptr)));
    RandomGaussian gauss(&rc);
    static constexpr int ACROSS = 3;
    static constexpr int DOWN = 3;
    // static constexpr int ONEW = 288 * 2;
    // static constexpr int ONEH = 162 * 2;
    static constexpr int ONEW = 1920;
    static constexpr int ONEH = 1080;
    static constexpr int OVERSAMPLE = 2;

    ImageRGBA variants(ONEW * ACROSS, ONEH * DOWN);
    const Shot &shot = shots[target_shot];
    double f = shot.easing(target_frame / (double)(shot.num_frames - 1));
    const CameraPosition center_pos = shot.getpos(f);

    for (int y = 0; y < DOWN; y++) {
      for (int x = 0; x < ACROSS; x++) {

        CameraPosition pos = center_pos;
        if (y > 0 || x > 0) {
          static constexpr double scale = 0.0001;
          auto RV = [&gauss]() {
              return vec3d(gauss.Next() * scale,
                           gauss.Next() * scale,
                           gauss.Next() * scale);
            };

          pos.pos += RV();
          pos.look_at += RV();
          pos.up += RV();
        }

        ImageRGBA img = RenderFrame(ONEW, ONEH, OVERSAMPLE, pos);
        CHECK(img.Width() == ONEW);
        CHECK(img.Height() == ONEH);

        variants.CopyImage(x * ONEW, y * ONEH, img);
      }
    }
    string filename = StringPrintf("variants%d.%d.png",
                                   target_shot, target_frame);
    variants.Save(filename);
    printf("Wrote %s\n", filename.c_str());

    break;
  }

  case ONE_FRAME: {
    Timer run_timer;
    const Shot &shot = shots[target_shot];
    double f = shot.easing(target_frame / (double)(shot.num_frames - 1));
    CameraPosition pos = shot.getpos(f);
    ImageRGBA img = RenderFrame(FRAME_WIDTH, FRAME_HEIGHT, OVERSAMPLE,
                                pos);
    img.Save(StringPrintf("solar/frame%d.%d.png", target_shot, target_frame));
    double sec = run_timer.Seconds();
    printf("Wrote frame in %.1f sec.\n", sec);

    break;
  }
  }

  delete bluemarble;
  delete stars;
  return 0;
}
