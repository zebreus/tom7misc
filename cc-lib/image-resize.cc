
#include "image-resize.h"
#include "image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR__HEADER_FILENAME "stb_image_resize.h"
#include "stb_image_resize.h"

ImageRGBA ImageResize::Resize(const ImageRGBA &src, int w, int h) {
  ImageRGBA out(w, h);
  out.Clear32(0xFFFFFFFF);
  (void)stbir_resize_uint8_srgb((const unsigned char*)src.rgba.data(),
                                src.Width(), src.Height(), 0,
                                (unsigned char *)out.rgba.data(), w, h, 0,
                                STBIR_ABGR);
  return out;
}
