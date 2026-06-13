
#include "inputs.h"

#include <array>
#include <cstdint>
#include <memory>

#include "SDL.h"
#include "SDL_events.h"
#include "SDL_keycode.h"


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

  Input GetInput() override {
    SDL_Event e = {};
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        return Exit{};
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

