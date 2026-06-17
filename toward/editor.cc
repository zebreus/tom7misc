
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

void Simulate(std::string_view level_file) {
  std::unique_ptr<Level> level = Levels::LoadSVG(level_file);
  std::unique_ptr<Scene> scene = Levels::CreateScene(*level);

  std::unique_ptr<Inputs> inputs = Inputs::CreateSDL();
  std::unique_ptr<Rendering> rendering = Rendering::CreateSDLGL();

  bool paused = true;

  CHECK(rendering.get() != nullptr);
  Print("Created rendering.\n");

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
        }
      }

      if (const Inputs::KeyUp *kup = std::get_if<Inputs::KeyUp>(&input)) {
        if (kup->codepoint == 0x1b) {
          // Escape
          return;
        }

        Print("KeyUp: {}\n", UTF8::Encode(kup->codepoint));
        fflush(stdout);
      }
    }

    if (!paused) {
      scene->Update();
    }
    std::vector<Rendering::Triangle> tri = scene->GetTriangles();

    rendering->RenderScene(vec2f{0.0f, 0.0f},
                           vec2f{Scene::WIDTH, Scene::HEIGHT},
                           tri);
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
