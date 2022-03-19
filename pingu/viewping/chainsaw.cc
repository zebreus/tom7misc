
#include <cstdint>
#include <string>
#include <mutex>
#include <cmath>
#include <optional>
#include <utility>
#include <tuple>

#include "SDL.h"

#include "sdl/sdlutil.h"
#include "sdl/font.h"
#include "sdl/chars.h"
#include "sdl/cursor.h"

#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"
#include "threadutil.h"
#include "timer.h"
#include "periodically.h"
#include "opt/optimizer.h"

using namespace std;
using int64 = int64_t;
using uint32 = uint32_t;
using uint8 = uint8_t;

static constexpr int SCREENW = 1920;
static constexpr int SCREENH = 1080;

std::mutex screen_mutex;
static SDL_Surface *screen = nullptr;
static Font *font = nullptr;
static SDL_Cursor *cursor_arrow = nullptr, *cursor_bucket = nullptr;
static SDL_Cursor *cursor_hand = nullptr, *cursor_hand_closed = nullptr;
static SDL_Cursor *cursor_eraser = nullptr;

static constexpr double EARTH_RADIUS_M = 6378100.0;
static constexpr double GRAVITATIONAL_CONSTANT = 6.67430e-11;
static constexpr double EARTH_MASS_KG = 5.97219e24;
// In km/sec. We could rederive this from first principles?
static constexpr double EARTH_ESCAPE_SPEED = 11186000.0;

static constexpr double START_X = 0.0;
static constexpr double START_Y = EARTH_RADIUS_M + 1.5;


struct Object {
  double mass_kg = 5.0;
  double dx_mps = 100.0;
  double dy_mps = 0.0;
  double x_m = START_X;
  double y_m = START_Y;
  uint32 color = 0xFFFFFFFF;
};

struct Sim {
  int64 steps = 0;
  vector<Object> objects;
  double min_dstart = 10000000.0;

  double max_dstart_squared = 0.0;
  
  void UpdatePhysics(double s, bool verbose = false) {
    steps++;
    // const double s = 1.0; //  1 / 10.0; // = 1.0 / 60.0;
    for (Object &obj : objects) {
      double rsquared = (obj.x_m * obj.x_m) + (obj.y_m * obj.y_m);
      double r = sqrt(rsquared);
      
      /*
      // in kg * m / s^2
      double f = GRAVITATIONAL_CONSTANT * (EARTH_MASS_KG * obj.mass_kg) /
        rsquared;
      double f_on_obj = (f / obj.mass_kg) * s;
      */

      // but object's mass cancels out (avoid precision loss).
      // Near the surface this is the famous 9.8 m/s^2.
      /*
      double f_on_obj =
        (GRAVITATIONAL_CONSTANT * EARTH_MASS_KG / rsquared) * s;

      // apply force along the vector
      double r = sqrt(rsquared);
      double normx = obj.x_m / r;
      double normy = obj.y_m / r;
      double gx_mps = -normx * f_on_obj;
      double gy_mps = -normy * f_on_obj;
      */

      // and then again, since we divide f_on_obj by r, we can
      // factor this out to the equivalent
      // fobj = (GRAVITATIONAL_CONSTANT * EARTH_MASS_KG * s / r * r);
      double gx_mps = -GRAVITATIONAL_CONSTANT * EARTH_MASS_KG * s * obj.x_m / (r * rsquared);
      double gy_mps = -GRAVITATIONAL_CONSTANT * EARTH_MASS_KG * s * obj.y_m / (r * rsquared);      

      obj.x_m += obj.dx_mps * s;
      obj.y_m += obj.dy_mps * s;

      obj.dx_mps += gx_mps;
      obj.dy_mps += gy_mps;

      // XXX
      {
        // Check if it has returned (but not instantly)
        double dx = (obj.x_m - START_X);
        double dy = (obj.y_m - START_Y);
        double dstart_squared = (dx * dx) + (dy * dy);
        if (dstart_squared < max_dstart_squared) {
        } else {
          max_dstart_squared = dstart_squared;
        }
      }
      
      if (verbose) {
        double dx = (obj.x_m - START_X);
        double dy = (obj.y_m - START_Y);
        double dstart = sqrt((dx * dx) + (dy * dy));
        if (steps > 10000) {
          min_dstart = std::min(dstart, min_dstart);
        }
        printf("%lld. obj %.3f %.3f  d %.3f %.3f  to start: %.4f (%.4f - %.4f)\n",
               steps, 
               obj.x_m, obj.y_m, obj.dx_mps, obj.dy_mps,
               dstart, min_dstart, sqrt(max_dstart_squared));
      }
    }
  }
  
};

