
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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
#include "util.h"
#include "pi-rendering.h"

static constexpr vec2f VIEW_MIN = vec2f{0.0f, 0.0f};
static constexpr vec2f VIEW_MAX = vec2f{Scene::WIDTH, Scene::HEIGHT};

enum class SlideResult {
  RESET,
  NEXT,
  PREV,
  EXIT,
};

// Shared by slide handlers.
static std::optional<SlideResult> DefaultSlideResult(
    const Inputs::Input &input) {
  if (std::holds_alternative<Inputs::None>(input))
    return std::nullopt;

  if (std::holds_alternative<Inputs::Exit>(input))
    return {SlideResult::EXIT};

  if (const Inputs::KeyDown *kdown = std::get_if<Inputs::KeyDown>(&input)) {
    if (kdown->codepoint == 'r' || kdown->codepoint == 'R') {
      return {SlideResult::RESET};

    } else if (kdown->codepoint == Inputs::CP_LEFT) {
      return {SlideResult::PREV};

    } else if (kdown->codepoint == Inputs::CP_RIGHT) {
      return {SlideResult::NEXT};

    } else if (kdown->codepoint == 0x1b) {
      // Escape
      return {SlideResult::EXIT};
    }
  }

  return std::nullopt;
}

SlideResult SimulateLevel(ArcFour *rc,
                          Inputs *inputs, Rendering *rendering,
                          const Level *level_in) {

  Level level = *level_in;

  CHECK(inputs != nullptr);
  CHECK(rendering != nullptr);

  std::unique_ptr<Scene> scene = Levels::CreateScene(level);

  bool paused = false;
  bool bit = true;

  for (;;) {
    for (;;) {
      const Inputs::Input input = inputs->GetInput();
      if (std::optional<SlideResult> ro = DefaultSlideResult(input)) {
        return ro.value();
      }

      if (std::holds_alternative<Inputs::None>(input))
        break;


      if (const Inputs::KeyDown *kdown = std::get_if<Inputs::KeyDown>(&input)) {
        if (kdown->codepoint == '\r' || kdown->codepoint == ' ') {
          paused = !paused;
        } else if (kdown->codepoint == '1') {
          bit = true;
        } else if (kdown->codepoint == '0') {
          bit = false;
        } else {
          Print("KeyDown: {:08x} '{}'\n", kdown->codepoint,
                UTF8::Encode(kdown->codepoint));
          fflush(stdout);
        }

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
          body.vel = vec2f(RandDouble(rc) * 2 - 1, RandDouble(rc) * 2 - 1);
          // between -2 and +2 radians per second.
          body.avel = RandDouble(rc) * 4 - 2;

          level.bodies.push_back(body);
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

SlideResult ShowImage(ArcFour *rc,
                      Inputs *inputs, Rendering *rendering,
                      const ImageRGBA &image) {
  CHECK(inputs != nullptr);
  CHECK(rendering != nullptr);

  rendering->SetBackground(image);

  rendering->RenderScene({0.0, 0.0}, {1920.0, 1080.0}, {});

  for (;;) {
    const Inputs::Input input = inputs->GetInput();
    if (std::optional<SlideResult> ro = DefaultSlideResult(input)) {
      return ro.value();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}


// A single full-screen level.
struct LevelContent {
  std::string file;
  std::unique_ptr<Level> level;
};

// A static full-screen image.
struct ImageContent {
  std::string file;
  ImageRGBA image;
};

using Content = std::variant<LevelContent, ImageContent>;

struct Props {
  // Default physical properties.
  float item_cor = LevelBody().restitution;
  float item_cof = LevelBody().restitution;
};

struct Slide {
  Props props = {};
  Content content;
};

struct Slideshow {
  int current_slide = 0;
  std::vector<Slide> slides;
  ArcFour rc;

  std::unique_ptr<Inputs> inputs;
  std::unique_ptr<Rendering> rendering;

  Slideshow(std::string_view slidefile) : rc("slides") {
    inputs = Inputs::CreateSDL();
    #ifdef RASPBERRY
    rendering = CreatePiRendering();
    #else
    rendering = CreateSDLGLRendering();
    #endif

    std::vector<std::string> lines = Util::ReadFileToLines(slidefile);
    CHECK(!lines.empty()) << slidefile;

    // Apply props to slides until changed.
    Props props;

    for (std::string_view line : lines) {
      Util::RemoveOuterWhitespace(&line);
      if (line.empty()) continue;
      if (Util::StartsWith(line, "#")) continue;

      if (line == "reset") {
        props = Props();

      } else if (Util::TryStripPrefix("level ", &line)) {
        Util::RemoveLeadingWhitespace(&line);
        Levels::Options opt;
        opt.include_text = true;

        std::unique_ptr<Level> level = Levels::LoadSVGExt(opt, line, false);
        slides.emplace_back(
            Slide{
              .props = props,
              .content = {LevelContent{
                  .file = std::string(line),
                  .level = std::move(level),
                }}
            });

      } else if (Util::TryStripPrefix("cell ", &line)) {
        Util::RemoveLeadingWhitespace(&line);
        Levels::Options opt;
        opt.include_text = false;

        std::unique_ptr<Level> level = Levels::LoadSVGExt(opt, line, false);
        Levels::AddChutes(level.get(), 0x00FF00FF, 0xFF0000FF);

        slides.emplace_back(
            Slide{
              .props = props,
              .content = {LevelContent{
                  .file = std::string(line),
                  .level = std::move(level),
                }}
            });

      } else if (Util::TryStripPrefix("image ", &line)) {
        Util::RemoveLeadingWhitespace(&line);
        std::unique_ptr<ImageRGBA> image(ImageRGBA::Load(line));
        CHECK(image.get()) << line;
        // TODO: Center it?
        slides.emplace_back(
            Slide{
              .props = props,
              .content = {ImageContent{
                  .file = std::string(line),
                  .image = std::move(*image),
                }}
            });

      } else {
        LOG(FATAL) << "Bad command: " << line;
      }
    }

  }

  void Run() {

    for (;;) {
      const Slide &slide = slides[current_slide];

      SlideResult sr = SlideResult::EXIT;

      // The content runs until there's an event that should be handled by
      // the slideshow loop (like "go to the next slide").
      if (const LevelContent *lc = std::get_if<LevelContent>(&slide.content)) {
        sr = SimulateLevel(&rc,
                           inputs.get(), rendering.get(), lc->level.get());

      } else if (const ImageContent *ic =
                 std::get_if<ImageContent>(&slide.content)) {
        sr = ShowImage(&rc, inputs.get(), rendering.get(), ic->image);

      } else {
        LOG(FATAL) << "Bad variant?";
      }

      // TODO: Cleaner way to do this?
      rendering->ClearBackground();

      switch (sr) {
      case SlideResult::EXIT:
        return;

      case SlideResult::RESET:
        // Just re-enter the loop.
        break;

      case SlideResult::NEXT:
        current_slide++;
        if (current_slide >= slides.size()) current_slide = 0;
        break;

      case SlideResult::PREV:
        current_slide--;
        if (current_slide < 0) current_slide = slides.size() - 1;
        break;

      default:
        // ?
        break;
      }

    }


  }

};

int main(int argc, char* argv[]) {
  ANSI::Init();

  std::string slides_file = "talk/slides.txt";
  if (argc >= 2) slides_file = argv[1];

  Initialization::Initialize();

  Slideshow show(slides_file);
  show.Run();

  Initialization::Exit();
  return 0;
}
