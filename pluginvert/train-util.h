
#ifndef _PLUGINVERT_TRAIN_UTIL_H
#define _PLUGINVERT_TRAIN_UTIL_H

#include <cstdint>
#include <vector>
#include <memory>
#include <string>

#include "network.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "threadutil.h"

// TODO: to .cc
struct TrainingImages {
  TrainingImages(const Network &net,
                 const std::string &basename,
                 const std::string &title,
                 int image_every,
                 int image_width = 3000,
                 int image_height = 1000,
                 bool continue_from_disk = true) :
    IMAGE_WIDTH(image_width), IMAGE_HEIGHT(image_height), basename(basename) {
    CHECK((IMAGE_WIDTH % 2) == 0) << "Assumes even width of image for "
      "shrinking step";

    // No images for input layer; it is not used.
    images.emplace_back();
    image_x.emplace_back();

    for (int layer_idx = 1;
         layer_idx < net.layers.size();
         layer_idx++) {
      images.emplace_back();
      image_x.emplace_back();
      for (int chunk_idx = 0;
           chunk_idx < net.layers[layer_idx].chunks.size();
           chunk_idx++) {
        const Chunk &chunk = net.layers[layer_idx].chunks[chunk_idx];

        image_x.back().push_back(0);
        images.back().emplace_back(nullptr);

        std::unique_ptr<ImageRGBA> *image = &images.back().back();

        if (chunk.type == CHUNK_INPUT || chunk.fixed) {
          // Skip input chunks and fixed chunks, since nothing interesting ever
          // happens for them.
          CHECK(image->get() == nullptr);
        } else {

          // Load existing image from disk if we have one.
          string filename = FilenameFor(layer_idx, chunk_idx);
          ImageRGBA *file_img = continue_from_disk ?
            ImageRGBA::Load(filename) : nullptr;
          if (file_img != nullptr) {
            CHECK(file_img->Width() == IMAGE_WIDTH &&
                  file_img->Height() == IMAGE_HEIGHT) << filename <<
              "has the wrong dimensions (got " << file_img->Width() <<
              " x " << file_img->Height() << "); delete it to continue";
            image->reset(file_img);
            // The next x position is stored at pixel 0,0.
            const uint32_t px = (*image)->GetPixel32(0, 0);
            const uint32_t nx = px >> 8;
            CHECK((px & 0xFF) == 0xFF &&
                  nx <= IMAGE_WIDTH) << filename << " does not have "
              "correct secret pixel or constants changed; delete it "
              "to continue";
            image_x.back().back() = nx;
            printf("Continuing from %s at %d.\n", filename.c_str(), nx);

          } else {
            image->reset(new ImageRGBA(IMAGE_WIDTH, IMAGE_HEIGHT));
            (*image)->Clear32(0x000000FF);
            std::string ct;
            switch (chunk.type) {
            case CHUNK_SPARSE: ct = "SPARSE"; break;
            case CHUNK_DENSE: ct = "DENSE"; break;
            case CHUNK_INPUT: ct = "INPUT"; break;
            case CHUNK_CONVOLUTION_ARRAY: ct = "CONV"; break;
            default: ct = ChunkTypeName(chunk.type); break;
            }

            (*image)->BlendText2x32(
                2, 2, 0x9999AAFF,
                StringPrintf(
                    "Layer %d.%d (%s) | %s | "
                    "Start Round %lld | 1 px = %d rounds",
                    layer_idx, chunk_idx, ct.c_str(),
                    title.c_str(),
                    net.rounds, image_every));
          }
        }
      }
    }
  }