// Optimize the release vector to give the longest period, but
// within the constraints of:
//  - the orbit has to be elliptical (not escaping)
//  - the path should not intersect earth
using OrbitOptimizer = Optimizer<0, 2, double>;

// Returns negative time; positive return values represent infeasible
// inputs.
static OrbitOptimizer::return_type OptimizeMe(
    OrbitOptimizer::arg_type arg) {
  auto [ints_, doubles] = arg;
  auto [dx, dy] = doubles;

  // Anything positive is considered infeasible.
  static constexpr double LARGE_TIME = 1000.0;

  // First, if this is an escape speed, the solution is
  // infeasible.
  const double speed = sqrt((dx * dx) + (dy * dy));
  if (speed >= EARTH_ESCAPE_SPEED)
    return make_pair(LARGE_TIME + (speed - EARTH_ESCAPE_SPEED),
                     nullopt);

  // Then, simulate the orbit.
  Sim sim;
  {
    Object obj;
    obj.dx_mps = dx;
    obj.dy_mps = dy;
    obj.x_m = START_X;
    obj.y_m = START_Y;
    sim.objects.push_back(obj);
  }
    
  // this should be larger than the typical distance traveled
  // in a time step.
  [[maybe_unused]]
  constexpr double DIST_RETURN_TO_START = 2000.0;

  constexpr int STEPS_PER_SECOND = 10;
      
  constexpr int MAX_ITERS = 3600 * 24 * 7 * STEPS_PER_SECOND;
  [[maybe_unused]]
  bool was_outside = false;
  double max_dstart_squared = 0.0;
  for (int i = 0; i < MAX_ITERS; i++) {
    const Object &obj = sim.objects[0];
    // Always check if it has intersected earth.
    double rsquared = (obj.x_m * obj.x_m) + (obj.y_m * obj.y_m);
    if (rsquared < (EARTH_RADIUS_M * EARTH_RADIUS_M)) {
      // infeasible, but better if we go longer before colliding
      CHECK(i < 2 * MAX_ITERS);
      return make_pair(LARGE_TIME + 2 * MAX_ITERS - i, nullopt);
    }
    
    // Check if it has returned (but not instantly)
    double dx = (obj.x_m - START_X);
    double dy = (obj.y_m - START_Y);
    double dstart_squared = (dx * dx) + (dy * dy);
    if (dstart_squared < max_dstart_squared) {
      return make_pair(-2.0 * i, make_optional(sqrt(dstart_squared)));
    } else {
      max_dstart_squared = dstart_squared;
    }

#if 0
    if (dstart_squared <= (DIST_RETURN_TO_START * DIST_RETURN_TO_START)) {
      if (was_outside) {
        return make_pair(-1.0 * i, make_optional((double)i));
      }
    } else {
      was_outside = true;
    }
#endif

    sim.UpdatePhysics(1.0 / STEPS_PER_SECOND, false);
  }

  // Too many iters. This should probably not happen, as we
  // exclude escape orbits, but the simulation is also not exact.
  // Not sure what we can return here?
  return make_pair(9999999999999.0, nullopt); // OrbitOptimizer::INFEASIBLE;
}

static void Solve() {
  OrbitOptimizer opt{OptimizeMe};

  opt.SetSaveAll(true);

  opt.Sample({{}, {0.0, 10.0}});
  opt.Sample({{}, {0.0, 4000.0}});
  opt.Sample({{}, {10000.0, 0.0}});
  opt.Sample({{}, {9744.547698, 5329.883005}});
  
  opt.Run({},
          {make_pair(-10000.0, 10000.0),
           make_pair(-10000.0, 10000.0)},
          nullopt, nullopt,
          // max seconds
          {60.0});

  auto all = opt.GetAll();
  printf("Ran %d tests.\n", (int)all.size());
  int64 feasible = 0;
  for (const auto &[arg, score, out] : all) {
    if (out.has_value()) feasible++;
  }
  printf("%d were feasible.\n", feasible);
    
  auto bo = opt.GetBest();
  if (bo.has_value()) {
    auto [arg, score, out] = *bo;
    auto [dx, dy] = arg.second;
    printf("Best dir: %.6f, %.6f in time %.6f\n", dx, dy, -score);
  }

  string ret;
  for (const auto &[arg, score, out] : all) {
    const auto [dx, dy] = arg.second;
    StringAppendF(&ret,
                  "{%.6f, %.6f}, // time %.6f%s\n", dx, dy,
                  -score,
                  out.has_value() ? "" : " INFEASIBLE");
  }
  Util::WriteFile("results.txt", ret);
}

struct UI {
  UI() : physics_per(1.0), drawinfo_per(5.0) {
    Reset();
  }

