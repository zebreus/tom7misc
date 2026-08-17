
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "SDL_main.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit-sim.h"
#include "initialization.h"
#include "inputs.h"
#include "layout.h"
#include "periodically.h"
#include "rendering.h"
#include "scene.h"
#include "sdl-rendering.h"
#include "status-bar.h"
#include "timer.h"
#include "toward-util.h"
#include "utf8.h"

static void Loop(std::string_view layout_file) {
  CellLibrary library;
  std::unique_ptr<Inputs> inputs = Inputs::CreateSDL();
  std::unique_ptr<Rendering> rendering = CreateSDLGLRendering();

  CircuitSim sim(library, rendering.get(), layout_file);
  sim.GoToTopLeftCell();

  Periodically status_per = Periodically(1);
  StatusBar status = StatusBar(1);

  Timer timer;
  int64_t frames = 0;
  bool paused = false;

  CHECK(rendering.get() != nullptr);
  Print("Created rendering.\n");

  [[maybe_unused]]
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
          sim.Reset();
          frames = 0;
          timer.Reset();

        } else if (kdown->codepoint == '1') {
          bit = true;
        } else if (kdown->codepoint == '0') {
          bit = false;
        } else if (kdown->codepoint == 'i' || kdown->codepoint == 'I') {
          sim.InjectRandomAssignment();
        }

      } else if (const Inputs::KeyUp *kup =
                 std::get_if<Inputs::KeyUp>(&input)) {
        if (kup->codepoint == 0x1b) {
          // Escape
          return;
        }

        status.Print("KeyUp: {}\n", UTF8::Encode(kup->codepoint));

      } else if (const Inputs::MouseChange *mc =
                 std::get_if<Inputs::MouseChange>(&input)) {

        if (mc->button & Inputs::MOUSE_MIDDLE) {
          sim.Pan(mc->x, mc->y, mc->dx, mc->dy);
        }

      } else if (const Inputs::MouseClick *mc =
                 std::get_if<Inputs::MouseClick>(&input)) {
        if (mc->button == Inputs::MOUSE_LEFT) {
          [[maybe_unused]]
          vec2f pos = sim.ScreenToWorld(mc->x, mc->y);

          /*
            TODO: Figure out which node we're clicking on

          LevelBody body = bit ? Levels::One() : Levels::Zero();
          body.pos = pos;
          body.color = 0xFF00FFFF;
          body.vel = vec2f(RandDouble(&rc) * 2 - 1, RandDouble(&rc) * 2 - 1);
          // between -2 and +2 radians per second.
          body.avel = RandDouble(&rc) * 4 - 2;

          level->bodies.push_back(body);
          Levels::AddBodyToScene(scene.get(), body);
          */
        }

      } else if (const Inputs::MouseWheel *mw =
                 std::get_if<Inputs::MouseWheel>(&input)) {
        sim.Zoom(mw->x, mw->y, mw->up);
      }

    }

    if (!paused) {
      sim.StepSimulation();
    }

    std::vector<Rendering::Triangle> tri;
    sim.FillVisibleTriangles(&tri);

    rendering->RenderScene(sim.ViewPos(), sim.ViewPosMax(), tri);
    frames++;

    status_per.RunIf([&]{
        double sec = timer.Seconds();
        status.Status("{} frames, {:.1f}fps, {} ticks, {} tris",
                      frames, frames / sec, sim.Ticks(), tri.size());
      });
  }
}

int main(int argc, char* argv[]) {
  ANSI::Init();

  std::string layout_file = "andvars.layout";
  if (argc >= 2) layout_file = argv[1];

  Initialization::Initialize();

  Loop(layout_file);

  Initialization::Exit();
  return 0;
}
