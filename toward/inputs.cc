
#include "inputs.h"

#include <cstdint>
#include <memory>

#include "SDL.h"
#include "SDL_events.h"
#include "SDL_keycode.h"


namespace {

struct SDLInputs : public Inputs {
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
        if ((mods & MOD_SHIFT) && sym >= 'a' && sym <= 'z') {
          sym -= 32;
        }

        if (e.type == SDL_KEYDOWN) {
          return KeyDown{(uint8_t)sym, mods};
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

