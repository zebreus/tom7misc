// Fork of image.h for large (greater than 2^32 pixels) images.
// Might make sense to merge this back into cc-lib if it works out?

#ifndef _PINGU_IMAGE64_H
#define _PINGU_IMAGE64_H

#include <string>
#include <vector>
#include <cstdint>
#include <tuple>
#include <optional>
#include <algorithm>

#include "base/logging.h"

struct Image64A;

// 4-channel image in R-G-B-A order.
// uint32s are represented like 0xRRGGBBAA irrespective of native
// byte order.
struct Image64RGBA {
  using uint8 = uint8_t;
  using uint32 = uint32_t;
  using int64 = int64_t;  
  Image64RGBA(const std::vector<uint8> &rgba, int64 width, int64 height);
  Image64RGBA(int64 width, int64 height);
  Image64RGBA() : width(0), height(0) {}

  // TODO: copy/assignment

  int64 Width() const { return width; }
  int64 Height() const { return height; }


  static Image64RGBA *Load(const std::string &filename);
  static Image64RGBA *LoadFromMemory(const std::vector<uint8> &bytes);
  static Image64RGBA *LoadFromMemory(const char *data, size_t size);
  // Saves in RGBA PNG format. Returns true if successful.
  bool Save(const std::string &filename) const;
  std::vector<uint8> SaveToVec() const;
  std::string SaveToString() const;

  // Quality in [1, 100]. Returns true if successful.
  bool SaveJPG(const std::string &filename, int quality = 90) const;
  // TODO: jpg to vec, to string

  // TODO: Replace with copy constructor
  Image64RGBA *Copy() const;
  // Crop (or pad), returning a new image of the given width and height.
  // If this includes any area outside the input image, fill with
  // fill_color.
  Image64RGBA Crop32(int64 x, int64 y, int64 w, int64 h,
                     uint32 fill_color = 0x00000000) const;

  // Scale by a positive integer factor, crisp pixels.
  Image64RGBA ScaleBy(int scale) const;
  // Scale down by averaging boxes of size scale x scale to produce
  // a pixel value. If the width and height are not divisible by
  // the scale, pixels are dropped.
  Image64RGBA ScaleDownBy(int scale) const;
  
  // In RGBA order, where R value is MSB. x/y must be in bounds.
  inline uint32 GetPixel32(int64 x, int64 y) const;
  inline std::tuple<uint8, uint8, uint8, uint8>
  GetPixel(int64 x, int64 y) const;

  // Clear the image to a single value.
  void Clear(uint8 r, uint8 g, uint8 b, uint8 a);
  void Clear32(uint32 rgba);

  inline void SetPixel(int64 x, int64 y, uint8 r, uint8 g, uint8 b, uint8 a);
  inline void SetPixel32(int64 x, int64 y, uint32 rgba);

  // Blend pixel with existing data.
  // Note: Currently assumes existing alpha is 0xFF.
  void BlendPixel(int64 x, int64 y, uint8 r, uint8 g, uint8 b, uint8 a);
  void BlendPixel32(int64 x, int64 y, uint32 color);

  // Blend a filled rectangle. Clips.
  void BlendRect(int64 x, int64 y, int64 w, int64 h,
                 uint8 r, uint8 g, uint8 b, uint8 a);
  void BlendRect32(int64 x, int64 y, int64 w, int64 h, uint32 color);

  // Hollow box, one pixel width.
  // nullopt corner_color = color for a crisp box, but setting
  // the corners to 50% alpha makes a subtle roundrect effect.
  void BlendBox32(int64 x, int64 y, int64 w, int64 h,
                  uint32 color, std::optional<uint32> corner_color);

  // Embedded 9x9 pixel font.
  void BlendText(int64 x, int64 y,
                 uint8 r, uint8 g, uint8 b, uint8 a,
                 const std::string &s);
  void BlendText32(int64 x, int64 y, uint32 color, const std::string &s);

  // Same font, but scaled to (crisp) 2x2 pixels.
  void BlendText2x(int64 x, int64 y,
                   uint8 r, uint8 g, uint8 b, uint8 a,
                   const std::string &s);
  void BlendText2x32(int64 x, int64 y, uint32 color, const std::string &s);

  // Clipped. Alpha blending.
  // This draws a crisp pixel line using Bresenham's algorithm.
  void BlendLine(int64 x1, int64 y1, int64 x2, int64 y2,
                 uint8 r, uint8 g, uint8 b, uint8 a);
  void BlendLine32(int64 x1, int64 y1, int64 x2, int64 y2, uint32 color);

  // Clipped. Alpha blending.
  // Blends an anti-aliased line using Wu's algorithm; slower.
  // Endpoints are pixel coordinates, but can be sub-pixel.
  void BlendLineAA(double x1, double y1, double x2, double y2,
                   uint8 r, uint8 g, uint8 b, uint8 a);
  void BlendLineAA32(double x1, double y1, double x2, double y2, uint32 color);

  // Clipped, alpha blending.
  void BlendImage(int64 x, int64 y, const Image64RGBA &other);
  void BlendImageRect(int64 destx, int64 desty, const Image64RGBA &other,
                      int64 srcx, int64 srcy, int64 srcw, int64 srch);
  // Clipped, but copy source alpha and ignore current image contents.
  void CopyImage(int64 x, int64 y, const Image64RGBA &other);
  void CopyImageRect(int64 destx, int64 desty, const Image64RGBA &other,
                     int64 srcx, int64 srcy, int64 srcw, int64 srch);

