
// Video output. All the system-specific display stuff goes in here,
// and we should use only portable types to communicate the scene.
// See sdl-rendering for a concrete instance.

#ifndef _TOWARD_RENDERING_H
#define _TOWARD_RENDERING_H

#include <memory>
#include <span>

#include "yocto-math.h"

struct Rendering {
  using vec2f = yocto::vec2f;

  struct Triangle {
    // vertices
    vec2f a, b, c;
    // RGBA color
    uint32_t rgba;
    // To make each triangle be eight 32-bit fields.
    // Could be used for effects in the future.
    uint32_t reserved;
  };

  // TODO: Some way to get absolute positioning for UI elements?

  // Render the scene right now to the display.
  virtual void RenderScene(
      vec2f viewport_min,
      vec2f viewport_max,
      std::span<const Triangle> scene) = 0;

  // Where in rendering space (Cartesian) is this pixel (screen)?
  // e.g. For a mouse click on the screen.
  virtual vec2f CartesianPixel(vec2f viewport_min,
                               vec2f viewport_max,
                               int x, int y) = 0;

  virtual ~Rendering();
};

#endif