  void Reset() {
    sim.objects.clear();

    #if 0
    {
      /* orbit! */
      Object saw;
      saw.mass_kg = 5.0;
      // supposedly earth's escape velocity is 11.2km, so
      // this is in the right ballpark...
      saw.dx_mps = 10000.0;
      saw.dy_mps = 0.0;
      saw.x_m = 0.0;
      saw.y_m = EARTH_RADIUS_M + 1.5;
      sim.objects.push_back(saw);
    }
    {
      Object saw;
      saw.mass_kg = 5.0;
      saw.dx_mps = 9800.0;
      saw.dy_mps = 200.0;
      saw.x_m = 0.0;
      saw.y_m = EARTH_RADIUS_M + 1.5;
      sim.objects.push_back(saw);
    }
    #endif

    // Basically, only the speed and initial position matter here;
    // differences in the angle just change the angle of the ellipse
    // (and some of them intersect earth and are invalid). So really
    // we just want to throw it tangent to Earth.
    Object saw1;
    saw1.mass_kg = 5.0;
    saw1.dx_mps = 9744.547698;
    saw1.dy_mps = 5329.883005;
    saw1.x_m = START_X;
    saw1.y_m = START_Y;
    saw1.color = SDL_MapRGBA(screen->format, 0x33, 0xFF, 0xFF, 0xFF);
    sim.objects.push_back(saw1);

    Object saw2;
    saw2.dx_mps = sqrt((saw1.dx_mps * saw1.dx_mps) +
                       (saw1.dy_mps * saw1.dy_mps));
    saw2.dy_mps = 0;
    saw2.x_m = START_X;
    saw2.y_m = START_Y;
    saw2.color = SDL_MapRGBA(screen->format, 0xFF, 0x33, 0x33, 0xFF);
    sim.objects.push_back(saw2);

    Object saw3;
    saw3.dx_mps = 11000.0;
    saw3.dy_mps = 0;
    saw3.x_m = START_X;
    saw3.y_m = START_Y;
    saw3.color = SDL_MapRGBA(screen->format, 0xFF, 0xFF, 0x33, 0xFF);
    sim.objects.push_back(saw3);

    printf("Saw2: %.6f\n", saw2.dx_mps);
  }
  

  Periodically physics_per;
  
  double scrollx = 0, scrolly = 0;

  Sim sim;
  std::vector<std::tuple<double, double, uint32>> points;
  
  void SetScroll(double sx, double sy) {
    scrollx = sx;
    scrolly = sy;
  }
  
  // zoom is in meters per pixel.
  // zoom such that Earth fills a quarter of the screen (vertically)
  double zoom_mpp = EARTH_RADIUS_M / ((double)SCREENH / 8.0);
  
  int mousex = 0, mousey = 0;
  
  // if dragging, the original scrollx,scrolly and start mouse
  // position.
  std::optional<std::tuple<double, double, int, int>> drag_origin;
  bool ui_dirty = true;

  void DrawSim() {
    // map 
    auto MapXY = [this](double x, double y) -> std::pair<double, double> {
        return std::make_pair(SCREENW / 2.0 + scrollx + x / zoom_mpp,
                              SCREENH / 2.0 + scrolly + y / zoom_mpp);
    };

    {
      double earth_radius_p = EARTH_RADIUS_M / zoom_mpp;
      auto [ex, ey] = MapXY(0.0, 0.0);
      sdlutil::DrawCircle32(screen, ex, ey,
                            earth_radius_p, 0xFF33AA33);
    }

    for (const Object &obj : sim.objects) {
      auto [ox, oy] = MapXY(obj.x_m, obj.y_m);
      // printf("obj at %.2f %.2f\n", ox, oy);
      sdlutil::DrawCircle32(screen, ox, oy,
                            3, obj.color);
    }

    {
      auto [sx, sy] = MapXY(START_X, START_Y);
      sdlutil::ClipPixel32(screen,
                           sx, sy,
                           0xFF00FFFF);

    }

    for (auto [x, y, c] : points) {
      auto [px, py] = MapXY(x, y);
      sdlutil::ClipPixel32(screen, px, py, c);
    }
  }

  Periodically drawinfo_per;
  double total_draw = 0.0;
  int num_draws = 0;
  void Draw() {
    Timer draw_timer;
    sdlutil::clearsurface(screen, 0xFF001100);
    DrawSim();

    total_draw += draw_timer.Seconds();
    num_draws++;
    if (drawinfo_per.ShouldRun()) {
      printf("%d draws. Average draw time %.1fms\n",
             num_draws, (total_draw * 1000.0) / num_draws);
    }
  }