  // Output value is RGBA floats in [0, 255].
  // Alpha values are just treated as a fourth channel and averaged,
  // which is not really correct.
  // Treats the input pixels as being "located" at their top-left
  // corners (not their centers).
  // x/y out of bounds will repeat edge pixels.
  std::tuple<float, float, float, float>
  SampleBilinear(double x, double y) const;
  
  // Extract single channel.
  Image64A Red() const;
  Image64A Green() const;
  Image64A Blue() const;
  Image64A Alpha() const;

  // Images must all be the same dimensions.
  static Image64RGBA FromChannels(const Image64A &red,
                                const Image64A &green,
                                const Image64A &blue,
                                const Image64A &alpha);

private:
  int64 width, height;
  // Size width * height * 4.
  std::vector<uint8> rgba;
};

// Single-channel 8-bit bitmap.
struct Image64A {
  using uint8 = uint8_t;
  using int64 = int64_t;
  Image64A(const std::vector<uint8> &alpha, int64 width, int64 height);
  Image64A(int64 width, int64 height);
  // Empty image sometimes useful for filling vectors, etc.
  Image64A() : Image64A(0, 0) {}
  // Value semantics.
  Image64A(const Image64A &other) = default;
  Image64A(Image64A &&other) = default;
  Image64A &operator =(const Image64A &other) = default;
  Image64A &operator =(Image64A &&other) = default;

  bool operator ==(const Image64A &other) const;
  std::size_t Hash() const;

  int64 Width() const { return width; }
  int64 Height() const { return height; }

  Image64A *Copy() const;
  // Scale by a positive integer factor, crisp pixels.
  Image64A ScaleBy(int scale) const;
  // Generally appropriate for enlarging, not shrinking.
  Image64A ResizeBilinear(int64 new_width, int64 new_height) const;
  // Nearest-neighbor; works "fine" for enlarging and shrinking.
  Image64A ResizeNearest(int64 new_width, int64 new_height) const;
  // Make a four-channel image of the same size, R=v, G=v, B=v, A=0xFF.
  Image64RGBA GreyscaleRGBA() const;
  // Make a four-channel alpha mask of the same size, RGB=rgb, A=v
  Image64RGBA AlphaMaskRGBA(uint8 r, uint8 g, uint8 b) const;

  // Only increases values.
  void BlendText(int64 x, int64 y, uint8 v, const std::string &s);

  void Clear(uint8 value);

  void BlendImage(int64 x, int64 y, const Image64A &other);

  // Clipped.
  inline void SetPixel(int64 x, int64 y, uint8 v);
  // x/y must be in bounds.
  inline uint8 GetPixel(int64 x, int64 y) const;
  inline void BlendPixel(int64 x, int64 y, uint8 v);

  // Output value is a float in [0, 255].
  // Treats the input pixels as being "located" at their top-left
  // corners (not their centers).
  // x/y out of bounds will repeat edge pixels.
  float SampleBilinear(double x, double y) const;

private:
  int64 width, height;
  // Size width * height.
  std::vector<uint8> alpha;
};



// Implementations follow.


Image64RGBA::uint32 Image64RGBA::GetPixel32(int64 x, int64 y) const {
  // Treat out-of-bounds reads as containing 00,00,00,00.
  if (x < 0 || x >= width ||
      y < 0 || y >= height) return 0;
  const int64 base = (y * width + x) << 2;
  const uint8_t r = rgba[base + 0];
  const uint8_t g = rgba[base + 1];
  const uint8_t b = rgba[base + 2];
  const uint8_t a = rgba[base + 3];
  return ((uint32)r << 24) | ((uint32)g << 16) | ((uint32)b << 8) | (uint32)a;
}

std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>
Image64RGBA::GetPixel(int64 x, int64 y) const {
  // Treat out-of-bounds reads as containing 00,00,00,00.
  if (x < 0 || x >= width ||
      y < 0 || y >= height) return std::make_tuple(0, 0, 0, 0);
  const int64 base = (y * width + x) << 2;
  return std::make_tuple(
      rgba[base], rgba[base + 1], rgba[base + 2], rgba[base + 3]);
}

void Image64RGBA::SetPixel(int64 x, int64 y,
                         uint8 r, uint8 g, uint8 b, uint8 a) {
  if (x < 0 || x >= width ||
      y < 0 || y >= height) return;
  int64 i = (y * width + x) * 4;
  rgba[i + 0] = r;
  rgba[i + 1] = g;
  rgba[i + 2] = b;
  rgba[i + 3] = a;
}

void Image64RGBA::SetPixel32(int64 x, int64 y, uint32 color) {
  if (x < 0 || x >= width ||
      y < 0 || y >= height) return;
  int64 i = (y * width + x) * 4;
  rgba[i + 0] = (color >> 24) & 255;
  rgba[i + 1] = (color >> 16) & 255;
  rgba[i + 2] = (color >>  8) & 255;
  rgba[i + 3] = (color      ) & 255;
}


uint8_t Image64A::GetPixel(int64 x, int64 y) const {
  return alpha[y * width + x];
}

void Image64A::SetPixel(int64 x, int64 y, uint8_t value) {
  if (x < 0 || y < 0) return;
  if (x >= width || y >= height) return;
  alpha[y * width + x] = value;
}

void Image64A::BlendPixel(int64 x, int64 y, uint8_t v) {
  if (x < 0 || y < 0) return;
  if (x >= width || y >= height) return;
  // XXX test this blending math
  uint8_t old = GetPixel(x, y);
  uint16_t opaque_part = 255 * v;
  uint16_t transparent_part = (255 - v) * old;
  uint8_t new_value =
    0xFF & ((opaque_part + transparent_part) / (uint16_t)255);
  SetPixel(x, y, new_value);
}



#endif
