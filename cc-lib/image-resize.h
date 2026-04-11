
#ifndef _CC_LIB_IMAGE_RESIZE_H
#define _CC_LIB_IMAGE_RESIZE_H

#include "image.h"

struct ImageResize {
  // Good blend of quality and speed.
  // Cubic spline for upscale, Mitchell for downscale.
  static ImageRGBA Resize(const ImageRGBA &src, int w, int h);

  // Nearest-neighbor sampling.
  static ImageRGBA ResizeNearest(const ImageRGBA &src, int w, int h);

  // TODO: Float
};

#endif
