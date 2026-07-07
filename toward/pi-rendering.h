
#ifndef _TOWARD_PI_RENDERING_H
#define _TOWARD_PI_RENDERING_H

#include "rendering.h"

#include <memory>

// This also uses SDL. It is not raspberry pi specific, but
// it uses a lesser GL version and goes fullscreen.
// Must have called Initialization::Initialize for SDL.
std::unique_ptr<Rendering> CreatePiRendering();

#endif
