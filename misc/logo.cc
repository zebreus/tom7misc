
#include <functional>

#include "image.h"
#include "arcfour.h"
#include "randutil.h"
#include "base/logging.h"
#include "threadutil.h"
#include "base/stringprintf.h"

static constexpr double PI = 3.141592653589;

static constexpr double ToRad(double d) {
  constexpr double CONV = ((1.0 / 360.0) * 2.0 * PI);
  return d * CONV;
}

struct Mat3 {
  double
    a, b, c,
    d, e, f,
    g, h, i;
};

// Column vector
struct Vec3 {
  double
    x,
    y,
    z;
};

static inline Vec3 Sub3(Vec3 p1, Vec3 p2) {
  return Vec3{.x = p1.x - p2.x, .y = p1.y - p2.y, .z = p1.z - p2.z};
}

static inline Vec3 Add3(Vec3 p1, Vec3 p2) {
  return Vec3{.x = p1.x + p2.x, .y = p1.y + p2.y, .z = p1.z + p2.z};
}

static inline Vec3 Scale3(Vec3 p1, double s) {
  return Vec3{.x = p1.x * s, .y = p1.y * s, .z = p1.z * s};
}

static inline double Dist3(Vec3 p1, Vec3 p2) {
  Vec3 d3 = Sub3(p1, p2);
  return d3.x * d3.x + d3.y * d3.y + d3.z * d3.z;
}

static inline Vec3 Norm3(Vec3 p1) {
  double inv_len = 1.0 / Dist3(p1, Vec3(0, 0, 0));
  return Vec3{.x = p1.x * inv_len, .y = p1.y * inv_len, .z = p1.z * inv_len};
}


Mat3 Mul33(Mat3 m, Mat3 n) {
  return Mat3{
    m.a*n.a+m.b*n.d+m.c*n.g, m.a*n.b+m.b*n.e+m.c*n.h, m.a*n.c+m.b*n.f+m.c*n.i,
    m.d*n.a+m.e*n.d+m.f*n.g, m.d*n.b+m.e*n.e+m.f*n.h, m.d*n.c+m.e*n.f+m.f*n.i,
    m.g*n.a+m.h*n.d+m.i*n.g, m.g*n.b+m.h*n.e+m.i*n.h, m.g*n.c+m.h*n.f+m.i*n.i};
}

Mat3 RotYaw(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);
  return Mat3
    {cosa, -sina, 0.0,
     sina, cosa, 0.0,
     0.0, 0.0, 1.0};
}

Mat3 RotPitch(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);

  return Mat3
    {cosa, 0.0, sina,
     0.0, 1.0, 0.0,
     -sina, 0.0, cosa};
}

Mat3 RotRoll(double a) {
  const double cosa = cos(a);
  const double sina = sin(a);

  return Mat3
    {1.0, 0.0, 0.0,
     0.0, cosa, -sina,
     0.0, sina, cosa};
}

Vec3 Mat33TimesVec3(Mat3 m, Vec3 v) {
  return Vec3
    {m.a * v.x + m.b * v.y + m.c * v.z,
     m.d * v.x + m.e * v.y + m.f * v.z,
     m.g * v.x + m.h * v.y + m.i * v.z};
}

Mat3 Rot(double yaw, double pitch, double roll) {
  Mat3 mr = RotRoll(roll);
  Mat3 mp = RotPitch(pitch);
  Mat3 my = RotYaw(yaw);
  Mat3 m = Mul33 (mp, my);
  Mat3 n = Mul33 (mr, m);
  return n;
}


// Implicit surface
using Surface = std::function<double(Vec3)>;

Surface CSGSphere(Vec3 center, double rad) {
  return Surface([center, rad](Vec3 sample) -> double {
      double dist = Dist3(center, sample);
      // Now we look at the distance from dist to the radius.
      // 0 is the surface itself, and outside of the surface should
      // be positive:
      return dist - rad;
    });
}

// This might work for the particular case I have in mind here,
// but the logic just isn't right. It's easy to compute "am I inside?"
// and thus the boundary at 0.0 is correct (and the signs are correct)
// but we don't actually get a distance to the surface.
Surface CSGMinus(Surface a, Surface b) {
  return Surface([a, b](Vec3 sample) -> double {
      double sa = a(sample);
      double sb = b(sample);
      // If we are inside the cut-out part, then we are
      // outside the surface.
      if (sb < 0.0) {
        // If we are also inside the positive part, then
        // the closest surface is the boundary.
        if (sa < 0.0) {
          return -sb;
        } else {
          // Correct sign here, but wrong magnitude...
          return sa;
        }

      } else {
        // We may be closer to the cut-out part's boundary than
        // the positive part.
        if (sa < -sb) {
          return -sb;
        }

        // Otherwise it's just the positive part. Note that
        // "sidereal" distances will be wrong (underestimate)
        // if the closest point is in the cut-out part. How to
        // fix this? Does it matter?
        return sa;
      }
    });
}

