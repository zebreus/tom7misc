
#include "ico.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "image.h"
#include "packet-parser.h"
#include "packet-writer.h"
#include "png.h"
#include "util.h"

static constexpr uint16_t TYPE_ICO = 1;
static constexpr uint16_t TYPE_CUR = 2;

// Parses both cursor and icon files. For icon files, it sets the
// x,y of the hotspot to -1.
static std::vector<ICO::Cursor>
InternalParseICO(std::span<const uint8_t> bytes) {
  PacketParser p(bytes);

  uint16_t reserved = p.W16LE();
  uint16_t type = p.W16LE();
  uint16_t count = p.W16LE();

  if (!p.OK() || reserved != 0) return {};
  // Type 1 = ICO, type 2 = CUR.
  if (type != TYPE_ICO && type != TYPE_CUR) return {};

  std::vector<ICO::Cursor> result;
  result.reserve(count);
  for (int i = 0; i < count; i++) {
    // Each directory entry is 16 bytes.
    PacketParser entry = p.Subpacket(16);
    if (!entry.OK()) break;

    // Skip width, height, colors, reserved.
    entry.Skip(4);

    int cursor_x = -1;
    int cursor_y = -1;
    if (type == TYPE_CUR) {
      cursor_x = entry.W16LE();
      cursor_y = entry.W16LE();
    } else {
      // Skip planes, bits per pixel.
      entry.Skip(4);
    }

    uint32_t bytes_in_res = entry.W32LE();
    uint32_t image_offset = entry.W32LE();

    if (!entry.OK()) continue;

    // Use original, unmodified bytes since offsets are absolute.
    PacketParser img_data(bytes);
    img_data.Skip(image_offset);
    img_data = img_data.Subpacket(bytes_in_res);

    if (!img_data.OK()) continue;

    static constexpr uint8_t png_magic[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (img_data.HasPrefix(png_magic)) {
      std::unique_ptr<ImageRGBA> img(
          ImageRGBA::LoadFromMemory(img_data.View()));
      if (img) {
        result.emplace_back(ICO::Cursor{
            .img = std::move(*img),
            .x = cursor_x,
            .y = cursor_y});
      }
      continue;
    }

    PacketParser header = img_data;
    uint32_t header_size = header.W32LE();

    if (header_size == 40 || header_size == 108 || header_size == 124) {
      int32_t width = header.W32LE();
      int32_t height = header.W32LE();
      bool top_down = false;
      if (height < 0) {
        top_down = true;
        height = -height;
      }

      // Skip planes.
      header.Skip(2);
      uint16_t bpp = header.W16LE();
      uint32_t compression = header.W32LE();

      if (!header.OK()) return {};

      // Skip compressed images, since they are valid.
      if (compression != 0) continue;

      int real_height = height / 2;
      if (width <= 0 || width > 8192 || real_height <= 0 ||
          real_height > 8192) {
        return {};
      }

      // Skip SizeImage, XPelsPerMeter, YPelsPerMeter.
      header.Skip(12);
      uint32_t colors_used = header.W32LE();

      int palette_colors = 0;
      if (bpp <= 8) {
        palette_colors = colors_used == 0 ? (1 << bpp) : colors_used;
      }

      int row_bytes = ((width * bpp + 31) / 32) * 4;

      PacketParser data_p = img_data;
      data_p.Skip(header_size);
      PacketParser palette = data_p.Subpacket(palette_colors * 4);
      PacketParser xor_mask = data_p.Subpacket(row_bytes * real_height);

      if (!data_p.OK()) return {};

      int and_row_bytes = ((width * 1 + 31) / 32) * 4;
      PacketParser and_mask = data_p;
      PacketParser and_mask_pass1 = and_mask;

      ImageRGBA out(width, real_height);

      for (int y = 0; y < real_height; y++) {
        int out_y = top_down ? y : (real_height - 1 - y);

        PacketParser row = xor_mask.Subpacket(row_bytes);
        PacketParser arow = and_mask_pass1.Subpacket(and_row_bytes);

        for (int x = 0; x < width; x++) {
          uint8_t r = 0, g = 0, b = 0, a = 255;
          if (bpp == 32) {
            b = row[x * 4 + 0];
            g = row[x * 4 + 1];
            r = row[x * 4 + 2];
            a = row[x * 4 + 3];

          } else if (bpp == 24) {
            b = row[x * 3 + 0];
            g = row[x * 3 + 1];
            r = row[x * 3 + 2];

          } else if (bpp == 8) {
            uint8_t idx = row[x];
            if (idx < palette_colors) {
              b = palette[idx * 4 + 0];
              g = palette[idx * 4 + 1];
              r = palette[idx * 4 + 2];
            }

          } else if (bpp == 4) {
            uint8_t idx = (row[x >> 1] >> ((1 - (x % 2)) * 4)) & 0xF;
            if (idx < palette_colors) {
              b = palette[idx * 4 + 0];
              g = palette[idx * 4 + 1];
              r = palette[idx * 4 + 2];
            }

          } else if (bpp == 1) {
            uint8_t idx = (row[x >> 3] >> (7 - (x % 8))) & 1;
            if (idx < palette_colors) {
              b = palette[idx * 4 + 0];
              g = palette[idx * 4 + 1];
              r = palette[idx * 4 + 2];
            }
          }

          if (bpp != 32) {
            bool mask = (arow[x >> 3] >> (7 - (x % 8))) & 1;
            if (mask) {
              a = 0;
            }
          }

          out.SetPixel(x, out_y, r, g, b, a);
        }

        if (!row.OK()) return {};
        if (!arow.OK()) return {};
      }

      // Assume that if the alpha channel is completely zero,
      // the file actually intended a 32-bpp BGR0 format.
      if (bpp == 32) {
        bool nontrivial_alpha = false;
        for (int y = 0; y < real_height; y++) {
          for (int x = 0; x < width; x++) {
            if ((out.GetPixel32(x, y) & 0xFF) != 0) {
              nontrivial_alpha = true;
              break;
            }
          }
          if (nontrivial_alpha) {
            break;
          }
        }

        if (!nontrivial_alpha) {
          PacketParser and_mask_pass2 = and_mask;
          for (int y = 0; y < real_height; y++) {
            int mask_y = top_down ? y : (real_height - 1 - y);
            PacketParser arow = and_mask_pass2.Subpacket(and_row_bytes);
            for (int x = 0; x < width; x++) {
              uint32_t color = out.GetPixel32(x, mask_y);
              color |= 0x000000FF;
              if (arow.OK()) {
                bool mask = (arow[x >> 3] >> (7 - (x & 7))) & 1;
                if (mask) {
                  color &= 0xFFFFFF00;
                }
              }
              out.SetPixel32(x, mask_y, color);
            }
          }
        }
      }

      result.emplace_back(ICO::Cursor{
          .img = std::move(out),
          .x = cursor_x,
          .y = cursor_y,
        });
    }
  }

  return result;
}

std::vector<ImageRGBA> ICO::ParseICO(std::span<const uint8_t> bytes) {
  std::vector<ImageRGBA> result;
  // Ignore hotspots.
  for (ICO::Cursor &cur : InternalParseICO(bytes)) {
    result.emplace_back(std::move(cur.img));
  }
  return result;
}


std::vector<ImageRGBA> ICO::LoadICO(std::string_view f) {
  std::string contents = Util::ReadFile(f);
  return ParseICO(std::span<const uint8_t>(
                      (const uint8_t *)contents.data(), contents.size()));
}

std::vector<ICO::Cursor> ICO::ParseCUR(std::span<const uint8_t> bytes) {
  std::vector<ICO::Cursor> result;
  for (ICO::Cursor &cur : InternalParseICO(bytes)) {
    // InternalParseICO sets x,y to -1 for images without hotspots.
    if (cur.x >= 0 && cur.y >= 0) {
      result.emplace_back(std::move(cur));
    }
  }
  return result;
}

std::vector<ICO::Cursor> ICO::LoadCUR(std::string_view f) {
  std::string contents = Util::ReadFile(f);
  return ParseCUR(std::span<const uint8_t>(
                      (const uint8_t *)contents.data(), contents.size()));
}

// Encode the images either as ICO or CUR format, depending on whether
// the hotspots are provided.
//
// PERF: We could pass pointers to the images here, but these should
// generally be tiny anyway.
static std::vector<uint8_t> InternalEncode(
    std::span<const ImageRGBA> images,
    std::optional<std::span<std::pair<int, int>>> hotspots) {
  if (hotspots.has_value()) {
    CHECK(images.size() == hotspots.value().size());
  }

  if (images.size() > 65535) return {};

  PacketWriter buf;

  // Header
  const uint16_t type = hotspots.has_value() ? TYPE_CUR : TYPE_ICO;
  buf.W16LE(0);
  buf.W16LE(type);
  buf.W16LE(images.size());

  CHECK(buf.size() == 6);

  std::vector<std::vector<uint8_t>> pngs;
  pngs.reserve(images.size());
  for (const auto& img : images) {
    pngs.push_back(PNG::EncodeInMemory(img, 9));
  }

  uint32_t payload_start = buf.size() + 16 * images.size();
  for (size_t i = 0; i < images.size(); i++) {
    const auto &img = images[i];
    const auto &png = pngs[i];

    int w = img.Width();
    int h = img.Height();
    CHECK(w <= 256 && h <= 256) << "This file format can only store "
      "dimensions up to 256 pixels!";

    // Width and height; 0 means 256.
    buf.W8(w >= 256 ? 0 : w);
    buf.W8(h >= 256 ? 0 : h);
    // Color count 0 is used for RGBA.
    buf.W8(0);
    // Reserved
    buf.W8(0);

    if (hotspots.has_value()) {
      const auto &[x, y] = hotspots.value()[i];
      buf.W16LE(x);
      buf.W16LE(y);
    } else {
      // planes, bpp
      buf.W16LE(1);
      buf.W16LE(32);
    }

    buf.W32LE(png.size());
    buf.W32LE(payload_start);

    payload_start += png.size();
  }

  for (const auto& png : pngs) {
    buf.Bytes(png);
  }

  return std::move(buf).Release();
}

std::vector<uint8_t> ICO::EncodeICO(std::span<const ImageRGBA> images) {
  return InternalEncode(images, {});
}

bool ICO::SaveICO(std::string_view filename,
                  std::span<const ImageRGBA> images) {
  std::vector<uint8_t> data = EncodeICO(images);
  if (data.empty() && !images.empty()) return false;
  std::string contents((const char*)data.data(), data.size());
  return Util::WriteFile(filename, contents);
}


std::vector<uint8_t> ICO::EncodeCUR(std::span<const ICO::Cursor> images) {
  std::vector<ImageRGBA> imgs;
  std::vector<std::pair<int, int>> hotspots;
  imgs.reserve(images.size());
  hotspots.reserve(images.size());
  for (const auto &cur : images) {
    imgs.push_back(cur.img);
    hotspots.emplace_back(cur.x, cur.y);
  }
  return InternalEncode(imgs, hotspots);
}

bool ICO::SaveCUR(std::string_view filename,
                  std::span<const ICO::Cursor> images) {
  std::vector<uint8_t> data = EncodeCUR(images);
  if (data.empty() && !images.empty()) return false;
  std::string contents((const char*)data.data(), data.size());
  return Util::WriteFile(filename, contents);
}

