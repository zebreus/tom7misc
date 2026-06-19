
#ifndef _TOWARD_SDL_RENDERING_H
#define _TOWARD_SDL_RENDERING_H

#include "rendering.h"

#include <memory>

// Must have called Initialization::Initialize for SDL.
std::unique_ptr<Rendering> CreateSDLGLRendering();

#endif
