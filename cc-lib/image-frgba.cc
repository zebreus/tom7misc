
#include "image-frgba.h"
#include "tinyexr.h"
#include "image.h"
#include "color-util.h"

#include "base/logging.h"
#include "util.h"

ImageFRGBA::ImageFRGBA(const std::vector<float> &rgbaf,
                       int64 width, int64 height) :
  width(width), height(height), rgba(rgbaf) {
  CHECK((int64)rgbaf.size() == width * height * 4);
}

ImageFRGBA::ImageFRGBA(const float *rgbaf,
                       int64 width, int64 height) :
  width(width), height(height) {
  printf("%lld x %lld x 4 = %lld\n", width, height, width * height * 4);
  rgba.resize(width * height * 4);
  for (int64 idx = 0; idx < width * height * 4; idx++) {
    rgba[idx] = rgbaf[idx];
  }
  printf("hi\n");
}

ImageFRGBA::ImageFRGBA(int64 width, int64 height) :
  width(width), height(height), rgba(width * height * 4, 0.0f) {
}

bool ImageFRGBA::operator==(const ImageFRGBA &other) const {
  return other.Width() == Width() &&
    other.Height() == Height() &&
    other.rgba == rgba;
}

void ImageFRGBA::Clear(float r, float g, float b, float a) {
  for (int64 px = 0; px < width * height; px++) {
    rgba[px * 4 + 0] = r;
    rgba[px * 4 + 1] = g;
    rgba[px * 4 + 2] = b;
    rgba[px * 4 + 3] = a;
  }
}

ImageRGBA ImageFRGBA::ToRGBA() const {
  ImageRGBA out(Width(), Height());
  for (int64 y = 0; y < Height(); y++) {
    for (int64 x = 0; x < Width(); x++) {
      const auto [r, g, b, a] = GetPixel(x, y);
      uint32_t c = ColorUtil::FloatsTo32(r, g, b, a);
      out.SetPixel32(x, y, c);
    }
  }
  return out;
}

ImageFRGBA::ImageFRGBA(const ImageRGBA &other) :
  width(other.Width()), height(other.Height()) {
  for (int64 y = 0; y < Height(); y++) {
    for (int64 x = 0; x < Width(); x++) {
      uint32_t c = other.GetPixel32(x, y);
      const auto [r, g, b, a] = ColorUtil::U32ToFloats(c);
      SetPixel(x, y, r, g, b, a);
    }
  }
}

ImageFRGBA *ImageFRGBA::Load(const std::string &filename) {
  std::vector<uint8_t> buffer =
    Util::ReadFileBytes(filename);
  if (buffer.empty()) return nullptr;
  return LoadFromMemory(buffer);
}

ImageFRGBA *ImageFRGBA::LoadFromMemory(const std::vector<uint8_t> &buffer) {
  return LoadFromMemory((const unsigned char *)buffer.data(), buffer.size());
}

ImageFRGBA *ImageFRGBA::LoadFromMemory(const unsigned char *data, size_t size) {
  float *out_rgba = nullptr;
  int width = 0, height = 0;
  const char *err = nullptr;

  if (LoadEXRWithLayerFromMemory(
          &out_rgba, &width, &height, data, size, nullptr, &err) < 0) {
    if (err != nullptr) printf("Err: %s\n", err);
    return nullptr;
  }

  printf("%d x %d\n", width, height);

  if (width <= 0 || height <= 0) {
    if (err != nullptr) printf("Err: %s\n", err);
    return nullptr;
  }

  ImageFRGBA *ret = new ImageFRGBA(out_rgba, width, height);
  printf("ok?\n");
  free(out_rgba);
  printf("freeed\n");

  return ret;
}
