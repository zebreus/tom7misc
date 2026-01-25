
#include "image-document.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big.h"
#include "document.h"
#include "fonts/ttf.h"
#include "image-resize.h"
#include "image.h"
#include "stb_truetype.h"
#include "svg.h"
#include "util.h"

static constexpr bool VERBOSE = false;

static constexpr const char *DEFAULT_FONT_NAME = "FixederSys2x";
static constexpr const char *DEFAULT_FONT_FILE = "fixedersys2x.ttf";

using Transform = Document::Transform;

ImageFont::ImageFont(std::string_view name,
                     std::string_view filename) : name(name) {
  Print("Trying to load " AWHITE("{}") "\n", filename);
  ttf.reset(new TTF(filename));
  if (VERBOSE) {
    Print("** {} **\n", filename);
    stbtt__print_tables(ttf->FontInfo());
  }
}

const Font *ImageDocument::GetDefaultFont() {
  // XXX Should be an embedded font or something (a TTF object with
  // no glyphs) that doesn't depend on external files?
  FontDescription desc;
  desc.font_family = DEFAULT_FONT_NAME;
  return GetDescribedFont(desc);
}

std::string ImageDocument::LoadFontFile(std::string_view filename) {
  std::string name = std::format("font{}", next_font_id);
  next_font_id++;
  fonts[name] = std::make_unique<ImageFont>(name, filename);
  Print("Loaded " ACYAN("{}") " as " APURPLE("{}") "\n",
        filename, name);
  return name;
}

void ImageDocument::InitBuiltInFonts() {
  FontDescription desc;
  desc.font_family = DEFAULT_FONT_NAME;
  RegisterFont(desc, new ImageFont(desc.font_family,
                                   Util::DirPlus(program_dir,
                                                 DEFAULT_FONT_FILE)));
}

std::string ImageFont::Name() const {
  return name;
}

std::optional<double>
ImageFont::GetKerning(int codepoint1, int codepoint2) const {
  // PERF: This has to search some tables. Better to load this once
  // or cache it?
  const int k =
    stbtt_GetCodepointKernAdvance(ttf->FontInfo(), codepoint1, codepoint2);
  // Print("Kern for {} to {} is {}\n", codepoint1, codepoint2, k);
  if (k == 0) return std::nullopt;
  const double scale = stbtt_ScaleForPixelHeight(ttf->FontInfo(), 1.0);
  return {scale * k};
}

double ImageFont::CharWidth(int codepoint) const {
  const double scale = stbtt_ScaleForPixelHeight(ttf->FontInfo(), 1.0);
  int advance_width = 0, left_side_bearing = 0;
  stbtt_GetCodepointHMetrics(ttf->FontInfo(), codepoint,
                             &advance_width,
                             &left_side_bearing);
  return advance_width * scale;
}

ImageImage::ImageImage(std::string_view name,
                       std::unique_ptr<ImageRGBA> img) :
  name(name), img(std::move(img)) {}

std::string ImageImage::Name() const {
  return name;
}

double ImageImage::Width() const {
  CHECK(img.get() != nullptr);
  return img->Width();
}
double ImageImage::Height() const {
  CHECK(img.get() != nullptr);
  return img->Height();
}

bool ImageImage::IsRaster() const {
  CHECK(img.get() != nullptr);
  return true;
}

ImageRGBA ImageImage::GetRaster() const {
  return *img;
}

std::string ImageDocument::AddImage(std::unique_ptr<ImageRGBA> img) {
  CHECK(img.get() != nullptr) << "Cannot add null image.";

  std::string handle = NextImageHandle();
  AddImageWithHandle(handle,
                     std::make_unique<ImageImage>(handle, std::move(img)));
  return handle;
}

std::string ImageDocument::AddImage(std::unique_ptr<SVG::Doc> img) {
  LOG(FATAL) << "Sorry, SVG files are not (yet?) supported by the "
    "image family of output document types.";
}

ImagePage::ImagePage(int pixel_width, int pixel_height,
                     ImageDocument *doc) :
  Page(Document::PixelToPoint(pixel_width),
       Document::PixelToPoint(pixel_height)),
  doc(doc) {
  image = std::make_unique<ImageRGBA>(pixel_width, pixel_height);
  image->Clear32(0xFFFFFFFF);
}

ImagePage::~ImagePage() {
}

void ImagePage::SetDuration(int dur) {
  duration = dur;
}

void ImagePage::SetTargetSec(int s) {
  target_sec = s;
}

void ImagePage::SetSection(std::string_view s) {
  section = s;
}

void ImagePage::DrawText(const Font *font_in,
                         std::string_view text, double size,
                         double x, double y,
                         uint32_t color) {
  const ImageFont *font = (const ImageFont*)font_in;

  CHECK(image.get() != nullptr);
  if (text.empty()) return;

  CHECK(font->ttf.get() != nullptr);

  uint32_t rgb0 = color & 0xFFFFFF00;
  uint32_t a = color & 0xFF;
  font->ttf->BlitStringFloat(
      x, y,
      size,
      text,
      [rgb0, a, img = image.get()](int xx, int yy, uint8_t aa) {
        uint32_t alpha = std::clamp((aa * a) / 255, 0u, 255u);
        img->BlendPixel32(xx, yy, rgb0 | alpha);
      },
      // No kerning: BoVeX does this itself.
      false);
}

