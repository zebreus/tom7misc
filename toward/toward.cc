
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
#include "letters.h"
#include "level.h"
#include "randutil.h"
#include "rendering.h"
#include "scene.h"
#include "sdl-rendering.h"
#include "toward-util.h"
#include "utf8.h"

enum GameScreen {
  INTRO,
  MAIN_MENU,
  PLAY_LEVEL,
};

static constexpr vec2f VIEW_MIN = vec2f{0.0f, 0.0f};
static constexpr vec2f VIEW_MAX = vec2f{Scene::WIDTH, Scene::HEIGHT};

struct Game {
  GameScreen game_screen = GameScreen::PLAY_LEVEL;

  // Abstraction around controller.
  std::unique_ptr<Inputs> inputs;
  // Abstraction around video.
  std::unique_ptr<Rendering> rendering;

  // The scene is the simulation state. We use id numbers to keep
  // track of objects with special meaning.
  std::unique_ptr<Scene> scene;

  std::unique_ptr<Letters> letters;

  ArcFour rc;
  Game() : rc("toward") {
    inputs = Inputs::CreateSDL();
    rendering = CreateSDLGLRendering();
    // Start with empty scene.
    scene = Scene::Create();
    CHECK(rendering.get() != nullptr);

    letters = Letters::LoadFont("helveticab.ttf", false);
  }

  void Loop();

  std::string current_level;
  void StartLevel(std::string_view level_file) {
    current_level = std::string(level_file);
    Reset();
  }

  void Reset() {
    Levels::Options opt;
    opt.include_text = true;
    std::unique_ptr<Level> level = Levels::LoadSVGExt(opt, current_level);
    Print("Loaded {} with {} bodies.\n", current_level,
          level->bodies.size());
    scene = Levels::CreateScene(*level);
    CHECK(scene.get() != nullptr);
  }

};

// Timing strategy:
// We want the game physics to be independent of the frame rate, but
//

void Game::Loop() {

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
          vec2f pos = rendering->ScreenToWorld(
              VIEW_MIN, VIEW_MAX, mc->x, mc->y);

          LevelBody body = bit ? Levels::One() : Levels::Zero();
          body.pos = pos;
          body.color = 0xFF00FFFF;
          body.vel = vec2f(RandDouble(&rc) * 2 - 1, RandDouble(&rc) * 2 - 1);
          // between -2 and +2 radians per second.
          body.avel = RandDouble(&rc) * 4 - 2;

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

  std::string level_file = "level1.svg";
  if (argc >= 2) level_file = argv[1];

  Initialization::Initialize();

  Game game;
  game.StartLevel(level_file);
  game.Loop();

  Initialization::Exit();
  return 0;
}