  void Loop() {
    Periodically makepoint_per(5.0);
    for (;;) {

      for (int i = 0; i < 1000; i++)
        sim.UpdatePhysics(0.01, physics_per.ShouldRun());
      ui_dirty = true;

      if (makepoint_per.ShouldRun()) {
        for (const Object &obj : sim.objects) {
          points.emplace_back(obj.x_m, obj.y_m, obj.color);
        }
      }
      
      SDL_Event event;
      if (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
          printf("QUIT.\n");
          return;

        case SDL_MOUSEMOTION: {
          SDL_MouseMotionEvent *e = (SDL_MouseMotionEvent*)&event;

          mousex = e->x;
          mousey = e->y;

          if (drag_origin.has_value()) {
            // In the midst of a drag.
            auto [osx, osy, omx, omy] = drag_origin.value();
            SetScroll(osx + (mousex - omx), osy + (mousey - omy));
            
            ui_dirty = true;
          }

          // info hover
          ui_dirty = true;
          
          break;
        }

        case SDL_KEYDOWN: {
          switch (event.key.keysym.sym) {
          case SDLK_ESCAPE:
            printf("ESCAPE.\n");
            return;

          case SDLK_SPACE:
            Reset();
            ui_dirty = true;
            break;
          default:;
          }
          break;
        }

        case SDL_MOUSEBUTTONDOWN: {
          // LMB/RMB, drag, etc.
          SDL_MouseButtonEvent *e = (SDL_MouseButtonEvent*)&event;
          printf("Mouse button %d\n", e->button);
          mousex = e->x;
          mousey = e->y;

          if (e->button == SDL_BUTTON_LEFT) {
            drag_origin = make_optional(make_tuple(
                                            scrollx, scrolly,
                                            mousex, mousey));
            SDL_SetCursor(cursor_hand_closed);
            ui_dirty = true;
          } else if (e->button == SDL_BUTTON_WHEELUP) {
            zoom_mpp /= 1.1;
            ui_dirty = true;
#if 0
            auto [fx, fy] = GetNormalizedPos(mousex, mousey);
            
            // scroll up = zoom in
            if (current_zoom > 0) {
              current_zoom--;
              ui_dirty = true;
            }
            printf("Old f %.2f,%.2f now zoom %d\n", fx, fy, current_zoom);

            auto [sx, sy] = GetScrollToNormalized(mousex, mousey, fx, fy);
            SetScroll(sx, sy);
#endif
          } else if (e->button == SDL_BUTTON_WHEELDOWN) {
            zoom_mpp *= 1.1;
            ui_dirty = true;
#if 0
            auto [fx, fy] = GetNormalizedPos(mousex, mousey);
            if (current_zoom < mipmaps.size() - 1) {
              current_zoom++;
              ui_dirty = true;
            }

            printf("Old f %.2f,%.2f now zoom %d\n", fx, fy, current_zoom);

            auto [sx, sy] = GetScrollToNormalized(mousex, mousey, fx, fy);
            SetScroll(sx, sy);
#endif
          }

          break;
        }

        case SDL_MOUSEBUTTONUP: {
          SDL_MouseButtonEvent *e = (SDL_MouseButtonEvent*)&event;
          printf("Mouse button %d\n", e->button);
          // LMB/RMB, drag, etc.
          if (e->button == SDL_BUTTON_LEFT) {
            drag_origin.reset();
            SDL_SetCursor(cursor_arrow);
          }
          break;
        }

        default:;
        }
      }

      if (ui_dirty) {
        Draw();
        SDL_Flip(screen);
        ui_dirty = false;
      }
    }
    
  }
  
};

int main(int argc, char **argv) {

  // Solve();
  // return 0;
  
  CHECK(SDL_Init(SDL_INIT_VIDEO) >= 0);
  fprintf(stderr, "SDL initialized OK.\n");

  screen = sdlutil::makescreen(SCREENW, SCREENH);
  CHECK(screen != nullptr);

  font = Font::Create(screen,
                      "..\\..\\cc-lib\\sdl\\font.png",
                      FONTCHARS,
                      FONTWIDTH, FONTHEIGHT, FONTSTYLES, 1, 3);
  CHECK(font != nullptr) << "Couldn't load font.";
  
  CHECK((cursor_arrow = Cursor::MakeArrow()));
  CHECK((cursor_bucket = Cursor::MakeBucket()));
  CHECK((cursor_hand = Cursor::MakeHand()));
  CHECK((cursor_hand_closed = Cursor::MakeHandClosed()));
  CHECK((cursor_eraser = Cursor::MakeEraser()));
  
  UI ui;
  ui.Loop();
  
  SDL_Quit();
  return 0;
}  
