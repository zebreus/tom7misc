
#include <vector>
#include <string>
#include <string_view>
#include <memory>

#include "image.h"
#include "util.h"
#include "randutil.h"
#include "threadutil.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"

using namespace std;

static constexpr int WIDTH = 1920;
static constexpr int HEIGHT = 1080;

struct Pos {
  double ox = 0.0, oy = 0.0;
  double dist = 0.0;
  double angle = 0.0;
  double da = 0.01;
};

struct Layer {
  string filename;
  ImageRGBA image;
  Pos pos;
};

int main(int argc, char **argv) {
  ArcFour rc("ddd");

  vector<string> files = Util::ListFiles("dddddd");
  std::sort(files.begin(), files.end());

  vector<Layer> layers;
  for (const string &file : files) {
    printf("%s\n", file.c_str());
    if (Util::MatchesWildcard("dddddd_*_Layer-*.png", file)) {
      string path = Util::dirplus("dddddd", file);
      std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(path));
      CHECK(img.get() != nullptr) << path;
      Layer layer;
      layer.filename = file;
      layer.image = *img;
      layers.push_back(std::move(layer));
    }
  }

  printf("Got %d layers\n", (int)layers.size());

  for (Layer &layer : layers) {
    // Does it touch the sides?
    bool left = false, right = false, top = false, bottom = false;
    double avgx = 0.0, avgy = 0.0;
    int count = 0;
    for (int y = 0; y < HEIGHT; y++) {
      for (int x = 0; x < WIDTH; x++) {
        const auto [r, g, b, a] = layer.image.GetPixel(x, y);
        if (a > 0) {
          avgx += x;
          avgy += y;
          count++;

          if (x == 0) left = true;
          if (x == WIDTH - 1) right = true;
          if (y == 0) top = true;
          if (y == HEIGHT - 1) bottom = true;
        }
      }
    }

    CHECK(count > 0) << layer.filename;
    avgx /= count;
    avgy /= count;

    CHECK(!left || !right) << layer.filename;
    CHECK(!top || !bottom) << layer.filename;

    double ox = 0.0, oy = 0.0;

    // Center of rotation near current center of mass.
    double dx = RandDouble(&rc) * 8.0 - 4.0;
    double dy = RandDouble(&rc) * 8.0 - 4.0;
    double cx = avgx - dx;
    double cy = avgy - dy;

    // But the center needs to be off screen if we are
    // touching an edge.
    if (left) {
      cx = -0.5 * cx;
      ox = cx * 0.5;
    } else if (right) {
      ox = (WIDTH - cx) * 0.25;
      cx = WIDTH + (WIDTH - cx) * 0.5;
    }

    if (top) {
      cy = -0.5 * cy;
      oy = cy * 0.5;
    } else if (bottom) {
      oy = (HEIGHT - cy) * 0.25;
      cy = HEIGHT + (HEIGHT - cy) * 0.5;
    }

    double dist = sqrt(dx * dx + dy * dy);

    double angle = atan2(dy, dx);

    layer.pos.angle = angle;
    layer.pos.da = RandDouble(&rc) * 0.2 - 0.1;
    layer.pos.dist = dist;
    layer.pos.ox = ox;
    layer.pos.oy = oy;
  }

  static constexpr int NUM_FRAMES = 30 * 60;

  ParallelComp(
      NUM_FRAMES,
      [&](int f) {
        ImageRGBA out(WIDTH, HEIGHT);
        out.Clear32(0x4f105cFF);

        for (int z = layers.size() - 1; z >= 0; z--) {
          const Layer &layer = layers[z];
          const Pos &pos = layer.pos;
          const double angle = pos.angle + f * pos.da;
          const int xx = pos.ox + sin(angle) * pos.dist;
          const int yy = pos.oy + cos(angle) * pos.dist;
          out.BlendImage(xx, yy, layer.image);
          // fog
          out.BlendRect32(0, 0, WIDTH, HEIGHT, 0x4f105c06);
        }

        string filename = StringPrintf("ddd-out\\ddd-%d.png", f);
        out.Save(filename);
        printf("%s\n", filename.c_str());
      },
      12);

  printf("Wrote ddd-out\\ddd-*.png\n");
  return 0;
}
