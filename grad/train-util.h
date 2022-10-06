
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
#include "color-util.h"

// A single image storing history of some data, which is drawn a single column
// at a time.
struct HistoryImage {
  static constexpr int HEADER_HEIGHT = 20;
  HistoryImage(
      const std::string &filename,
      // Width must be a multiple of 2.
      // Height is height of column. We always add a header, so the output
      // image is taller.
      int width, int col_height,
      bool continue_from_disk = true) : filename(filename),
                                        col_height(col_height) {
    CHECK(width > 0 && col_height > 0);
    CHECK((width % 2) == 0) << "Requires even width of image for "
      "shrinking step";
    const int height = col_height + HEADER_HEIGHT;

    ImageRGBA *file_img = continue_from_disk ?
      ImageRGBA::Load(filename) : nullptr;
    if (file_img != nullptr) {
      CHECK(file_img->Width() == width &&
            file_img->Height() == height) << filename <<
        "has the wrong dimensions (got " << file_img->Width() <<
        " x " << file_img->Height() << "); delete it to continue";
      image.reset(file_img);
      // The next x position is stored at pixel 0,0.
      const uint32_t px = image->GetPixel32(0, 0);
      const uint32_t nx = px >> 8;
      CHECK((px & 0xFF) == 0xFF &&
            nx <= width) << filename << " does not have "
        "correct secret pixel or constants changed; delete it "
        "to continue";

      col = nx;
      printf("Continuing from %s at %d.\n", filename.c_str(), col);

    } else {
      image.reset(new ImageRGBA(width, height));
      image->Clear32(0x000000FF);
    }

    CHECK(image.get() != nullptr);
  }

  // You can draw anything you want in the title region.
  void ClearTitle() {
    image->BlendRect32(0, 0, image->Width(), HEADER_HEIGHT, 0x000000FF);
  }

  void SetTitle(const std::string &s) {
    ClearTitle();
    image->BlendText2x32(1, 1, 0x9999AAFF, s);
  }

  void AddColumn(const ImageRGBA &column) {
    CHECK(column.Width() == 1);
    CHECK(column.Height() == col_height);

    // If necessary, shrink old data to make room.
    if (col >= image->Width()) {
      // Skip over header.
      for (int y = HEADER_HEIGHT; y < image->Height(); y++) {
        // First half gets the entire image shrunk 2:1.
        for (int x = 0; x < image->Width() / 2; x++) {
          const auto [r1, g1, b1, a1] = image->GetPixel(x * 2 + 0, y);
          const auto [r2, g2, b2, a2] = image->GetPixel(x * 2 + 1, y);
          // We know we have full alpha here.
          const uint8_t r = ((uint32_t)r1 + (uint32_t)r2) >> 1;
          const uint8_t g = ((uint32_t)g1 + (uint32_t)g2) >> 1;
          const uint8_t b = ((uint32_t)b1 + (uint32_t)b2) >> 1;
          image->SetPixel(x, y, r, g, b, 0xFF);
        }
        // And clear the second half.
        for (int x = image->Width() / 2; x < image->Width(); x++) {
          image->SetPixel(x, y, 0, 0, 0, 0xFF);
        }
      }
      col = image->Width() / 2;
    }

    CHECK(col < image->Width());
    for (int y = 0; y < col_height; y++) {
      image->SetPixel32(col, y + HEADER_HEIGHT, column.GetPixel32(0, y));
    }
    col++;
  }

  void Save() {
    const int next_x = col + 1;
    // Width must fit in 24 bits.
    CHECK(next_x == (next_x & 0xFFFFFF)) << "out of range!";
    image->SetPixel32(0, 0, ((uint32_t)next_x << 8) | 0xFF);
    image->Save(filename);
  }

  std::string filename;
  int col_height = 0;
  std::unique_ptr<ImageRGBA> image;
  int col = 0;
};

