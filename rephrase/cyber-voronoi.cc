
#include "integer-voronoi.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>
#include <vector>

#include "arcfour.h"
#include "image.h"
#include "randutil.h"
#include "threadutil.h"

struct Ball {
  float x = 0.0;
  float y = 0.0;
  float dx = 0.0;
  float dy = 0.0;
};

static void MakeFrames() {
  Asynchronously async(10);

  static constexpr int WIDTH = 1920;
  static constexpr int HEIGHT = 1080;

  static constexpr int XMIN = 1;
  static constexpr int YMIN = 1;
  static constexpr int XMAX = WIDTH - 1;
  static constexpr int YMAX = HEIGHT - 1;

  ArcFour rc("hi");

  std::vector<Ball> balls;
  for (int i = 0; i < 100; i++) {
    balls.emplace_back(Ball{
        .x = RandFloat(&rc) * (float)WIDTH,
        .y = RandFloat(&rc) * (float)HEIGHT,
        .dx = RandFloat(&rc) * 4.0f - 2.0f,
        .dy = RandFloat(&rc) * 4.0f - 2.0f,
      });
  }

  auto Render = [&balls]() -> ImageF {
      Image1 bitmap(WIDTH, HEIGHT);
      bitmap.Clear(false);
      for (const Ball &ball : balls) {
        bitmap.SetPixel(std::round(ball.x), std::round(ball.y), true);
      }

      return IntegerVoronoi::DistanceField(bitmap);
    };

  // Render one frame to get the range of distances.
  ImageF init = Render();
  float maxx = 0.0f;
  for (int y = 0; y < init.Height(); y++) {
    for (int x = 0; x < init.Width(); x++) {
      maxx = std::max(maxx, init.GetPixel(x, y));
    }
  }

  const float invmax = 1.0f / maxx;
  auto Normalize = [invmax](ImageF *img) {
      for (int y = 0; y < img->Height(); y++) {
        for (int x = 0; x < img->Width(); x++) {
          img->SetPixel(x, y, img->GetPixel(x, y) * invmax);
        }
      }
    };


  for (int i = 0; i < 60 * 6; i++) {
    ImageF df = Render();

    async.Run([&Normalize, df = std::move(df), i]() mutable {
        Normalize(&df);
        df.Make8Bit().GreyscaleRGBA().Save(
            std::format("voronoi{:05d}.png", i));
      });

    // Simulate.
    for (Ball &ball : balls) {
      ball.x += ball.dx;
      ball.y += ball.dy;
      if (ball.x < XMIN) {
        ball.x = XMIN - ball.x;
        ball.dx = -ball.dx;
      }

      if (ball.y < YMIN) {
        ball.y = YMIN - ball.y;
        ball.dy = -ball.dy;
      }

      if (ball.x > XMAX) {
        ball.x = 2 * XMAX - ball.x;
        ball.dx = -ball.dx;
      }

      if (ball.y > YMAX) {
        ball.y = 2 * YMAX - ball.y;
        ball.dy = -ball.dy;
      }
    }
  }
}

int main(int argc, char **argv) {

  MakeFrames();

  return 0;
}
