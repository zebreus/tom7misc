
#include <algorithm>
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
#include "timer.h"
#include "toward-util.h"
#include "utf8.h"

enum GameScreen {
  INTRO,
  MAIN_MENU,
  PLAY_LEVEL,
};

static constexpr vec2f VIEW_MIN = vec2f{0.0f, 0.0f};
static constexpr vec2f VIEW_MAX = vec2f{Scene::WIDTH, Scene::HEIGHT};

// Drawing routines, with world coordinates.
static void DrawRect(std::vector<Rendering::Triangle> *tris,
                     float x0, float y0,
                     float w, float h,
                     uint32_t color) {
  const float x1 = x0 + w;
  const float y1 = y0 + h;

  tris->push_back(Rendering::Triangle{
    .a = vec2f{x0, y0},
    .b = vec2f{x1, y0},
    .c = vec2f{x1, y1},
    .rgba = color,
    .reserved = 0,
  });
  tris->push_back(Rendering::Triangle{
    .a = vec2f{x0, y0},
    .b = vec2f{x1, y1},
    .c = vec2f{x0, y1},
    .rgba = color,
    .reserved = 0,
  });
}

// Draws an inner-stroked box with the given stroke weight.
// Sharp corners. Takes care not to draw the corners twice, since
// the color may have alpha, but the walls could self-overlap.
static void DrawBox(std::vector<Rendering::Triangle> *tris,
                    float x0, float y0,
                    float w, float h,
                    // thickness of box lines
                    float stroke,
                    uint32_t color) {
  // Top and bottom
  DrawRect(tris, x0, y0, w, stroke, color);
  DrawRect(tris, x0, y0 + h - stroke, w, stroke, color);
  // Left and right (excluding corners)
  DrawRect(tris, x0, y0 + stroke, stroke, h - 2.0f * stroke, color);
  DrawRect(tris, x0 + w - stroke, y0 + stroke, stroke, h - 2.0f * stroke,
           color);
}

static void DrawLetter(std::vector<Rendering::Triangle> *tris,
                       const Letter *letter,
                       // nominal top-left corner of letter (not baseline)
                       float x0, float y0,
                       // nominal height in world units
                       float scale,
                       uint32_t color) {
  CHECK(letter != nullptr);

  // The mesh consists of convex polygons. We can triangulate each
  // polygon using a triangle fan starting from the first vertex.
  for (const auto &poly : letter->mesh.polygons) {
    if (poly.size() < 3) continue;

    const auto &v0 = letter->mesh.vertices[poly[0]];
    const vec2f p0 = {x0 + static_cast<float>(v0.x) * scale,
                      y0 + static_cast<float>(v0.y) * scale};

    for (size_t i = 1; i + 1 < poly.size(); i++) {
      const auto &v1 = letter->mesh.vertices[poly[i]];
      const auto &v2 = letter->mesh.vertices[poly[i + 1]];
      const vec2f p1 = {x0 + static_cast<float>(v1.x) * scale,
                        y0 + static_cast<float>(v1.y) * scale};
      const vec2f p2 = {x0 + static_cast<float>(v2.x) * scale,
                        y0 + static_cast<float>(v2.y) * scale};

      tris->push_back(Rendering::Triangle{
        .a = p0,
        .b = p1,
        .c = p2,
        .rgba = color,
        .reserved = 0,
      });
    }
  }
}

struct OSD {

  const Letter *next_letter = nullptr;

  // Nominal pixel.
  static constexpr float PX = Scene::HEIGHT / 1080.0f;
  static constexpr float NEXT_MARGIN = Scene::WIDTH * 0.05;
  static constexpr float NEXT_SIZE = Scene::WIDTH * 0.1;
  static constexpr float NEXT_X = Scene::WIDTH - NEXT_SIZE - NEXT_MARGIN;
  static constexpr float NEXT_Y = NEXT_MARGIN;
  static constexpr float NEXT_STROKE = 4 * PX;

  void AddTriangles(std::vector<Rendering::Triangle> *tris) {
    if (next_letter) {
      DrawBox(tris, NEXT_X, NEXT_Y, NEXT_SIZE, NEXT_SIZE,
              NEXT_STROKE, 0x00003388);

      const float LETTER_MARGIN = 0.05 * (NEXT_SIZE - NEXT_STROKE * 2.0);
      const float LETTER_SIZE = 0.95 * (NEXT_SIZE - NEXT_STROKE * 2.0);
      const float LETTER_X = NEXT_X + NEXT_STROKE + LETTER_MARGIN;
      const float LETTER_Y = NEXT_Y + NEXT_STROKE + LETTER_MARGIN;
      DrawLetter(tris, next_letter,
                 LETTER_X, LETTER_Y, LETTER_SIZE,
                 0xFFFFFFCC);
    }
  }
};

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

  std::unique_ptr<OSD> osd;

  ArcFour rc;
  Game() : rc("toward") {
    inputs = Inputs::CreateSDL();
    rendering = CreateSDLGLRendering();
    // Start with empty scene.
    scene = Scene::Create();
    CHECK(rendering.get() != nullptr);

    letters = Letters::LoadFont("helveticab.ttf", false);

    osd = std::make_unique<OSD>();
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

void Game::Loop() {

  bool paused = false;
  bool bit = true;

  Timer timer;
  double accumulator = 0.0;

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
        } else {
          if (kdown->codepoint >= 'a' && kdown->codepoint <= 'z') {
            auto it = letters->letter.find(kdown->codepoint);
            if (it != letters->letter.end()) {
              osd->next_letter = &it->second;
            }
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

    double frame_time = timer.Seconds();
    timer.Reset();
    // Throttle game speed if we aren't keeping up.
    frame_time = std::min(frame_time, 1.0 / 10.0);

    if (!paused) {
      // Native timing is 120 Hz. We run multiple ticks
      // of the simulation if video is running slower
      // than that.
      accumulator += frame_time;
      while (accumulator >= Scene::DELTA_T) {
        scene->Update();
        accumulator -= Scene::DELTA_T;
      }
    }

    std::vector<Rendering::Triangle> tri = scene->GetTriangles();
    osd->AddTriangles(&tri);

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
