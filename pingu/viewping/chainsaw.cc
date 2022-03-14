
#include <cstdint>
#include <string>
#include <mutex>
#include <cmath>

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

using namespace std;
using int64 = int64_t;

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

struct Object {
  double mass_kg = 5.0;
  double dx_mps = 100.0;
  double dy_mps = 0.0;
  double x_m = 0.0;
  double y_m = EARTH_RADIUS_M + 1.5;
};

struct UI {
  UI() : physics_per(1.0), drawinfo_per(5.0) {
    Reset();
  }

  void Reset() {
    objects.clear();

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
      objects.push_back(saw);
    }
    {
      Object saw;
      saw.mass_kg = 5.0;
      saw.dx_mps = 9800.0;
      saw.dy_mps = 200.0;
      saw.x_m = 0.0;
      saw.y_m = EARTH_RADIUS_M + 1.5;
      objects.push_back(saw);
    }
  }

  Periodically physics_per;
  void UpdatePhysics() {
    const bool verbose = physics_per.ShouldRun();
    const double s = 1.0; //  1 / 10.0; // = 1.0 / 60.0;
    for (Object &obj : objects) {
      double rsquared = (obj.x_m * obj.x_m) + (obj.y_m * obj.y_m);

      /*
      // in kg * m / s^2
      double f = GRAVITATIONAL_CONSTANT * (EARTH_MASS_KG * obj.mass_kg) /
        rsquared;
      double f_on_obj = (f / obj.mass_kg) * s;
      */

      // but object's mass cancels out (avoid precision loss).
      // Near the surface this is the famous 9.8 m/s^2.
      double f_on_obj =
        (GRAVITATIONAL_CONSTANT * EARTH_MASS_KG / rsquared) * s;

      // apply force along the vector
      double r = sqrt(rsquared);
      double normx = obj.x_m / r;
      double normy = obj.y_m / r;
      double gx_mps = -normx * f_on_obj;
      double gy_mps = -normy * f_on_obj;

      obj.x_m += obj.dx_mps * s;
      obj.y_m += obj.dy_mps * s;

      obj.dx_mps += gx_mps;
      obj.dy_mps += gy_mps;

      if (verbose) {
        printf("obj %.3f %.3f  d %.3f %.3f\n",
               obj.x_m, obj.y_m, obj.dx_mps, obj.dy_mps);
      }
    }
  }
  
  double scrollx = 0, scrolly = 0;

  vector<Object> objects;
  
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

    for (const Object &obj : objects) {
      auto [ox, oy] = MapXY(obj.x_m, obj.y_m);
      // printf("obj at %.2f %.2f\n", ox, oy);
      sdlutil::DrawCircle32(screen, ox, oy,
                            3, 0xFF33AAAA);
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
    for (;;) {

      UpdatePhysics();
      ui_dirty = true;
      
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
            SetScroll(osx - (mousex - omx), osy - (mousey - omy));
            
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