// TODO: to .cc
struct TrainingImages {
  TrainingImages(const Network &net,
                 const std::string &basename,
                 const std::string &title,
                 int image_every,
                 int image_width = 3000,
                 int image_col_height = 2000,
                 bool continue_from_disk = true) :
    image_col_height(image_col_height) {

    // No images for input layer; it is not used.
    images.emplace_back();

    for (int layer_idx = 1;
         layer_idx < net.layers.size();
         layer_idx++) {
      // Empty vector of history images.
      images.emplace_back();
      for (int chunk_idx = 0;
           chunk_idx < net.layers[layer_idx].chunks.size();
           chunk_idx++) {
        const Chunk &chunk = net.layers[layer_idx].chunks[chunk_idx];

        // Start null, as we may not populate it.
        images.back().emplace_back(nullptr);
        std::unique_ptr<HistoryImage> *himage = &images.back().back();

        if (chunk.type == CHUNK_INPUT || chunk.fixed) {
          // Skip input chunks and fixed chunks, since nothing interesting ever
          // happens for them. (Note: This might not be true if we include errors
          // and stimulations.)
          CHECK(himage->get() == nullptr);
        } else {

          himage->reset(
              new HistoryImage(FilenameFor(basename, layer_idx, chunk_idx),
                               image_width, image_col_height,
                               continue_from_disk));

          (*himage)->ClearTitle();

          std::string ct;
          switch (chunk.type) {
          case CHUNK_SPARSE: ct = "SPARSE"; break;
          case CHUNK_DENSE: ct = "DENSE"; break;
          case CHUNK_INPUT: ct = "INPUT"; break;
          case CHUNK_CONVOLUTION_ARRAY: ct = "CONV"; break;
          default: ct = ChunkTypeName(chunk.type); break;
          }

          (*himage)->image->BlendText2x32(
              2, 2, 0x9999AAFF,
              StringPrintf(
                  "Layer %d.%d (%s) | %s | "
                  "Start Round %lld | 1 px = %d rounds",
                  layer_idx, chunk_idx, ct.c_str(),
                  title.c_str(),
                  net.rounds, image_every));

          int xpos = image_width;
          auto WriteRight = [&](const std::string &s, uint32_t color) {
              int w = s.size() * 9 * 2;
              xpos -= w;
              (*himage)->image->BlendText2x32(xpos, 2, color, s);
            };
          // weight: 0xFFFFFF00, bias: 0xFF777700
          // weight_aux: FFFF00, FF00FF
          // bias_aux: 00FF00, 00FFFF

          WriteRight("ux",  0x00FFFFFF);
          WriteRight(" ba", 0x00FF00FF);

          WriteRight("ux",  0xFF00FFFF);
          WriteRight(" wa", 0xFFFF00FF);

          WriteRight(" bias", 0xFF7777FF);
          WriteRight("weight", 0xFFFFFFFF);
        }
      }
    }
  }

