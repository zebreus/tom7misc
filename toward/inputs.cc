
#include "inputs.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include "SDL.h"
#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_mouse.h"


namespace {

struct SDLInputs : public Inputs {
  static constexpr std::array<uint8_t, 128> SHIFT_KEY = []{
      std::array<uint8_t, 128> ret;
      // By default, map to self.
      for (int i = 0; i < 128; i++) ret[i] = i;
      for (int i = 'a'; i <= 'z'; i++) ret[i] = i - 32;

      static constexpr char S[] = "`1234567890-=[]\\;',./";
      static constexpr char D[] = "~!@#$%^&*()_+{}|:\"<>?";
      static_assert(sizeof (S) == sizeof (D));
      for (int i = 0; i < sizeof (S); i++) {
        ret[S[i]] = D[i];
      }
      return ret;
    }();

  std::optional<Input> pending_;

  Input GetInput() override {
    if (pending_) {
      Input i = *pending_;
      pending_.reset();
      return i;
    }

    SDL_Event e = {};
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        return Exit{};
      }

      if (e.type == SDL_MOUSEBUTTONDOWN) {
        uint8_t button = MOUSE_LEFT;
        if (e.button.button == SDL_BUTTON_LEFT) {
          button = MOUSE_LEFT;
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
          button = MOUSE_RIGHT;
        } else if (e.button.button == SDL_BUTTON_MIDDLE) {
          button = MOUSE_MIDDLE;
        } else {
          // XXX support other buttons as generic events?
          continue;
        }

        uint8_t mask = 0;
        uint32_t state = SDL_GetMouseState(nullptr, nullptr);
        if (state & SDL_BUTTON_LMASK) mask |= MOUSE_LEFT;
        if (state & SDL_BUTTON_RMASK) mask |= MOUSE_RIGHT;
        if (state & SDL_BUTTON_MMASK) mask |= MOUSE_MIDDLE;
        pending_ = MouseChange{e.button.x, e.button.y, 0, 0, mask};

        return MouseClick{e.button.x, e.button.y, button};
      }

      if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONUP) {
        uint8_t mask = 0;
        uint32_t state = SDL_GetMouseState(nullptr, nullptr);
        if (state & SDL_BUTTON_LMASK) mask |= MOUSE_LEFT;
        if (state & SDL_BUTTON_RMASK) mask |= MOUSE_RIGHT;
        if (state & SDL_BUTTON_MMASK) mask |= MOUSE_MIDDLE;
        int x = e.type == SDL_MOUSEMOTION ? e.motion.x : e.button.x;
        int y = e.type == SDL_MOUSEMOTION ? e.motion.y : e.button.y;
        int dx = e.type == SDL_MOUSEMOTION ? e.motion.xrel : 0;
        int dy = e.type == SDL_MOUSEMOTION ? e.motion.yrel : 0;
        return MouseChange{x, y, dx, dy, mask};
      }

      if (e.type == SDL_MOUSEWHEEL) {
        if (e.wheel.y != 0) {
          // This incorporates "natural scrolling," but we aren't scrolling.
          bool scroll_up = e.wheel.y > 0;

          int x, y;
          SDL_GetMouseState(&x, &y);

          return MouseWheel{
            .x = x,
            .y = y,
            .up = scroll_up != (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED),
          };
        }
      }

      if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        uint8_t mods = 0;
        if (e.key.keysym.mod & KMOD_CTRL) mods |= MOD_CTRL;
        if (e.key.keysym.mod & KMOD_ALT) mods |= MOD_ALT;
        if (e.key.keysym.mod & KMOD_SHIFT) mods |= MOD_SHIFT;

        uint32_t sym = (uint32_t)e.key.keysym.sym;
        if (mods & MOD_SHIFT && sym < sizeof(SHIFT_KEY)) {
          sym = SHIFT_KEY[sym];
        }

        if (e.type == SDL_KEYDOWN) {
          return KeyDown{sym, mods};
        }
        return KeyUp{sym, mods};
      }
    }

    return None{};
  }
};

}  // namespace

Inputs::~Inputs() {}

std::unique_ptr<Inputs> Inputs::CreateSDL() {
  return std::make_unique<SDLInputs>();
}

