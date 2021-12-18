
#include "network.h"

#include <string>
#include <vector>
#include <cstdio>
#include <memory>
#include <optional>

#include "network-gpu.h"
#include "util.h"
#include "image.h"
#include "lines.h"
#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"

using namespace std;

// Given a line segment and a single endpoint of another line,
// try all other endpoints and plot them on the image.

// One edge of image
static constexpr int IMG_SIZE = 1024;
// Samples range from [-GAMUT, GAMUT].
static constexpr double GAMUT = 1.0f;

static constexpr int ROWS_PER_BATCH = 64;
static_assert(IMG_SIZE % ROWS_PER_BATCH == 0);

static CL *cl = nullptr;

struct IntersectionModel {
  static constexpr int INPUT_SIZE = 8;
  static constexpr int OUTPUT_SIZE = 3;
  static constexpr int EXAMPLES_PER_ROUND = ROWS_PER_BATCH * IMG_SIZE;

  IntersectionModel(const string &model) {
    net.reset(Network::ReadFromFile(model));
    CHECK(net.get() != nullptr);
    net_gpu.reset(new NetworkGPU(cl, net.get()));
    net_gpu->SetVerbose(false);
    
    forward_cl.reset(new ForwardLayerCL(cl, net_gpu.get()));

    // Run many examples in parallel.
    training.reset(
      new TrainingRoundGPU(EXAMPLES_PER_ROUND, cl, *net));
  }

  ImageRGBA IntersectionImage(
      double x0, double y0, double x1, double y1,
      double x2, double y2) {
    ImageRGBA image(IMG_SIZE, IMG_SIZE);
    image.Clear32(0x000000FF);

    auto CoordToScreen = [](float c) {
        double f = (c + GAMUT) / (GAMUT + GAMUT);
        int p = round(f * IMG_SIZE);
        return p;
      };
    auto ScreenToCoord = [](int p) {
        double f = p / (double)IMG_SIZE;
        double c = f * (GAMUT + GAMUT) - GAMUT;
        return c;
      };

    auto Dot = [&](double fx, double fy) {
        int cx = CoordToScreen(x2);
        int cy = CoordToScreen(y2);

        image.BlendRect32(cx - 1, cy - 1, 3, 3, 0xFFFFFFFF);
        image.BlendBox32(cx - 2, cy - 2, 5, 5, 0xFFFFFFFF, 0xFFFFFF4F);
      };
    
    for (auto [x, y] : Line<int>{CoordToScreen(x0), CoordToScreen(y0),
          CoordToScreen(x1), CoordToScreen(y1)}) {
      // image.BlendPixel32(x, y, 0xFFFFFFFF);
      // Thick line.
      image.BlendRect32(x - 1, y - 1, 3, 3, 0xFFFFFFFF);      
    }
    
    Dot(x0, y0);
    Dot(x1, y1);

    Dot(x2, y2);

    for (int batch = 0; batch < IMG_SIZE / ROWS_PER_BATCH; batch++) {
      // if (batch % 10 == 0) printf("Batch %d\n", batch);

      std::vector<float> input;
      input.reserve(EXAMPLES_PER_ROUND * INPUT_SIZE);

      for (int ry = 0; ry < ROWS_PER_BATCH; ry++) {
        for (int x = 0; x < IMG_SIZE; x++) {
          // x,y in image space
          const int y = batch * ROWS_PER_BATCH + ry;
          const float x3 = ScreenToCoord(x);
          const float y3 = ScreenToCoord(y);

          input.push_back(x0);
          input.push_back(y0);
          input.push_back(x1);
          input.push_back(y1);
          input.push_back(x2);
          input.push_back(y2);
          input.push_back(x3);
          input.push_back(y3);
        }
      }

      CHECK(input.size() == EXAMPLES_PER_ROUND * INPUT_SIZE);
      training->LoadInputs(input);
      for (int src_layer = 0;
           src_layer < net->layers.size() - 1;
           src_layer++) {
        forward_cl->RunForward(training.get(), src_layer);
      }

      std::vector<float> out;
      out.resize(EXAMPLES_PER_ROUND * OUTPUT_SIZE);
      training->ExportOutputs(&out);

      // Draw in image
      for (int ry = 0; ry < ROWS_PER_BATCH; ry++) {
        for (int x = 0; x < IMG_SIZE; x++) {
          // x,y in image space
          const int y = batch * ROWS_PER_BATCH + ry;

          int idx = (ry * IMG_SIZE + x) * OUTPUT_SIZE;
          CHECK(idx < out.size());
          float isect = out[idx + 0];

          float ifx = out[idx + 1];
          float ify = out[idx + 2];        

          // Actual intersection
          int g = std::clamp((int)round(isect * 255.0), 0, 255);
          int r = 255 - g;

          const float x3 = ScreenToCoord(x);
          const float y3 = ScreenToCoord(y);
          auto ao = LineIntersection<double>(x0, y0, x1, y1, x2, y2, x3, y3);
          if (r < 10) {
            if (ao.has_value()) {
              // Get error
              auto [aifx, aify] = ao.value();

              #if 0
              // float / aa version
              double dx = aifx - ifx;
              double dy = aify - ify;
              
              image.BlendLineAA(x, y,
                                CoordToScreen(x3 + dx), CoordToScreen(y3 + dy),
                                r, g, 0x00, 0x07);

              #else
              // integer version
              
              // Predicted intersection.
              int px = CoordToScreen(ifx);
              int py = CoordToScreen(ify);

              auto dx = CoordToScreen(aifx) - px;
              auto dy = CoordToScreen(aify) - py;
              for (auto [ix, iy] : Line<int>{x, y, x + dx, y + dy}) {
                image.BlendPixel(ix, iy, r, g, 0x00, 0x07);
              }

            } else {
              image.BlendPixel(x, y, r, g, 0x7F, 0x70);
            }            

          } else {
            image.BlendPixel(x, y, r, g, 0x00, 0x20);
          }
        }
      }
    }
    return image;
  }

private:  
  std::unique_ptr<Network> net;
  std::unique_ptr<NetworkGPU> net_gpu;
  std::unique_ptr<ForwardLayerCL> forward_cl;
  std::unique_ptr<TrainingRoundGPU> training;
};


