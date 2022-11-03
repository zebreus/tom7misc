
#include "yocto_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <variant>
#include <utility>

#include "threadutil.h"
#include "image.h"
#include "color-util.h"
#include "base/logging.h"

static constexpr int THREADS = 24;

static constexpr double EARTH_RADIUS = 2.8f;

static constexpr double FAR_WIDTH = 100.0f;
// static constexpr double NEAR_WIDTH = 0.01f;
static constexpr double NEAR_WIDTH = 0.1f;

// static constexpr int FRAME_WIDTH = 1920;
// static constexpr int FRAME_HEIGHT = 1080;
static constexpr int FRAME_WIDTH = 2880;
static constexpr int FRAME_HEIGHT = 1620;

static constexpr double ASPECT = FRAME_WIDTH / (double)FRAME_HEIGHT;

static constexpr double NEAR_HEIGHT = NEAR_WIDTH / ASPECT;
static constexpr double FAR_HEIGHT = FAR_WIDTH / ASPECT;

static constexpr float TOTAL_MILES = 800 + 1400 + 1800 + 25;
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

using namespace yocto;

static ImageRGBA *bluemarble = nullptr;

mat3f RotYaw(float a) {
  const float cosa = cos(a);
  const float sina = sin(a);
  return mat3f
    {cosa, -sina, 0.0f,
     sina, cosa,  0.0f,
     0.0f, 0.0f,  1.0f};
}

mat3f RotPitch(float a) {
  const float cosa = cos(a);
  const float sina = sin(a);

  return mat3f
    {cosa,  0.0f, sina,
     0.0f,  1.0f, 0.0f,
     -sina, 0.0f, cosa};
}

mat3f RotRoll(float a) {
  const float cosa = cos(a);
  const float sina = sin(a);

  return mat3f
    {1.0f, 0.0f, 0.0f,
     0.0f, cosa, -sina,
     0.0f, sina, cosa};
}

mat3f Rot(double yaw, double pitch, double roll) {
  mat3f mr = RotRoll(roll);
  mat3f mp = RotPitch(pitch);
  mat3f my = RotYaw(yaw);
  mat3f m = mp * my;
  mat3f n = mr * m;
  return n;
}

struct Tetrahedron {
  vec3f p0, p1, p2, p3;
};

// TODO: Doesn't tell us which face it hit; same UV coordinates no matter
// what.
inline prim_intersection intersect_tetrahedron(
    const ray3f& ray, const Tetrahedron &tet) {

  //    1---3
  //   / \ /
  //  0---2

  // TODO: These all have the same winding order, but probably we want
  // to pick them so that they have consistent UV coordinates or something.
  prim_intersection p0 = intersect_triangle(ray, tet.p0, tet.p1, tet.p2);
  prim_intersection p1 = intersect_triangle(ray, tet.p1, tet.p3, tet.p2);
  prim_intersection p2 = intersect_triangle(ray, tet.p0, tet.p3, tet.p1);
  prim_intersection p3 = intersect_triangle(ray, tet.p2, tet.p3, tet.p0);

  // take the closest one
  for (const auto &p : {p1, p2, p3}) {
    if (p.hit && p.distance < p0.distance) p0 = p;
  }

  return p0;
}

struct Sphere {
  vec3f origin = {0.0f, 0.0f, 0.0f};
  float radius = 0.0f;
};

struct Prim {
  std::variant<Sphere, Tetrahedron> v;
};

struct Scene {
  std::vector<Prim> prims;

