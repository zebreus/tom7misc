
// Video output. All the system-specific display stuff goes in here,
// and we should use only portable types to communicate the scene.

#ifndef _TOWARD_RENDERING_H
#define _TOWARD_RENDERING_H

#include <memory>
#include <mutex>
#include <span>

#include "yocto-math.h"

struct Rendering {
  static std::unique_ptr<Rendering> CreateSDLGL();

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

  // Render the scene right now to the display.
  virtual void RenderScene(
      vec2f viewport_min,
      vec2f viewport_max,
      std::span<const Triangle> scene);

  virtual ~Rendering();
};

#endif
