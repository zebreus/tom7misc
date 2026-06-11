
#include "initialization.h"

#include <string>
#include <format>

#include "base/logging.h"
#include "SDL.h"

void Initialization::Initialize() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::string err = std::format("Failed to initialize SDL: {}",
                                  SDL_GetError());
    LOG(FATAL) << "Could not initialize SDL.\n" << err;
  }
}

void Initialization::Exit() {
  SDL_Quit();
}