  // Updates ray with distance of next intersection (if any).
  std::pair<int, prim_intersection> NextIntersection(
      ray3f *ray) {
    // Obviously this should use spatial data structures if the
    // scene is big!
    int isect_idx = -1;
    prim_intersection isect;
    isect.distance = flt_max;
    isect.hit = false;
    for (int idx = 0; idx < prims.size(); idx++) {
      const Prim &p = prims[idx];
      if (const Sphere *sphere = std::get_if<Sphere>(&p.v)) {
        prim_intersection pi =
          intersect_sphere(*ray, sphere->origin, sphere->radius);
        if (pi.hit && pi.distance < isect.distance) {
          isect = pi;
          isect_idx = idx;
        }

      } else if (const Tetrahedron *tet = std::get_if<Tetrahedron>(&p.v)) {
        prim_intersection pi =
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
    if (isect.hit) ray->tmin = isect.distance + 0.00001;
    return make_pair(isect_idx, isect);
  }
};


static ImageRGBA RenderFrame(
    // Distance from Earth
    double distance,
    // Rotation of Earth along its axis
    double angle) {
  constexpr int OVERSAMPLE = 2;

  ImageRGBA img(FRAME_WIDTH * OVERSAMPLE, FRAME_HEIGHT * OVERSAMPLE);

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
  vec3f tp1 = {0, -5, 0};
  vec3f tp2 = {0, +5, 0};
  vec3f tp3 = {5, 0, +5};
  vec3f tp4 = {5, 0, -5};
  Tetrahedron mouth(tp1, tp2, tp3, tp4);
  scene.prims.emplace_back(mouth);

  ParallelComp2D(
      img.Width(), img.Height(),
      [&img, &scene](int px, int py) {
        double xf = px / (double)img.Width();
        double yf = py / (double)img.Height();

        // image plane and far plane are parallel to the
        // xy plane.
        float far_dist = 100.0f;
        // (maybe we want much less to zoom to a
        // region on the planet?)
        float near_dist = -20.1f;

        float near_x = std::lerp(-NEAR_WIDTH * 0.5, NEAR_WIDTH * 0.5, xf);
        float near_y = std::lerp(-NEAR_HEIGHT * 0.5, NEAR_HEIGHT * 0.5, yf);

        float far_x = std::lerp(-FAR_WIDTH * 0.5, FAR_WIDTH * 0.5, xf);
        float far_y = std::lerp(-FAR_HEIGHT * 0.5, FAR_HEIGHT * 0.5, yf);

        vec3f near_pt = {near_x, near_y, -near_dist};
        vec3f far_pt = {far_x, far_y, -far_dist};
        // vec3f far_pt = {0, 0, 0};

        ray3f ray;
        ray.o = near_pt;
        ray.d = far_pt - near_pt;
        ray.tmin = 0.001f;
        ray.tmax = 10.0f;

        // Default orientation would be facing the south pole.
        // Rotate...
        // double axis = 0.0;
        mat3f rot = Rot(0.0, 3.14159 + 0.25, 3.14159 * 0.5);
        // Rotate around origin.
        frame3f rot_frame = make_frame(rot, {0.0, 0.0, 0.0});
        ray = transform_ray(rot_frame, ray);

        bool in_mouth = false;
        for (;;) {
          const auto [idx, pi] = scene.NextIntersection(&ray);
          if (!pi.hit) {
            // space
            img.SetPixel32(px, py, 0x111100FF);
            break;
          }

          if (idx == 0) {
            // sphere
            if (in_mouth) {
              // Ignore the surface of the sphere while
              // inside the mouth cutout.
            } else {
              // Normal hit on sphere.
              uint32_t color = bluemarble->GetPixel32(
                  pi.uv.x * (double)bluemarble->Width(),
                  pi.uv.y * (double)bluemarble->Height());

              img.SetPixel32(px, py, color);
              break;
            }
          } else if (idx == 1) {

            if (in_mouth) {
              // Exit mouth.
              // Is this the inner surface of the sphere?
              vec3f pt = ray.o + pi.distance * ray.d;
              // XXX hard coded location of earth
              float r = length(pt);
              if (r <= EARTH_RADIUS) {
                uint32_t color = ColorUtil::LinearGradient32(
                    INNER_EARTH, r / EARTH_RADIUS);
                img.SetPixel32(px, py, color);
                // But make it darker
                img.BlendPixel32(px, py, 0x48270677);
                in_mouth = false;
                break;
              } else {
                // Exiting to free space
                in_mouth = false;
              }

            } else {
              // Enter mouth.
              in_mouth = true;
            }
          } else {
            CHECK(false) << "unknown prim";
          }
        }

      }, THREADS);

  return img.ScaleDownBy(OVERSAMPLE);
}

int main(int argc, char **argv) {

  printf("Loading marble...\n");
  // bluemarble = ImageRGBA::Load("world_shaded_43k.jpg");
  bluemarble = ImageRGBA::Load("bluemarble.png");
  CHECK(bluemarble != nullptr);
  printf("Done.\n");

  ImageRGBA img = RenderFrame(100, 0);
  img.Save("sphere.png");

  delete bluemarble;
  return 0;
}
