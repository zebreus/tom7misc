
#include "yocto_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "threadutil.h"
#include "image.h"
#include "color-util.h"

static constexpr int THREADS = 24;

static constexpr double EARTH_RADIUS = 1.0f;
static constexpr double FINAL_SCREEN_DISTANCE = 0.0f;

static constexpr double FAR_WIDTH = 100.0f;
// static constexpr double NEAR_WIDTH = 0.01f;
static constexpr double NEAR_WIDTH = 0.1f;

static constexpr double ASPECT = 1920.0 / 1080.0;

static constexpr double NEAR_HEIGHT = NEAR_WIDTH / ASPECT;
static constexpr double FAR_HEIGHT = FAR_WIDTH / ASPECT;

static constexpr int FRAME_WIDTH = 1920;
static constexpr int FRAME_HEIGHT = 1080;

using namespace yocto;

static ImageRGBA RenderFrame(
    // Distance from Earth
    double distance,
    // Rotation of Earth along its axis
    double angle) {
  constexpr int OVERSAMPLE = 3;

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
        float near_dist = -10.1f;

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

        // test: intersect sphere
        /*
          vec2f uv       = {0, 0};
          float distance = flt_max;
          bool  hit      = false;
        */
        prim_intersection isect =
          intersect_sphere(ray, {0.0, 0.0, 0.0}, EARTH_RADIUS);

        if (isect.hit) {
          float r = std::clamp(isect.uv.x, 0.0f, 1.0f);
          float b = std::clamp(isect.uv.y, 0.0f, 1.0f);

          img.SetPixel32(px, py,
                         ColorUtil::FloatsTo32(r, 0.0f, b, 1.0f));

        } else {
          img.SetPixel32(px, py, 0x111100FF);
        }

        // TODO:
        // For each pixel, compute what part of the
        // truncated sphere it sees, if any.

        // Compute the texture coordinate at that pixel.
      }, THREADS);

  return img.ScaleDownBy(OVERSAMPLE);
}

int main(int argc, char **argv) {

  ImageRGBA img = RenderFrame(100, 0);
  img.Save("sphere.png");

  return 0;
}
