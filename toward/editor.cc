
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "SDL_main.h"
#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "image.h"
#include "initialization.h"
#include "inputs.h"
#include "level.h"
#include "randutil.h"
#include "rendering.h"
#include "scene.h"
#include "sdl-rendering.h"
#include "toward-util.h"
#include "utf8.h"

static constexpr vec2f VIEW_MIN = vec2f{0.0f, 0.0f};
static constexpr vec2f VIEW_MAX = vec2f{Scene::WIDTH, Scene::HEIGHT};

void Simulate(std::string_view level_file) {
  ArcFour rc("sim");

  std::unique_ptr<Inputs> inputs = Inputs::CreateSDL();
  std::unique_ptr<Rendering> rendering = CreateSDLGLRendering();
  CHECK(rendering.get() != nullptr);
  Print("Created rendering.\n");

  /*
  std::unique_ptr<ImageRGBA> bg(ImageRGBA::Load("backgroundtest.png"));
  CHECK(bg.get() != nullptr);
  rendering->SetBackground(*bg);
  */

  std::unique_ptr<Level> level;
  std::unique_ptr<Scene> scene;


  auto Reset = [&level, &scene, level_file]() {
      bool is_cell = level_file.find("cell-") != std::string_view::npos;

      Levels::Options opt;
      opt.include_text = true;
      if (is_cell)
        opt.include_text = false;

      level = Levels::LoadSVGExt(opt, level_file);
      Print("There are {} bodies in the level.\n", level->bodies.size());
      if (is_cell) {
        Levels::AddChutes(level.get(), 0x00FF00FF, 0xFF0000FF);
      }
      scene = Levels::CreateScene(*level);
    };

  Reset();

  bool paused = false;

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
          Reset();
        } else if (kdown->codepoint == '1') {
          bit = true;
        } else if (kdown->codepoint == '0') {
          bit = false;
        } else if (kdown->codepoint == 'h') {
          if (scene->Hibernating()) {
            Print("Unhibernate.\n");
            scene->Unhibernate();
          } else {
            Print("Hibernate.\n");
            scene->Hibernate();
          }
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
          // between -2 and +2 radians per second.
          body.avel = RandDouble(&rc) * 4 - 2;

          level->bodies.push_back(body);
          Levels::AddBodyToScene(scene.get(), body);
        }

      } else if (const Inputs::MouseWheel *mw =
                 std::get_if<Inputs::MouseWheel>(&input)) {
        Print("Wheel {}\n", mw->up ? "up" : "dn");
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

  std::string level_file = "talk/example.svg";
  if (argc >= 2) level_file = argv[1];

  Initialization::Initialize();

  Simulate(level_file);

  Initialization::Exit();
  return 0;
}