struct ZImage {
  // Typically, depth is much shallower than width/height.
  ZImage(int width, int height, int depth) : width(width),
                                             height(height),
                                             depth(depth) {
    images.reserve(depth);
    for (int i = 0; i < depth; i++) {
      ImageRGBA img(width, height);
      img.Clear32(0x00000000);
      images.push_back(std::move(img));
    }
  }

  // Ignoring z for now
  int width = 0, height = 0, depth = 0;
  std::vector<ImageRGBA> images;

  ImageRGBA &ImageAt(int z) {
    return images[std::clamp(z, 0, (int)images.size() - 1)];
  }

  void BlendPixel32(int x, int y, int z, uint32_t c) {
    ImageAt(z).BlendPixel32(x, y, c);
  }

  ImageRGBA Composite(uint32_t bgcolor = 0x000000FF,
                      uint32_t fog_color = 0x00000000) const {
    ImageRGBA out(width, height);
    out.Clear32(bgcolor);

    // From back to front.
    // We go layer-by-layer for better memory locality.
    for (int z = (int)images.size() - 1; z >= 0; z--) {
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          if (fog_color) {
            out.BlendPixel32(x, y, fog_color);
          }
          out.BlendPixel32(x, y, images[z].GetPixel32(x, y));
        }
      }
    }
    return out;
  }
};

ImageRGBA RenderOne(int frame, float yaw, float pitch, float roll) {
  // printf("%.3f %.3f %.3f\n", yaw, pitch, roll);
  const Mat3 rm = Rot(yaw, pitch, roll);
  auto Rotate = [rm](Vec3 v) -> Vec3 {
      return Mat33TimesVec3(rm, v);
    };

  Surface logo = CSGMinus(CSGSphere(Vec3(0, 0, 0), 1.0),
                          CSGSphere(Vec3(1.0, 0, 0), 0.25));

  // We render by drawing a bunch of rays to what we know is the
  // center of the object.
  //  Vec3 center(0, 0, 0);
  double outside = 2.0;


  static constexpr int IMG_WIDTH = 1920 * 2;
  static constexpr int IMG_HEIGHT = 1080 * 2;
  static constexpr int IMG_SQUARE = std::min(IMG_WIDTH, IMG_HEIGHT);
  static constexpr int IMG_DEPTH = 128;
  ZImage zimg(IMG_WIDTH, IMG_HEIGHT, 256);

  auto Plot = [&zimg](Vec3 pt, uint32_t c) {
      // scale the object
      pt = Scale3(pt, IMG_SQUARE * 0.4);
      // and place in the center of the screen
      Vec3 screen_pt = Add3(pt, Vec3(IMG_WIDTH * 0.5, IMG_HEIGHT * 0.5, 0.0));

      // But squash depth.
      float screenz = screen_pt.z * (IMG_DEPTH / (float)IMG_SQUARE);

      int sx = std::round(screen_pt.x);
      int sy = std::round(screen_pt.y);

      int sz = (int)std::round(screenz);
      zimg.ImageAt(sz).BlendRect32(sx - 3, sy - 3, 7, 7, c);
    };

  // ArcFour rc(StringPrintf("test.%d", frame));
  ArcFour rc("test");
  static constexpr int NUM_RAYS = 50000;
  // random in [-1, 1].
  auto Rand11 = [&rc]() {
      return RandDouble(&rc) * 2.0 - 1.0;
    };

  auto SampleRay = [&Rand11]() -> Vec3 {
      for (;;) {
        // Rejection sampling to get samples in unit ball.
        Vec3 v;
        v.x = Rand11();
        v.y = Rand11();
        v.z = Rand11();
        if (Dist3(Vec3(0, 0, 0), v) >= 1.0) continue;
        return Norm3(v);
      }
    };

  for (int i = 0; i < NUM_RAYS; i++) {
    Vec3 v = Rotate(SampleRay());

    // binary search on the scale of that vector.
    double lb = 0.0;
    double ub = outside;
    // Loop invariants.

    while (ub - lb > 0.0001) {
      CHECK(logo(Scale3(v, lb)) < 0.0);
      CHECK(logo(Scale3(v, ub)) > 0.0);

      double mid = (lb + ub) * 0.5;
      double d = logo(Scale3(v, mid));
      if (d < 0.0) {
        lb = mid;
      } else {
        ub = mid;
      }
    }

    Plot(Rotate(Scale3(v, lb)), 0x44FF4477);
  }

  // zimg.AddFog(0x0000FF11);
  return zimg.Composite(0x000000FF, 0x25000010);
}

static void Render() {
  constexpr double YAW = ToRad(15.0);
  constexpr double PITCH = ToRad(15.0);
  constexpr double ROLL = ToRad(15.0);

  Asynchronously async(12);
  static constexpr int NUM_FRAMES = 600;
  for (int i = 0; i < NUM_FRAMES; i++) {
    async.Run([i]() {
        float t = i / (float)NUM_FRAMES;
        ImageRGBA frame = RenderOne(i,
                                    YAW,
                                    ToRad(8.0 + 360.0 * t),
                                    ROLL);
        frame.Save(StringPrintf("logo%03d.png", i));
        printf("%d\n", i);
      });

  }

}


int main(int argc, char **argv) {

  Render();

  return 0;
}
