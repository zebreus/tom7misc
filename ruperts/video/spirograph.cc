
#include <cmath>
#include <numbers>
#include <string>

#include "ansi.h"
#include "arcfour.h"
#include "color-util.h"
#include "image.h"
#include "mov-recorder.h"
#include "mov.h"
#include "randutil.h"
#include "status-bar.h"
#include "yocto-math.h"

using vec2 = yocto::vec<double, 2>;
using frame2 = yocto::frame<double, 2>;

struct Piece {
  vec2 v0, v1;
  uint32_t color;
};

// Returns a frame representing rotation by angle around the origin.
static inline frame2 rotation_frame2(double angle) {
  auto s = std::sin(angle);
  auto c = std::cos(angle);
  return {{c, s}, {-s, c}, {0.0, 0.0}};
}

static void Spirograph() {
  ArcFour rc("animate");

  std::string filename = "spirograph.mov";
  StatusBar status(1);

  constexpr int WIDTH = 3840;
  constexpr int HEIGHT = 2160;
  constexpr int LINE_FRAMES = 12;
  MovRecorder rec(filename, WIDTH, HEIGHT, MOV::DURATION_60,
                  MOV::Codec::PNG_CCLIB);

  ImageRGBA img(WIDTH, HEIGHT);
  img.Clear32(0x000000FF);

  const double angle_slice = 2.0 * std::numbers::pi / 7.0;
  static constexpr int LIMIT = 10;
  for (int p = 0; p < LIMIT; p++) {
    Piece piece;
    piece.v0 = vec2((RandDouble(&rc) - 0.5) * (HEIGHT * 0.9),
                    (RandDouble(&rc) - 0.5) * (HEIGHT * 0.9));
    piece.v1 = vec2((RandDouble(&rc) - 0.5) * (HEIGHT * 0.9),
                    (RandDouble(&rc) - 0.5) * (HEIGHT * 0.9));
    piece.color = ColorUtil::HSVAToRGBA32(
        RandDouble(&rc),
        0.5 + RandDouble(&rc) * 0.5,
        0.5 + RandDouble(&rc) * 0.5, 1.0f);
    piece.color &= 0xFFFFFFAA;

    for (int i = 0; i < 7; i++) {
      double a = i * angle_slice;
      frame2 f = rotation_frame2(a);
      vec2 p0 = transform_point(f, piece.v0);
      vec2 p1 = transform_point(f, piece.v1);

      for (int t = 0; t < LINE_FRAMES; t++) {
        vec2 pp1 = (p1 - p0) * (t / (double)LINE_FRAMES) + p0;
        img.BlendThickLine32(p0.x + WIDTH / 2.0,
                             p0.y + HEIGHT / 2.0,
                             pp1.x + WIDTH / 2.0,
                             pp1.y + HEIGHT / 2.0,
                             4.0f, piece.color);
        rec.AddFrame(img);
      }
    }

    status.Progress(p, LIMIT, "Spirograph");
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  Spirograph();

  return 0;
}
