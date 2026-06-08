
#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <variant>
#include <vector>

#include "SDL_main.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "randutil.h"
#include "yocto-math.h"

#include "initialization.h"
#include "inputs.h"
#include "rendering.h"
#include "utf8.h"


using vec2f = yocto::vec2f;

struct Scene {

  // Dimensions of the world.
  static constexpr float WIDTH = 1920.0;
  static constexpr float HEIGHT = 1080.0;

  static constexpr float MARGIN = 10.0;

  // Currently, circles.
  struct Obj {
    uint32_t rgba = 0xFFFFFFFF;
    float x = 0.0f, y = 0.0f;
    float r = 3;
    float dx = 0.0, dy = 0.0f;
  };

  std::vector<Obj> objects;

  Scene() {
    ArcFour rc("scene");

    for (int i = 0; i < 100; i++) {
      float r = 2.0 + RandDouble(&rc) * 20.0;
      float rmargin = MARGIN + r;
      float w = WIDTH - 2.0 * rmargin;
      float h = HEIGHT - 2.0 * rmargin;

      uint32_t color =
        ColorUtil::HSVAToRGBA32(
            RandDouble(&rc), 0.5 + RandDouble(&rc) * 0.5,
            0.25 + RandDouble(&rc) * 0.5, 1.0);

      objects.emplace_back(Obj{
        .rgba = color,
        .x = (float)RandDouble(&rc) * w + rmargin,
        .y = (float)RandDouble(&rc) * h + rmargin,
        .r = r,
        .dx = (float)RandDouble(&rc) * 2.0f - 1.0f,
        .dy = (float)RandDouble(&rc) * 2.0f - 1.0f,
        });
    }
  }

  void Update() {
    for (Obj &obj : objects) {
      obj.x += obj.dx;
      obj.y += obj.dy;

      obj.dy += 0.1;

      if (obj.dx < 0.0 && obj.x - obj.r < MARGIN) {
        obj.x = MARGIN + obj.r;
        obj.dx = -obj.dx;
      } else if (obj.dx > 0.0 && obj.x + obj.r > WIDTH - MARGIN) {
        obj.x = WIDTH - MARGIN - obj.r;
        obj.dx = -obj.dx;
      } else if (obj.dy > 0.0 && obj.y + obj.r > HEIGHT - MARGIN) {
        obj.y = HEIGHT - MARGIN - obj.r;
        obj.dy = -obj.dy;
      }
    }
  }

  // Get the scene, using Cartesian coordinates.
  std::vector<Rendering::Triangle> GetScene() {
    std::vector<Rendering::Triangle> scene;
    scene.reserve(objects.size() * 3);

    for (const auto& obj : objects) {
      Rendering::vec2f v[5];
      for (int i = 0; i < 5; ++i) {
        float theta = i * 2.0f * std::numbers::pi / 5.0f;
        v[i] = {
          .x = obj.x + obj.r * std::cos(theta),
          .y = HEIGHT - obj.y + obj.r * std::sin(theta),
        };
      }
      // Fan triangulation from v[0]
      scene.push_back({v[0], v[1], v[2], obj.rgba, 0});
      scene.push_back({v[0], v[2], v[3], obj.rgba, 0});
      scene.push_back({v[0], v[3], v[4], obj.rgba, 0});
    }

    return scene;
  }

};

void Simulate() {

  Scene scene;

  std::unique_ptr<Inputs> inputs =
    Inputs::CreateSDL();
  std::unique_ptr<Rendering> rendering =
    Rendering::CreateSDLGL();
  CHECK(rendering.get() != nullptr);
  Print("Created rendering.\n");

  for (;;) {
    for (;;) {
      const Inputs::Input input = inputs->GetInput();
      if (std::holds_alternative<Inputs::None>(input))
        break;

      if (std::holds_alternative<Inputs::Exit>(input))
        return;

      if (const Inputs::KeyUp *kup = std::get_if<Inputs::KeyUp>(&input)) {
        if (kup->codepoint == 0x1b) {
          // Escape
          return;
        }

        Print("KeyUp: {}\n", UTF8::Encode(kup->codepoint));
        fflush(stdout);
      }
    }

    scene.Update();
    rendering->RenderScene(vec2f{0.0f, 0.0f},
                           vec2f{Scene::WIDTH, Scene::HEIGHT},
                           scene.GetScene());
  }

}


int main(int argc, char* argv[]) {
  Initialization::Initialize();

  Simulate();

  Initialization::Exit();
  return 0;
}