  // Make sure you do net_gpu->ReadFromGPU() first!
  void Sample(const Network &net) {
    // Batch these so we can save in parallel at the end.
    vector<pair<ImageRGBA *, string>> to_save;

    for (int target_layer = 1;
         target_layer < net.layers.size();
         target_layer++) {
      CHECK(target_layer < images.size());
      for (int target_chunk = 0;
           target_chunk < net.layers[target_layer].chunks.size();
           target_chunk++) {
        CHECK(target_chunk < images[target_layer].size());
        ImageRGBA *image = images[target_layer][target_chunk].get();
        // For example, fixed chunks are skipped.
        if (image == nullptr) continue;

        // If we exceeded the bounds, shrink in place.
        if (image_x[target_layer][target_chunk] >= image->Width()) {
          printf("Shrink image for layer %d\n", target_layer);
          // Skips over the text at the top (but not any pixels that
          // were drawn over it...)
          for (int y = 18; y < IMAGE_HEIGHT; y++) {
            // First half gets the entire image shrunk 2:1.
            for (int x = 0; x < IMAGE_WIDTH / 2; x++) {
              const auto [r1, g1, b1, a1] = image->GetPixel(x * 2 + 0, y);
              const auto [r2, g2, b2, a2] = image->GetPixel(x * 2 + 1, y);
              // We know we have full alpha here.
              const uint8_t r = ((uint32_t)r1 + (uint32_t)r2) >> 1;
              const uint8_t g = ((uint32_t)g1 + (uint32_t)g2) >> 1;
              const uint8_t b = ((uint32_t)b1 + (uint32_t)b2) >> 1;
              image->SetPixel(x, y, r, g, b, 0xFF);
            }
            // And clear the second half.
            for (int x = IMAGE_WIDTH / 2; x < image->Width(); x++) {
              image->SetPixel(x, y, 0, 0, 0, 0xFF);
            }
          }
          image_x[target_layer][target_chunk] = IMAGE_WIDTH / 2;
        }

        const int ix = image_x[target_layer][target_chunk];
        if (ix >= image->Width()) continue;

        CHECK(net.layers.size() > 0);
        CHECK(target_layer < net.layers.size());
        const Layer &layer = net.layers[target_layer];
        const Chunk &chunk = layer.chunks[target_chunk];

        auto ToScreenY = [this](float w) {
            int yrev = w * float(IMAGE_HEIGHT / 4) + (IMAGE_HEIGHT / 2);
            int y = IMAGE_HEIGHT - yrev;
            // Always draw on-screen.
            return std::clamp(y, 0, IMAGE_HEIGHT - 1);
          };
        // 1, -1, x axis
        if (ix & 1) {
          image->BlendPixel32(ix, ToScreenY(1), 0xCCFFCC40);
          image->BlendPixel32(ix, ToScreenY(0), 0xCCCCFFFF);
          image->BlendPixel32(ix, ToScreenY(-1), 0xFFCCCC40);
        }

        const uint8_t weight_alpha =
          std::clamp((255.0f / sqrtf(chunk.weights.size())), 10.0f, 240.0f);

        for (float w : chunk.weights) {
          // maybe better to AA this?
          image->BlendPixel32(ix, ToScreenY(w),
                              0xFFFFFF00 | weight_alpha);
        }

        const uint8_t bias_alpha =
          std::clamp((255.0f / sqrtf(chunk.biases.size())), 10.0f, 240.0f);

        for (float b : chunk.biases) {
          image->BlendPixel32(ix, ToScreenY(b),
                              0xFF777700 | bias_alpha);
        }

        if (chunk.weight_update == ADAM) {
          CHECK(chunk.weights_aux.size() == 2 * chunk.weights.size());
          CHECK(chunk.biases_aux.size() == 2 * chunk.biases.size());
          for (int idx = 0; idx < chunk.weights.size(); idx++) {
            const float m = chunk.weights_aux[idx * 2 + 0];
            const float v = sqrtf(chunk.weights_aux[idx * 2 + 1]);

            image->BlendPixel32(ix, ToScreenY(m),
                                0xFFFF0000 | weight_alpha);
            image->BlendPixel32(ix, ToScreenY(v),
                                0xFF00FF00 | weight_alpha);
          }
          for (int idx = 0; idx < chunk.biases.size(); idx++) {
            const float m = chunk.biases_aux[idx * 2 + 0];
            const float v = sqrtf(chunk.biases_aux[idx * 2 + 1]);
            image->BlendPixel32(ix, ToScreenY(m),
                                0x00FF0000 | bias_alpha);
            image->BlendPixel32(ix, ToScreenY(v),
                                0x00FF7700 | bias_alpha);
          }
        }

        if (ix % 100 == 0) {
          const int next_x = ix + 1;
          // Width must fit in 24 bits.
          CHECK(next_x == (next_x & 0xFFFFFF)) << "out of range!";
          image->SetPixel32(0, 0, ((uint32_t)next_x << 8) | 0xFF);

          to_save.emplace_back(
              image,
              FilenameFor(target_layer, target_chunk));
        }
        image_x[target_layer][target_chunk]++;
      }
    }

    if (!to_save.empty()) {
      ParallelApp(to_save, [](const pair<ImageRGBA *, string> &p) {
          auto &[image, filename] = p;
          image->Save(filename);
        }, 4);
      printf("Wrote %d images\n", (int)to_save.size());
    }
  }

private:
  string FilenameFor(int layer, int chunk) const {
    return StringPrintf("%s-%d.%d.png",
                        basename.c_str(), layer, chunk);
  }

  const int IMAGE_WIDTH = 0, IMAGE_HEIGHT = 0;
  const std::string basename;
  // Parallel to layers/chunks
  std::vector<std::vector<std::unique_ptr<ImageRGBA>>> images;
  // we can just have one of these right?
  std::vector<std::vector<int>> image_x;
};


#endif