  // Make sure you do net_gpu->ReadFromGPU() first!
  void Sample(const Network &net,
              int num_examples,
              const std::vector<std::vector<float>> &stims) {
    samples++;
    const bool save_this_time = (samples % 100) == 0;
    Asynchronously save_async(4);

    const int weight_height = image_col_height / 2;
    const int stim_height = image_col_height - weight_height;

    // Writing to the same column. We only save asynchronously.
    ImageRGBA column(1, image_col_height);

    for (int target_layer = 1;
         target_layer < net.layers.size();
         target_layer++) {
      CHECK(target_layer < images.size());

      CHECK(target_layer < stims.size());
      const std::vector<float> &layer_values = stims[target_layer];

      int64_t chunk_start = 0;
      for (int target_chunk = 0;
           target_chunk < net.layers[target_layer].chunks.size();
           target_chunk++) {
        CHECK(target_chunk < images[target_layer].size());
        CHECK(net.layers.size() > 0);
        CHECK(target_layer < net.layers.size());
        const Layer &layer = net.layers[target_layer];
        const Chunk &chunk = layer.chunks[target_chunk];

        HistoryImage *himage = images[target_layer][target_chunk].get();
        // For example, fixed chunks are skipped.
        if (himage != nullptr) {

          column.Clear32(0x000000FF);

          // top

          auto TopToScreenY = [this, weight_height](float w) {
              int yrev = w * float(weight_height / 4) + (weight_height / 2);
              int y = weight_height - yrev;
              // Always draw on-screen.
              return std::clamp(y, 0, weight_height - 1);
            };
          // 1, -1, x axis
          if (himage->col & 1) {
            column.BlendPixel32(0, TopToScreenY(1),  0xCCFFCC40);
            column.BlendPixel32(0, TopToScreenY(0),  0xCCCCFFFF);
            column.BlendPixel32(0, TopToScreenY(-1), 0xFFCCCC40);
          }

          const uint8_t weight_alpha =
            std::clamp((255.0f / sqrtf(chunk.weights.size())), 10.0f, 240.0f);

          const uint8_t bias_alpha =
            std::clamp((255.0f / sqrtf(chunk.biases.size())), 10.0f, 240.0f);

          // Write the aux stuff first, so it does not obscure the main values.
          if (chunk.weight_update == ADAM) {
            CHECK(chunk.weights_aux.size() == 2 * chunk.weights.size());
            CHECK(chunk.biases_aux.size() == 2 * chunk.biases.size());
            for (int idx = 0; idx < chunk.weights.size(); idx++) {
              const float m = chunk.weights_aux[idx * 2 + 0];
              const float v = sqrtf(chunk.weights_aux[idx * 2 + 1]);

              column.BlendPixel32(0, TopToScreenY(m),
                                  0xFFFF0000 | weight_alpha);
              column.BlendPixel32(0, TopToScreenY(v),
                                  0xFF00FF00 | weight_alpha);
            }
            for (int idx = 0; idx < chunk.biases.size(); idx++) {
              const float m = chunk.biases_aux[idx * 2 + 0];
              const float v = sqrtf(chunk.biases_aux[idx * 2 + 1]);

              column.BlendPixel32(0, TopToScreenY(m),
                                  0x00FF0000 | bias_alpha);
              column.BlendPixel32(0, TopToScreenY(v),
                                  0x00FFFF00 | bias_alpha);
            }
          }

          for (float w : chunk.weights) {
            // maybe better to AA this?
            column.BlendPixel32(0, TopToScreenY(w),
                                0xFFFFFF00 | weight_alpha);
          }

          for (float b : chunk.biases) {
            column.BlendPixel32(0, TopToScreenY(b),
                                0xFF777700 | bias_alpha);
          }

          // bottom

          auto BotToScreenY = [this, weight_height, stim_height](float w) {
              int yrev = w * float(stim_height / 4) + (stim_height / 2);
              int y = weight_height + stim_height - yrev;
              // Always draw on-screen.
              return std::clamp(y, weight_height, image_col_height - 1);
            };
          // 1, -1, x axis
          if (himage->col & 1) {
            column.BlendPixel32(0, BotToScreenY(1),  0xCCFFCC40);
            column.BlendPixel32(0, BotToScreenY(0),  0xCCCCFFFF);
            column.BlendPixel32(0, BotToScreenY(-1), 0xFFCCCC40);
          }

          const uint8_t stim_alpha =
            std::clamp((255.0f / sqrtf(chunk.num_nodes * num_examples)),
                       8.0f, 200.0f);

          for (int ex = 0; ex < num_examples; ex++) {
            for (int i = 0; i < chunk.num_nodes; i++) {
              const int idx =
                layer.num_nodes * ex + (chunk_start + i);

              float cf = i / (float)chunk.num_nodes;
              uint32_t c = ColorUtil::LinearGradient32(RAINBOW, cf);

              c &= ~0xFF;
              c |= stim_alpha;

              CHECK(idx >= 0 && idx < layer_values.size());
              const float f = layer_values[idx];
              column.BlendPixel32(0, BotToScreenY(f), c);
            }
          }

          himage->AddColumn(column);

          if (save_this_time) {
            save_async.Run([himage]() {
                himage->Save();
              });
          }
        }

        chunk_start += chunk.num_nodes;
      }
    }

    if (save_this_time) {
      save_async.Wait();
      printf("Saved training images.\n");
    }
  }

private:
  string FilenameFor(const std::string &basename, int layer, int chunk) const {
    return StringPrintf("%s-%d.%d.png",
                        basename.c_str(), layer, chunk);
  }

  static constexpr ColorUtil::Gradient RAINBOW{
    GradRGB(0.00f,  0xFF0000),
    GradRGB(0.15f,  0xFFFF00),
    GradRGB(0.30f,  0x00FF00),
    GradRGB(0.45f,  0x00FFFF),
    GradRGB(0.60f,  0x0000FF),
    GradRGB(0.75f,  0xFF00FF),
    GradRGB(1.00f,  0xFFFFFF),
  };

  const int image_col_height = 0;
  uint32_t samples = 0;
  // Parallel to layers/chunks
  std::vector<std::vector<std::unique_ptr<HistoryImage>>> images;
};

template<int RADIX>
struct ConfusionImage {
  static constexpr int COL_HEIGHT = 10 * RADIX * RADIX;
  ConfusionImage(int width,
                 const std::array<std::string, RADIX> &labels,
                 const std::string &filename) : labels(labels),
                                                himage(filename, width,
                                                       COL_HEIGHT, true) {
    static constexpr int LABCHAR = 6;
    auto Trunc = [&](std::string label) {
        if (label.size() > LABCHAR) label.resize(LABCHAR);
        return Util::Pad(LABCHAR, label);
      };

    // XXX Should be able to reserve space on left in historyimage
    if (himage.col == 0) {
      himage.SetTitle("Confusions: Actual | Predicted");

      // Init!
      int yy = HistoryImage::HEADER_HEIGHT;
      for (int actual = 0; actual < RADIX; actual++) {
        std::string alab = Trunc(labels[actual]);
        for (int pred = 0; pred < RADIX; pred++) {
          std::string plab = Trunc(labels[pred]);
          himage.image->BlendText32(1, yy, 0x8888CCFF, alab);
          himage.image->BlendText32(1 + LABCHAR * 9, yy,
                                    0x444444FF, "|");
          himage.image->BlendText32(1 + (LABCHAR + 1) * 9, yy,
                                    0xCC88CCFF, plab);
          yy += 10;
        }
      }

      himage.col = 1 + (LABCHAR + 1 + LABCHAR) * 9 + 2;
    }
  }