void ImagePage::DrawImage(const Image *doc_image,
                          double x, double y,
                          double width, double height) {
  // XXX should check type id or something?
  const ImageImage *image_image = (const ImageImage *)doc_image;
  CHECK(image_image->img.get() != nullptr);
  const ImageRGBA &img = *image_image->img;

  if (VERBOSE > 1) {
    Print("Add {}x{} image at {:.11g} {:.11g}.\n",
          img.Width(), img.Height(),
          x, y);
  }

  const int ww = std::round(width);
  const int hh = std::round(height);

  if (ww != img.Width() || hh != img.Height()) {
    // TODO: Sub-pixel resize and positioning.
    ImageRGBA resized = ImageResize::Resize(img, ww, hh);
    image->BlendImage((int)std::round(x), (int)std::round(y), resized);
  } else {
    image->BlendImage((int)std::round(x), (int)std::round(y), img);
  }
}

void ImagePage::DrawRect(double x, double y, double width, double height,
                        double border_width, uint32_t color_fill,
                        uint32_t color_border) {
  double x2 = x + width;
  double y2 = y + height;
  int ix = (int)std::round(x);
  int iy = (int)std::round(y);
  int iw = (int)std::round(x2 - ix);
  int ih = (int)std::round(y2 - iy);

  int ib = (int)std::round(border_width);

  if ((color_fill & 0xFF) > 0) {
    image->BlendRect32(ix, iy, iw, ih, color_fill);
  }

  if (ib > 0 && (color_border & 0xFF) > 0) {
    image->BlendBox32(ix, iy, iw, ih, color_border, std::nullopt);
    for (int i = 0; i < (ib >> 1); i++) {
      image->BlendBox32(ix - i, iy - i, iw + 2 * i, ih + 2 * i, color_border,
                        std::nullopt);
      image->BlendBox32(ix + i, iy + i, iw - 2 * i, ih - 2 * i, color_border,
                        std::nullopt);
    }
  }
}

void ImagePage::DrawLine(double x0, double y0, double x1, double y1,
                        double border_width, uint32_t color) {
  // Should we AA it? Perhaps this should be a property.
  image->BlendThickLine32(x0, y0, x1, y1, border_width, color);
}

void ImagePage::DrawVideo(double x, double y,
                          double width, double height,
                          std::string_view src,
                          bool loop) {
  CHECK(!video.has_value()) << "Just one video per slide is supported "
    "in the talk format. It would not be that bad to support more; you "
    "just gotta make it work in talk.html. Had: " << video.value().src <<
    " and tried to add " << src;
  video.emplace(Video{.x = x, .y = y, .width = width, .height = height,
      .src = std::string(src), .loop = loop});
}

void ImageDocument::GenerateOutput(
    std::string_view dirname,
    const std::map<int, std::map<int, DocTree>> &pages) {
  LOG(FATAL) << "Maybe it will be good to consolidate some "
    "of the shared code here, but right now the leaf class "
    "has to do this.";
}

ImageDocument::ImageDocument(std::string_view prog_dir) :
    Document(prog_dir) {
  InitBuiltInFonts();
}

void ImageDocument::SetDocumentInfoStrings(
    const std::unordered_map<std::string, std::string> &m) {

  // TODO. Most of this stuff would be slide-level metadata,
  // but we have a document title at least?
}

void ImageDocument::SetPageInfo(
    int page_idx, int frame_idx,
    const std::unordered_map<std::string, AttrVal> &attrs) {

  {
    const auto it = attrs.find("duration");
    if (it != attrs.end()) {
      const BigInt *bi = std::get_if<BigInt>(&it->second.v);
      CHECK(bi != nullptr && bi->ToInt().has_value()) << "Expected "
        "a reasonable-size integer for frame duration";
      durations[page_idx][frame_idx] = bi->ToInt().value();
    }
  }

  {
    const auto it = attrs.find("target");
    if (it != attrs.end()) {
      const BigInt *bi = std::get_if<BigInt>(&it->second.v);
      CHECK(bi != nullptr && bi->ToInt().has_value()) << "Expected "
        "a reasonable-size integer for target seconds";
      targets[page_idx] = bi->ToInt().value();
    }
  }

  {
    const auto it = attrs.find("section");
    if (it != attrs.end()) {
      const std::string *s = std::get_if<std::string>(&it->second.v);
      CHECK(Util::MatchSpec("-a-z", *s)) << "PageInfo's section must "
        "be strictly lowercase letters and hyphen. This is so that "
        "it can be used in filenames that also use numbers, without "
        "ambiguity. Got: " << *s;
      sections[page_idx] = *s;
    }
  }
}
