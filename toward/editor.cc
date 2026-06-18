
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "SDL_main.h"
#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "color-util.h"
#include "geom/bezier.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "initialization.h"
#include "inputs.h"
#include "letters.h"
#include "level.h"
#include "randutil.h"
#include "rendering.h"
#include "scene.h"
#include "svg.h"
#include "toward-util.h"
#include "utf8.h"
#include "util.h"
#include "yocto-math.h"

static constexpr vec2f VIEW_MIN = vec2f{0.0f, 0.0f};
static constexpr vec2f VIEW_MAX = vec2f{Scene::WIDTH, Scene::HEIGHT};

void Simulate(std::string_view level_file) {
  ArcFour rc("sim");

  std::unique_ptr<Level> level = Levels::LoadSVG(level_file);
  std::unique_ptr<Scene> scene = Levels::CreateScene(*level);

  std::unique_ptr<Inputs> inputs = Inputs::CreateSDL();
  std::unique_ptr<Rendering> rendering = Rendering::CreateSDLGL();

  bool paused = false;

  CHECK(rendering.get() != nullptr);
  Print("Created rendering.\n");

  bool bit = true;

  for (;;) {
    for (;;) {
      const Inputs::Input input = inputs->GetInput();
      if (std::holds_alternative<Inputs::None>(input))
        break;

      if (std::holds_alternative<Inputs::Exit>(input))
        return;

      if (const Inputs::KeyDown *kdown = std::get_if<Inputs::KeyDown>(&input)) {
        if (kdown->codepoint == '\r') {
          paused = !paused;
        } else if (kdown->codepoint == 'r' || kdown->codepoint == 'R') {
          level = Levels::LoadSVG(level_file);
          scene = Levels::CreateScene(*level);
        } else if (kdown->codepoint == '1') {
          bit = true;
        } else if (kdown->codepoint == '0') {
          bit = false;
        }

      } else if (const Inputs::KeyUp *kup =
                 std::get_if<Inputs::KeyUp>(&input)) {
        if (kup->codepoint == 0x1b) {
          // Escape
          return;
        }

        Print("KeyUp: {}\n", UTF8::Encode(kup->codepoint));
        fflush(stdout);
      } else if (const Inputs::MouseClick *mc =
                 std::get_if<Inputs::MouseClick>(&input)) {
        if (mc->button == Inputs::MOUSE_LEFT) {
          vec2f pos = rendering->CartesianPixel(
              VIEW_MIN, VIEW_MAX, mc->x, mc->y);
          // XXX ugh
          pos.y = VIEW_MAX.y - pos.y;

          LevelBody body = bit ? Levels::One() : Levels::Zero();
          body.pos = pos;
          body.color = 0xFF00FFFF;
          body.vel = vec2f(RandDouble(&rc) * 2 - 1, RandDouble(&rc) * 2 - 1);
          // between -1 and +1 radians per second.
          body.avel = RandDouble(&rc) * 2 - 1;

          level->bodies.push_back(body);
          Levels::AddBodyToScene(scene.get(), body);
        }
      }

    }

    if (!paused) {
      scene->Update();
    }
    std::vector<Rendering::Triangle> tri = scene->GetTriangles();

    rendering->RenderScene(VIEW_MIN, VIEW_MAX, tri);
  }

}


int main(int argc, char* argv[]) {
  ANSI::Init();

  std::string level_file = "example.svg";
  if (argc >= 2) level_file = argv[1];

  Initialization::Initialize();

  Simulate(level_file);

  Initialization::Exit();
  return 0;
}
