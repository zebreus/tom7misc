
#include "rendering.h"

#include <cmath>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "base/logging.h"
#include "image.h"

Rendering::~Rendering() {}

static constexpr int IMAGE_WIDTH = 1920;
static constexpr int IMAGE_HEIGHT = 1080;
// Render at higher resolution and resize before output for
// anti-aliasing.
static constexpr int RENDER_SCALE = 2;

namespace {

class ImageRendering : public Rendering {
 public:
  explicit ImageRendering(std::string_view base_filename)
    : base_filename(base_filename),
      background(IMAGE_WIDTH * RENDER_SCALE, IMAGE_HEIGHT * RENDER_SCALE) {
    ClearBackground();
  }

  void SetBackground(const ImageRGBA &img) override {
    background.Clear32(0x000000FF);
    background.BlendImage(0, 0, img.ScaleBy(RENDER_SCALE));
  }

  void ClearBackground() override {
    background.Clear32(0x000000FF);
  }

  void RenderScene(vec2f viewport_min, vec2f viewport_max,
                   std::span<const Triangle> scene) override {
    const int w = IMAGE_WIDTH * RENDER_SCALE;
    const int h = IMAGE_HEIGHT * RENDER_SCALE;
    ImageRGBA img = background;
    CHECK(img.Width() == w && img.Height() == h);

    const float vw = viewport_max.x - viewport_min.x;
    const float vh = viewport_max.y - viewport_min.y;

    CHECK(vw != 0.0f && vh != 0.0f);

    auto MapX = [&](float x) -> int {
        return (int)(std::round((x - viewport_min.x) / vw * w));
      };
    auto MapY = [&](float y) -> int {
        return (int)(std::round((y - viewport_min.y) / vh * h));
      };

    for (const auto &t : scene) {
      img.BlendTriangle32(MapX(t.a.x), MapY(t.a.y),
                          MapX(t.b.x), MapY(t.b.y),
                          MapX(t.c.x), MapY(t.c.y),
                          t.rgba);
    }

    ImageRGBA out = img.ScaleDownBy(RENDER_SCALE);
    std::string filename =
      std::format("{}-{:04d}.png", base_filename, counter++);
    out.Save(filename);
  }

  vec2f ScreenToWorld(vec2f viewport_min, vec2f viewport_max,
                      int x, int y) override {
    float tx = x / (float)IMAGE_WIDTH;
    float ty = y / (float)IMAGE_HEIGHT;

    return vec2f{
      viewport_min.x + tx * (viewport_max.x - viewport_min.x),
      viewport_min.y + ty * (viewport_max.y - viewport_min.y),
    };
  }

  ~ImageRendering() override = default;

 private:
  std::string base_filename;
  ImageRGBA background;
  int counter = 0;
};

}  // namespace

std::unique_ptr<Rendering> CreateImageRendering(
    std::string_view base_filename) {
  return std::make_unique<ImageRendering>(base_filename);
}