  void Add(const std::array<std::array<int, RADIX>, RADIX> &conf) {
    ImageRGBA col(1, COL_HEIGHT);
    col.Clear32(0x000000FF);
    int yy = 0;
    for (int r = 0; r < RADIX; r++) {

      int rtotal = 0;
      for (const int cell : conf[r])
        rtotal += cell;

      for (int c = 0; c < RADIX; c++) {
        float f = conf[r][c] / (float)rtotal;
        uint8_t v = std::clamp((int)(f * 255.0f), 0, 255);

        uint32_t color = 0xFF | ((r == c) ? (v << 16) : (v << 24));
        for (int y = 0; y < 9; y++) {
          col.SetPixel32(0, yy + y, color);
        }
        yy += 10;
      }
    }

    himage.AddColumn(col);
  }

  void Save() {
    himage.Save();
  }

  const std::array<std::string, RADIX> labels;
  HistoryImage himage;
};


// TODO: Use HistoryImage.
struct ErrorImage {
  static constexpr int MARGIN = 4;
  ErrorImage(int width,
             int examples_per_round,
             const std::string &filename,
             bool continue_from_disk = true) :
    width(width),
    height((examples_per_round + MARGIN) * 3),
    examples_per_round(examples_per_round),
    filename(filename) {

    printf("Create %s\n", filename.c_str());
    if (continue_from_disk) {
      image.reset(ImageRGBA::Load(filename));
      if (image.get() != nullptr) {
        // The next x position is stored at pixel 0,0.
        const uint32_t px = image->GetPixel32(0, 0);
        const uint32_t nx = px >> 8;
        CHECK((px & 0xFF) == 0xFF &&
              nx <= width) << filename << " does not have "
          "correct secret pixel or constants changed; delete it "
          "to continue";
        image_x = nx;
      }
    }

    if (image.get() == nullptr) {
      image = std::make_unique<ImageRGBA>(width, height);
      image->Clear32(0x000055FF);
    }
  }

  const std::string &Filename() const {
    return filename;
  }

  // Takes a vector with examples_per_round elements: {expected, actual}
  void Add(std::vector<std::pair<float, float>> ex) {
    CHECK(ex.size() == examples_per_round);
    // TODO: Shrink instead of clearing.
    if (image_x >= width) {
      image->Clear32(0x000055FF);
      image_x = 0;
    }

    CHECK(ex.size() == examples_per_round);
    std::sort(ex.begin(), ex.end(),
              [](const std::pair<float, float> &a,
                 const std::pair<float, float> &b) {
                if (a.first == b.first)
                  return a.second < b.second;
                return a.first < b.first;
              });

    for (int i = 0; i < examples_per_round; i++) {
      const auto &[expected, actual] = ex[i];
      const float diff = actual - expected;

      const uint32 ce = ColorUtil::LinearGradient32(RED_GREEN, expected);
      const uint32 ca = ColorUtil::LinearGradient32(RED_GREEN, actual);
      const uint32 cd = ColorUtil::LinearGradient32(RED_GREEN, diff);

      image->SetPixel32(image_x, MARGIN + i, ce);
      image->SetPixel32(
          image_x, MARGIN + examples_per_round + MARGIN + i, ca);
      image->SetPixel32(
          image_x, MARGIN + 2 * (examples_per_round + MARGIN) + i, cd);
    }

    image_x++;
  }

  void Save() {
    // Top-left is secret pixel as with training images.
    CHECK(image_x == (image_x & 0xFFFFFF)) << "out of range!";
    image->SetPixel32(0, 0, ((uint32_t)image_x << 8) | 0xFF);
    image->Save(filename);
  }

private:
  static constexpr ColorUtil::Gradient RED_GREEN{
    GradRGB(-2.0f, 0xFFFF88),
    GradRGB(-1.0f, 0xFF0000),
    GradRGB( 0.0f, 0x000000),
    GradRGB(+1.0f, 0x00FF00),
    GradRGB(+2.0f, 0x88FFFF),
  };

  const int width = 0;
  const int height = 0;
  const int examples_per_round = 0;
  const std::string filename;
  int image_x = 0;
  std::unique_ptr<ImageRGBA> image;
};

#endif
