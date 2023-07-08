
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
static constexpr ColorUtil::Gradient ATMOSPHERE {
  GradRGB(0.0f, 0x7700FF),
  GradRGB(0.15f, 0xFF77FF),
  GradRGB(0.25f, 0xffa21e),
  GradRGB(1.0f, 0x000000),
};

using namespace yocto;

static PacTom *pactom = nullptr;

static ImageRGBA *bluemarble = nullptr;
static ImageFRGBA *stars = nullptr;

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

static WideTile *widetile = nullptr;
static Tile *tile = nullptr;


// distance is distance from camera to surface
uint32_t EarthColor(double ux, double uy, double distance) {

  float r, g, b, a_;
  std::tie(r, g, b, a_) = bluemarble->SampleBilinear(
      ux * bluemarble->Width(),
      uy * bluemarble->Height());

  // Two zoom levels; wider goes behind narrower.
  std::tie(r, g, b) = BlendIfInside(widetile, ux, uy, distance, r, g, b);
  std::tie(r, g, b) = BlendIfInside(tile, ux, uy, distance, r, g, b);

  return ColorUtil::FloatsTo32(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

// Camera position.
struct Position {
  double distance = 2.80100000000;

  double a1 = 0.117112122;
  double a2 = 3.008712215;
  double a3 = 2.284319047;
};

static Position InterpolatePos(const Position &a,
                               const Position &b,
                               double f) {
  Position ret;
  ret.distance = std::lerp(a.distance, b.distance, f);
  ret.a1 = std::lerp(a.a1, b.a1, f);
  ret.a2 = std::lerp(a.a2, b.a2, f);
  ret.a3 = std::lerp(a.a3, b.a3, f);
  return ret;
}

struct Shot {
  // Assuming 60fps.
  int num_frames = 60;

  Position start;
  Position end;

  bool draw_atmosphere = true;
  bool draw_mouth = true;

  std::function<double(double)> easing = [](double f) { return f; };
};

static double EaseOutQuart(double f) {
  double sf = (1.0 - f) * (1.0 - f);
  return 1.0 - (sf * sf);
}

static double EaseInOutSin(double f) {
  return 0.5 * (1.0 + sin(3.141592653589 * (f - 0.5)));
}

// 0.0f = fully shadow
inline static float Illumination(const Scene &scene,
                                 const Tetrahedron &mouth,
                                 vec3d start) {
  // XXX
  // I never quite got this to look good, so it's disabled.
  return 1.0f;

  // Could do an actual 3D rotation here, but the distance is made up
  // anyway.
# define EARTH_TILT_RAD 0.41f
# define SUN_ANGLE 2.0
  Sphere sun = {.origin = {double(SUN_DISTANCE * sin(EARTH_TILT_RAD)),
                           double(SUN_DISTANCE * sin(SUN_ANGLE)),
                           double(SUN_DISTANCE * cos(SUN_ANGLE))},
                .radius = SUN_RADIUS};

  // TODO: Sample sun as disc
  ray3d shadow_ray;
  vec3d dir = sun.origin - start;
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
    Position pos,
    bool draw_atmosphere,
    bool draw_mouth) {

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
      img.Height(),
      [&](int py) {
      double yf = py / (double)img.Height();
      for (int px = 0; px < img.Width(); px++) {
        double xf = px / (double)img.Width();

        // image plane and far plane are parallel to the
        // xy plane.
        double far_dist = FAR_DIST;
        // (maybe we want much less to zoom to a
        // region on the planet?)
        double near_dist = -pos.distance;

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
        mat3d rot = Rot(pos.a1, pos.a2, pos.a3);
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

        bool in_mouth = draw_mouth && InTetrahedron(ray.o, mouth);
        // point where we entered atmosphere
        std::optional<vec3d> in_atmosphere =
          (draw_atmosphere && InSphere(ray.o, atmosphere)) ?
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

            auto ExitAtmosphere = [&]() {
                if (!in_atmosphere.has_value()) return;

                vec3d pt = ray.o + pi.distance * ray.d;
                vec3d chord = pt - in_atmosphere.value();
                double len = length(chord);

                double max_len = atmosphere.radius * 2.0;
                // But most chords are only a tiny fraction.
                double frac = len / max_len;
                frac = pow(frac, 2);

                const auto [r, g, b] = ColorUtil::LinearGradient(
                    ATMOSPHERE, frac);

                uint32 color = ColorUtil::FloatsTo32(
                    r, g, b, frac);

                blend.push_back(color);

                in_atmosphere = std::nullopt;
              };

            if (idx == 0) {
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
            } else if (idx == 1) {

              if (in_mouth) {
                // Exit mouth.
                // Is this the inner surface of the sphere?
                vec3d pt = ray.o + pi.distance * ray.d;
                // XXX hard coded location of earth
                double r = length(pt);
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
            } else if (idx == 2) {
              // Hit stars

              const double wrap_x = fmod(pi.uv.x + 0.5, 1.0);

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
            } else if (idx == 3) {
              // Atmosphere
              vec3d pt = ray.o + pi.distance * ray.d;
              if (!in_atmosphere.has_value()) {
                if (draw_atmosphere) {
                  in_atmosphere = {pt};
                }
                // printf("enter atmosphere\n");
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

int main(int argc, char **argv) {

  auto pt = PacTom::FromFiles({"../pac.kml", "../pac2.kml"},
                              "../neighborhoods.kml");
  CHECK(pt.get() != nullptr);
  pactom = pt.release();

  OSM osm;
  for (const string osmfile : {
        "../pittsburgh-center.osm",
        "../pittsburgh-northeast.osm",
        "../pittsburgh-north.osm",
        "../pittsburgh-south.osm",
        "../pittsburgh-southwest.osm",
        "../pittsburgh-west.osm",
       }) osm.AddFile(osmfile);

  InParallel(
      [&](){
        tile = new Tile;
        tile->DrawStreets(*pactom, osm);
        tile->DrawHoods(pactom);
      },
      [](){
        widetile = new WideTile;
      });

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

  enum RenderMode {
    FRAMES,
    ONE_FRAME,
    MOSAIC,
    FRAME_VARIANTS,
  };

  RenderMode mode = ONE_FRAME;
  const int target_shot = 1;
  const int target_frame = 239;
  // static constexpr int FRAME_WIDTH = 2880;
  // static constexpr int FRAME_HEIGHT = 1620;
  static constexpr int FRAME_WIDTH = 1920;
  static constexpr int FRAME_HEIGHT = 1080;
  static constexpr int OVERSAMPLE = 4;

  Position pittsburgh;
  // pittsburgh.distance = 2.80000428964;
  pittsburgh.distance = 2.81133246;
  pittsburgh.a1 = 0.116977384;
  pittsburgh.a2 = 3.008537539;
  pittsburgh.a3 = 2.284259683;

  Position chainsaw;
  chainsaw.distance = 42.8;
  chainsaw.a1 = 0.11;
  chainsaw.a2 = 3.00;
  chainsaw.a3 = 2.27;

  Position pacman;
  /*
  pacman.distance = 14.0;
  pacman.a1 = a1;
  pacman.a2 = a2 + 0.002;
  pacman.a3 = a3 - 0.8;
  */

  pacman.distance = 15.5;
  pacman.a1 = a1;
  pacman.a2 = a2 + 0.002 - 3.141592653589;
  pacman.a3 = a3 - 0.8;

  constexpr int SCENE_FRAMES = 240;

  Shot zoomin_shot;
  zoomin_shot.start = chainsaw;
  zoomin_shot.end = pittsburgh;
  zoomin_shot.num_frames = SCENE_FRAMES;
  zoomin_shot.draw_atmosphere = true;
  zoomin_shot.draw_mouth = false;
  zoomin_shot.easing = EaseOutQuart;

  Shot zoomout_shot;
  zoomout_shot.start = pittsburgh;
  zoomout_shot.end = pacman;
  zoomout_shot.num_frames = SCENE_FRAMES;
  zoomout_shot.draw_atmosphere = false;
  zoomout_shot.draw_mouth = true;
  zoomout_shot.easing = EaseInOutSin;

  std::vector<Shot> shots = {zoomin_shot, zoomout_shot};
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
        Position pos = InterpolatePos(shot.start, shot.end, f);
        ImageRGBA img = RenderFrame(FRAME_WIDTH, FRAME_HEIGHT, OVERSAMPLE,
                                    pos,
                                    shot.draw_atmosphere,
                                    shot.draw_mouth);
        async.Run([s, i, img = std::move(img)]() {
            img.Save(StringPrintf("sphere%d.%d.png", s, i));
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
            Position pos = InterpolatePos(shot.start, shot.end, f);
            ImageRGBA img = RenderFrame(ONEW, ONEH, OVERSAMPLE,
                                        pos,
                                        shot.draw_atmosphere,
                                        shot.draw_mouth);
            mosaic.CopyImage(x * ONEW, y * ONEH, img);
            break;
          } else {
            global_idx -= shot.num_frames;
          }
        }
      }
    }
    mosaic.Save("mosaic.png");
    printf("Wrote mosaic.png\n");
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
    const Position center_pos = InterpolatePos(shot.start, shot.end, f);

    for (int y = 0; y < DOWN; y++) {
      for (int x = 0; x < ACROSS; x++) {

        Position pos = center_pos;
        if (y > 0 || x > 0) {
          const double scale = 0.0001;
          pos.a1 += gauss.Next() * scale;
          pos.a2 += gauss.Next() * scale;
          pos.a3 += gauss.Next() * scale;
          pos.distance += gauss.Next() * scale;
        }

        ImageRGBA img = RenderFrame(ONEW, ONEH, OVERSAMPLE,
                                    pos,
                                    shot.draw_atmosphere,
                                    shot.draw_mouth);
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
    Position pos = InterpolatePos(shot.start, shot.end, f);
    ImageRGBA img = RenderFrame(FRAME_WIDTH, FRAME_HEIGHT, OVERSAMPLE,
                                pos,
                                shot.draw_atmosphere,
                                shot.draw_mouth);
    img.Save(StringPrintf("frame%d.%d.png", target_shot, target_frame));
    double sec = run_timer.Seconds();
    printf("Wrote frame in %.1f sec.\n", sec);

    break;
  }
  }

  delete bluemarble;
  delete stars;
  return 0;
}
