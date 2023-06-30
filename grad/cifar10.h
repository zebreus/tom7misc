
#ifndef _GRAD_CIFAR10_H
#define _GRAD_CIFAR10_H

#include <cstdint>
#include <string>

#include "util.h"
#include "image.h"
#include "base/logging.h"
#include "base/stringprintf.h"

// Set of labeled images, which could be either train or test.
struct CIFAR10 {
  static inline constexpr int WIDTH = 32;
  static inline constexpr int HEIGHT = 32;
  static inline constexpr int RADIX = 10;

  CIFAR10(const std::string &dir, bool test) {
    if (test) {
      LoadImages(Util::dirplus(dir, "test_batch.bin"));
    } else {
      for (int i = 1; i <= 5; i++) {
        LoadImages(Util::dirplus(dir, StringPrintf("data_batch_%d.bin", i)));
      }
    }

    CHECK(labels.size() == images.size());
  }

  int Num() const {
    return labels.size();
  }

  void LoadImages(const std::string &filename) {
    std::vector<uint8_t> bytes = Util::ReadFileBytes(filename);
    const int one_image_size = 1 + 32 * 32 * 3;
    CHECK(!bytes.empty() && (bytes.size() % one_image_size) == 0) <<
      filename << " : " << bytes.size();

    ImageA aa(32, 32);
    aa.Clear(0xFF);

    const int num_images = bytes.size() / one_image_size;

    int file_idx = 0;
    auto GetByte = [&file_idx, &bytes]() {
        CHECK(file_idx <= bytes.size());
        uint8_t r = bytes[file_idx];
        file_idx++;
        return r;
      };

    auto ReadChannel = [&GetByte](ImageA *img) {
        for (int y = 0; y < 32; y++) {
          for (int x = 0; x < 32; x++) {
            img->SetPixel(x, y, GetByte());
          }
        }
      };

    for (int ex = 0; ex < num_images; ex++) {
      uint8_t label = GetByte();
      CHECK(label < RADIX) << filename << ": " << label;

      ImageA rr(32, 32);
      ImageA gg(32, 32);
      ImageA bb(32, 32);

      ReadChannel(&rr);
      ReadChannel(&gg);
      ReadChannel(&bb);

      labels.push_back(label);
      images.push_back(ImageRGBA::FromChannels(rr, gg, bb, aa));
    }
  }

  static const char *LabelString(uint8_t label) {
    static_assert(RADIX == 10);
    switch (label) {
    case 0: return "airplane";
    case 1: return "automobile";
    case 2: return "bird";
    case 3: return "cat";
    case 4: return "deer";
    case 5: return "dog";
    case 6: return "frog";
    case 7: return "horse";
    case 8: return "ship";
    case 9: return "truck";
    default:
      return "invalid";
    }
  }

  // 0x00 to RADIX-1.
  std::vector<uint8_t> labels;
  // All the images are WIDTHxHEIGHT. Alpha channel always 0xFF.
  std::vector<ImageRGBA> images;
};

#endif
