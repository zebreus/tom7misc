
#include <algorithm>
#include <cstdio>
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
#include "circuit.h"
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

static constexpr uint32_t SELECTION_COLOR = 0x8888FF44;

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

  bool shift_held = false;
  bool is_dragging_sel = false;
  bool has_selection = false;
  vec2f sel_start = {0, 0};
  vec2f sel_end = {0, 0};

  int mouse_x = 0;
  int mouse_y = 0;
  bool is_tracking = false;
  uint64_t track_id = 0;

  Timer frame_timer;
  double frame_accumulator = 0.0;

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
        shift_held = (kdown->modifiers & Inputs::MOD_SHIFT) != 0;

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
        } else if (kdown->codepoint == 'f' || kdown->codepoint == 'F') {
          if (is_tracking) {
            is_tracking = false;
            status.Print("Stopped tracking item.\n");
          } else {
            vec2f pos = sim.ScreenToWorld(mouse_x, mouse_y);
            if (auto hit = sim.GetNodeAt(pos)) {
              if (hit->node->items.size() == 1) {
                is_tracking = true;
                track_id = hit->node->items[0].id;
                status.Print("Tracking item ID {}\n", track_id);
              } else {
                status.Print("Need exactly 1 item in cell to track.\n");
              }
            }
          }
          fflush(stdout);

        } else if (kdown->codepoint == 'i' || kdown->codepoint == 'I') {
          sim.InjectRandomAssignment();
        } else if (kdown->codepoint == 'c' || kdown->codepoint == 'C') {
          if (has_selection) {
            vec2f aabb_min = {std::min(sel_start.x, sel_end.x),
                              std::min(sel_start.y, sel_end.y)};
            vec2f aabb_max = {std::max(sel_start.x, sel_end.x),
                              std::max(sel_start.y, sel_end.y)};
            Layout extracted = sim.ExtractOverlapping(aabb_min, aabb_max);
            std::vector<uint8_t> serialized =
              LayoutEngine::Serialize(extracted);
            FILE *f = fopen("copied.layout", "wb");
            if (f != nullptr) {
              fwrite(serialized.data(), 1, serialized.size(), f);
              fclose(f);
              status.Print("Saved copied.layout\n");
            } else {
              status.Print("Failed to save copied.layout\n");
            }
          }
        }

      } else if (const Inputs::KeyUp *kup =
                 std::get_if<Inputs::KeyUp>(&input)) {
        shift_held = (kup->modifiers & Inputs::MOD_SHIFT) != 0;

        if (kup->codepoint == 0x1b) {
          // Escape
          return;
        }

        status.Print("KeyUp: {}\n", UTF8::Encode(kup->codepoint));

      } else if (const Inputs::MouseChange *mc =
                 std::get_if<Inputs::MouseChange>(&input)) {
        mouse_x = mc->x;
        mouse_y = mc->y;

        if (mc->button & Inputs::MOUSE_MIDDLE) {
          sim.Pan(mc->x, mc->y, mc->dx, mc->dy);
        }

        if (is_dragging_sel) {
          if (mc->button & Inputs::MOUSE_LEFT) {
            sel_end = sim.ScreenToWorld(mc->x, mc->y);
          } else {
            is_dragging_sel = false;
          }
        }

      } else if (const Inputs::MouseClick *mc =
                 std::get_if<Inputs::MouseClick>(&input)) {
        if (mc->button == Inputs::MOUSE_LEFT) {
          if (shift_held) {
            is_dragging_sel = true;
            has_selection = true;
            sel_start = sim.ScreenToWorld(mc->x, mc->y);
            sel_end = sel_start;
          } else {
            has_selection = false;
            vec2f pos = sim.ScreenToWorld(mc->x, mc->y);
            if (auto hit = sim.GetNodeAt(pos)) {
              status.Print("Layer {}, Column {}, Cell: {}\n",
                           hit->layer, hit->col, CellString(hit->node->cell));
              fflush(stdout);
            }
          }
        }

      } else if (const Inputs::MouseWheel *mw =
                 std::get_if<Inputs::MouseWheel>(&input)) {
        sim.Zoom(mw->x, mw->y, mw->up);
      }

    }

    constexpr bool FIX_YOUR_TIMESTEP = false;
    if (FIX_YOUR_TIMESTEP) {
      double frame_time = frame_timer.Seconds();
      frame_timer.Reset();
      frame_time = std::max(frame_time, 1.0 / 10.0);

      if (!paused) {
        frame_accumulator += frame_time;
        while (frame_accumulator >= Scene::DELTA_T) {
          sim.StepSimulation();
          frame_accumulator -= Scene::DELTA_T;
        }
      }
    } else {

      if (!paused) {
        sim.StepSimulation();
      }
    }

    if (is_tracking) {
      if (std::optional<vec2f> pos = sim.GetItemPosition(track_id)) {
        sim.CenterOn(*pos);
      } else {
        is_tracking = false;
        status.Print("Lost tracked item (destroyed or exited circuit).\n");
      }
    }

    std::vector<Rendering::Triangle> tri;
    sim.FillVisibleTriangles(&tri);

    if (has_selection) {
      vec2f a = {std::min(sel_start.x, sel_end.x),
                 std::min(sel_start.y, sel_end.y)};
      vec2f b = {std::max(sel_start.x, sel_end.x),
                 std::min(sel_start.y, sel_end.y)};
      vec2f c = {std::min(sel_start.x, sel_end.x),
                 std::max(sel_start.y, sel_end.y)};
      vec2f d = {std::max(sel_start.x, sel_end.x),
                 std::max(sel_start.y, sel_end.y)};
      tri.push_back({a, b, c, SELECTION_COLOR, 0});
      tri.push_back({c, b, d, SELECTION_COLOR, 0});
    }

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
