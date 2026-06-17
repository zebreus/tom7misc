
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
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "initialization.h"
#include "inputs.h"
#include "letters.h"
#include "randutil.h"
#include "rendering.h"
#include "scene.h"
#include "utf8.h"
#include "yocto-math.h"

struct Game {
  Game(std::string_view fontfile) : rc("game") {
    letters = Letters::LoadFont(fontfile);
    CHECK(letters.get() != nullptr);
    scene = std::make_unique<Scene>();
  }

  ArcFour rc;

  static constexpr float LETTER_SCALE = 2.0f;

  uint32_t prev_codepoint = 0;
  float letter_x = Scene::MARGIN + 0.01 * Scene::ARENA_WIDTH;
  float letter_y =
    Scene::HEIGHT - Scene::MARGIN - 0.01 * Scene::ARENA_HEIGHT -
    LETTER_SCALE;

  void ApplyRandomImpluse() {
    float max_mag = 6.0f;
    float dx = ((float)RandDouble(&rc) * 2.0f - 1.0f) * max_mag;
    float dy = ((float)RandDouble(&rc) - 1.0f) * max_mag;
    scene->ApplyImpulse({dx, dy});
  }

  void AddLetter(uint32_t codepoint) {
    auto it = letters->letter.find(codepoint);
    if (it == letters->letter.end()) return;

    const Letter &letter = it->second;
    if (letter.mesh.polygons.empty()) return;

    Polygonization::Mesh scaled_mesh;
    scaled_mesh.vertices.reserve(letter.mesh.vertices.size());
    for (const vec2 &v : letter.mesh.vertices) {
      scaled_mesh.vertices.push_back(LETTER_SCALE * v);
    }
    scaled_mesh.polygons = letter.mesh.polygons;

    uint32_t color = ColorUtil::HSVAToRGBA32(
        RandDouble(&rc), 0.5 + RandDouble(&rc) * 0.5,
        0.25 + RandDouble(&rc) * 0.5, 0.8);

    letter_x += letters->GetKerning(prev_codepoint, codepoint) * LETTER_SCALE;
    if (letter_x + LETTER_SCALE > Scene::WIDTH - Scene::MARGIN) {
      letter_x = Scene::MARGIN + 0.01 * Scene::ARENA_WIDTH;
      prev_codepoint = 0;
    } else {
      prev_codepoint = codepoint;
    }

    // First reject from the previous character ("typing").
    std::optional<vec2f> ro_right =
      scene->RejectObject(scaled_mesh, vec2f{letter_x, letter_y},
                          vec2f{1.0f, 0.0f});

    if (!ro_right.has_value()) {
      Print("Couldn't place (1)!\n");
      return;
    } else {
      Print("Now try place at {},{}\n", ro_right.value().x,
            Scene::HEIGHT - Scene::MARGIN);
    }

    // Now place on the ground.
    if (std::optional<vec2f> ro_up =
        scene->RejectObject(scaled_mesh,
                            vec2f{
                              .x = ro_right.value().x,
                              // At the bottom.
                              .y = Scene::HEIGHT - Scene::MARGIN,
                            },
                            vec2f{0.0f, -1.0f})) {
      const auto &[x, y] = ro_up.value();
      letter_x = x;
      constexpr float RESTITUTION = 0.05;
      scene->AddObject(scaled_mesh, color, vec2f{x, y},
                       vec2f{0.0, 0.0}, RESTITUTION);
    } else {
      Print("Couldn't place (2)!\n");
      return;
    }
  }

  std::unique_ptr<Letters> letters;
  std::unique_ptr<Scene> scene;
};

void Simulate(std::string_view fontfile) {
  Game game(fontfile);
  bool paused = false;

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

      if (const Inputs::KeyDown *kdown = std::get_if<Inputs::KeyDown>(&input)) {
        if (kdown->codepoint == '\r') {
          paused = !paused;
        }
      }

      if (const Inputs::KeyUp *kup = std::get_if<Inputs::KeyUp>(&input)) {
        if (kup->codepoint == 0x1b) {
          // Escape
          return;
        }

        Print("KeyUp: {}\n", UTF8::Encode(kup->codepoint));
        fflush(stdout);

        if (kup->codepoint >= 32 && kup->codepoint < 127) {
          game.AddLetter(kup->codepoint);
        }
      }
    }

    if (!paused) {
      game.scene->Update();
    }
    rendering->RenderScene(vec2f{0.0f, 0.0f},
                           vec2f{Scene::WIDTH, Scene::HEIGHT},
                           game.scene->GetTriangles());
  }

}


int main(int argc, char* argv[]) {
  ANSI::Init();

  std::string fontfile = "helveticab.ttf";
  if (argc >= 2) fontfile = argv[1];

  Initialization::Initialize();

  Simulate(fontfile);

  Initialization::Exit();
  return 0;
}
