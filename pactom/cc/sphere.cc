
#include "yocto_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "threadutil.h"
#include "image.h"
#include "color-util.h"

static constexpr int THREADS = 24;

static constexpr double EARTH_RADIUS = 2.8f;

static constexpr double FAR_WIDTH = 100.0f;
// static constexpr double NEAR_WIDTH = 0.01f;
static constexpr double NEAR_WIDTH = 0.1f;

static constexpr double ASPECT = 1920.0 / 1080.0;

static constexpr double NEAR_HEIGHT = NEAR_WIDTH / ASPECT;
static constexpr double FAR_HEIGHT = FAR_WIDTH / ASPECT;

static constexpr int FRAME_WIDTH = 1920;
static constexpr int FRAME_HEIGHT = 1080;

using namespace yocto;

static ImageRGBA *bluemarble = nullptr;

mat3f RotYaw(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);
  return mat3f
    {cosa, -sina, 0.0,
     sina, cosa, 0.0,
     0.0, 0.0, 1.0};
}

mat3f RotPitch(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);

  return mat3f
    {cosa, 0.0, sina,
     0.0, 1.0, 0.0,
     -sina, 0.0, cosa};
}

mat3f RotRoll(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);

  return mat3f
    {1.0, 0.0, 0.0,
     0.0, cosa, -sina,
     0.0, sina, cosa};
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

  ParallelComp2D(
      img.Width(), img.Height(),
      [&img](int px, int py) {
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

        // test: intersect sphere
        /*
          vec2f uv       = {0, 0};
          float distance = flt_max;
          bool  hit      = false;
        */
        prim_intersection s_isect =
          intersect_sphere(ray, {0.0, 0.0, 0.0}, EARTH_RADIUS);

        if (s_isect.hit) {
          #if 0
          float r = std::clamp(s_isect.uv.x, 0.0f, 1.0f);
          float b = std::clamp(s_isect.uv.y, 0.0f, 1.0f);

          img.SetPixel32(px, py,
                         ColorUtil::FloatsTo32(r, 0.0f, b, 1.0f));
          #endif
          uint32_t color = bluemarble->GetPixel32(
              s_isect.uv.x * (double)bluemarble->Width(),
              s_isect.uv.y * (double)bluemarble->Height());

          img.SetPixel32(px, py, color);

        } else {
          img.SetPixel32(px, py, 0x111100FF);
        }

        vec3f tp1 = {0, -2.5, 0};
        vec3f tp2 = {0, +2.5, 0};
        vec3f tp3 = {5, 0, -5};
        vec3f tp4 = {5, 0, +5};

        Tetrahedron tet(tp1, tp2, tp3, tp4);
        prim_intersection t_isect = intersect_tetrahedron(ray, tet);

        if (t_isect.hit) {
          float r = std::clamp(t_isect.uv.x, 0.0f, 1.0f);
          float b = std::clamp(t_isect.uv.y, 0.0f, 1.0f);

          img.BlendPixel32(px, py,
                           ColorUtil::FloatsTo32(r, 0.0f, b, 0.2f));
        }

        // TODO:
        // For each pixel, compute what part of the
        // truncated sphere it sees, if any.

        // Compute the texture coordinate at that pixel.
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
