
#ifndef _GRAD_MNIST_H
#define _GRAD_MNIST_H

#include <cstdint>
#include <string>

#include "util.h"
#include "image.h"
#include "base/logging.h"

// Set of labeled images, which could be either train or test.
struct MNIST {
  MNIST(const std::string &base) {
    LoadLabels(base + "-labels-idx1-ubyte");
    LoadImages(base + "-images-idx3-ubyte");
    CHECK(images.size() == labels.size());
  }

  void LoadLabels(const std::string &filename) {
    std::vector<uint8_t> bytes = Util::ReadFileBytes(filename);

    int idx = 0;
    auto GetByte = [&idx, &bytes]() {
        CHECK(idx <= bytes.size());
        uint8_t r = bytes[idx];
        idx++;
        return r;
      };
    auto Get32 = [&GetByte]() {
        uint32_t a = GetByte();
        uint32_t b = GetByte();
        uint32_t c = GetByte();
        uint32_t d = GetByte();

        return (a << 24) | (b << 16) | (c << 8) | d;
      };

    CHECK(Get32() == 0x00000801);
    int num_items = Get32();
    labels.resize(num_items);
    for (int i = 0; i < num_items; i++) {
      uint8_t lab = GetByte();
      CHECK(lab <= 0x09) << lab;
      labels[i] = lab;
    }
    CHECK(idx == bytes.size());
  }

  void LoadImages(const std::string &filename) {
    std::vector<uint8_t> bytes = Util::ReadFileBytes(filename);

    int idx = 0;
    auto GetByte = [&idx, &bytes]() {
        CHECK(idx <= bytes.size());
        uint8_t r = bytes[idx];
        idx++;
        return r;
      };
    auto Get32 = [&GetByte]() {
        uint32_t a = GetByte();
        uint32_t b = GetByte();
        uint32_t c = GetByte();
        uint32_t d = GetByte();

        return (a << 24) | (b << 16) | (c << 8) | d;
      };

    CHECK(Get32() == 0x00000803);
    int num_items = Get32();
    width = Get32();
    height = Get32();
    CHECK(width > 0 && height > 0);

    images.reserve(num_items);
    for (int i = 0; i < num_items; i++) {
      ImageA img(width, height);
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          img.SetPixel(x, y, GetByte());
        }
      }
      images.emplace_back(std::move(img));
    }
    CHECK(idx == bytes.size());
  }

  // 0x00 to 0x09.
  std::vector<uint8_t> labels;
  // All the images have the same width/height.
  int width = 0, height = 0;
  std::vector<ImageA> images;
};

#endif
