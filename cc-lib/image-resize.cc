
#include "image-resize.h"
#include "image.h"

#include <bit>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR__HEADER_FILENAME "stb_image_resize.h"

// The resize code was not written with these warnings in mind.
// Suppress them so that compiles with -Wall can still be clean.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Warray-bounds"
#endif

#include "stb_image_resize.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif

// ImageRGBA is always 0xRRGGBBAA.
static constexpr stbir_pixel_layout BYTE_LAYOUT =
  std::endian::native == std::endian::little ? STBIR_ABGR : STBIR_RGBA;

ImageRGBA ImageResize::Resize(const ImageRGBA &src, int w, int h) {
  ImageRGBA out(w, h);
  out.Clear32(0xFFFFFFFF);
  (void)stbir_resize_uint8_srgb((const unsigned char*)src.rgba.data(),
                                src.Width(), src.Height(), 0,
                                (unsigned char *)out.rgba.data(), w, h, 0,
                                BYTE_LAYOUT);
  return out;
}

// Nearest-neighbor sampling.
ImageRGBA ImageResize::ResizeNearest(const ImageRGBA &src, int w, int h) {
  ImageRGBA out(w, h);
  out.Clear32(0xFFFFFFFF);
  (void)stbir_resize(src.rgba.data(), src.Width(), src.Height(), 0,
                     out.rgba.data(), w, h, 0,
                     BYTE_LAYOUT, STBIR_TYPE_UINT8_SRGB,
                     STBIR_EDGE_CLAMP, STBIR_FILTER_POINT_SAMPLE);
  return out;
}