int main(int argc, char **argv) {
  cl = new CL;

  #if 0
  CHECK(argc == 8) << "./test-intersection.exe model.val x0 y0 x1 y1  x2 y2\n";
  auto Coord = [](const char *a) {
      auto ao = Util::ParseDoubleOpt(a);
      CHECK(ao.has_value()) << "Expected double: " << a;
      return ao.value();
    };
  const double x0 = Coord(argv[2]);
  const double y0 = Coord(argv[3]);  
  const double x1 = Coord(argv[4]);
  const double y1 = Coord(argv[5]);  
  const double x2 = Coord(argv[6]);
  const double y2 = Coord(argv[7]);  

  {
    IntersectionModel intersection_model(argv[1]);
    ImageRGBA image = intersection_model.IntersectionImage(
        x0, y0, x1, y1, x2, y2);
    image.Save("intersection.png");
  }
  #endif

  // Animate
  {
    static constexpr int NUM_FRAMES = 300;
    static constexpr double TWO_PI = 2.0 * 3.141592653589;
    const string model = "gpu-test-net-200000.val";
    IntersectionModel intersection_model(model);

    double cx0 = -0.1;
    double cy0 = -0.1;
    double r0 = 0.4;
    double phase0 = 0.25 * TWO_PI;

    double cx1 = 0.5;
    double cy1 = 0.5;
    double r1 = 0.15;
    double phase1 = 0.0 * TWO_PI;

    for (int t = 0; t < NUM_FRAMES; t++) {
      double a0 = phase0 + (t / (double)NUM_FRAMES) * TWO_PI;
      double x0 = cx0 + r0 * sin(sin(a0));
      double y0 = cy0 + r0 * cos(a0);

      double a1 = phase1 + (t / (double)NUM_FRAMES) * TWO_PI;
      double x1 = cx1 + r1 * sin(a1);
      double y1 = cy1 + r1 * cos(a1);

      double a2 = (t / (double)NUM_FRAMES) * TWO_PI;
      double x2 = cos(a2);
      double y2 = cos(a2 * 2.0)/3.0 + sin(a2)/2.0;
      
      ImageRGBA img = intersection_model.IntersectionImage(
          x0, y0, x1, y1, x2, y2);
      img.Save(StringPrintf("frame-%03d.png", t));
      printf("Frame %d\n", t);
    }
  }
  
  delete cl;
  return 0;
}
