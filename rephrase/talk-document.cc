
#include "talk-document.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "util.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "fonts/ttf.h"
#include "stb_truetype.h"
#include "image-resize.h"
#include "threadutil.h"
#include "timer.h"

#include "document.h"

static constexpr bool VERBOSE = false;

static constexpr const char *DEFAULT_FONT_NAME = "FixederSys2x";
static constexpr const char *DEFAULT_FONT_FILE = "fixedersys2x.ttf";

using Transform = Document::Transform;

TalkFont::TalkFont(const std::string &name,
                   const std::string &filename) : name(name) {
  ttf.reset(new TTF(filename));
  if (VERBOSE) {
    printf("** %s **\n", filename.c_str());
    stbtt__print_tables(ttf->FontInfo());
  }
}

const Font *TalkDocument::GetDefaultFont() {
  // XXX Should be an embedded font or something (a TTF object with
  // no glyphs) that doesn't depend on external files?
  FontDescription desc;
  desc.font_family = DEFAULT_FONT_NAME;
  return GetDescribedFont(desc);
}

std::string TalkDocument::LoadFontFile(const std::string &filename) {
  std::string name = StringPrintf("font%d", next_font_id);
  next_font_id++;
  fonts[name] = std::make_unique<TalkFont>(name, filename);
  printf("Loaded " ACYAN("%s") " as " APURPLE("%s") "\n",
         filename.c_str(), name.c_str());
  return name;
}

void TalkDocument::InitBuiltInFonts() {
  FontDescription desc;
  desc.font_family = DEFAULT_FONT_NAME;
  RegisterFont(desc, new TalkFont(desc.font_family,
                                  DEFAULT_FONT_FILE));
}

std::string TalkFont::Name() const {
  return name;
}

std::optional<double>
TalkFont::GetKerning(int codepoint1, int codepoint2) const {
  // PERF: This has to search some tables. Better to load this once
  // or cache it?
  const int k =
    stbtt_GetCodepointKernAdvance(ttf->FontInfo(), codepoint1, codepoint2);
  // printf("Kern for %d to %d is %d\n", codepoint1, codepoint2, k);
  if (k == 0) return std::nullopt;
  const double scale = stbtt_ScaleForPixelHeight(ttf->FontInfo(), 1.0);
  return {scale * k};
}

double TalkFont::CharWidth(int codepoint) const {
  const double scale = stbtt_ScaleForPixelHeight(ttf->FontInfo(), 1.0);
  int advance_width = 0, left_side_bearing = 0;
  stbtt_GetCodepointHMetrics(ttf->FontInfo(), codepoint,
                             &advance_width,
                             &left_side_bearing);
  return advance_width * scale;
}


TalkDocument::TalkDocument(int pixel_width, int pixel_height) :
  pixel_width(pixel_width), pixel_height(pixel_height) {
  InitBuiltInFonts();
}

void TalkDocument::SetDocumentInfoStrings(
    const std::unordered_map<std::string, std::string> &m) {

  // TODO. Most of this stuff would be slide-level metadata,
  // but we have a document title at least?
}

TalkPage::TalkPage(int pixel_width, int pixel_height,
                   TalkDocument *talk) :
  Page(Document::PixelToPoint(pixel_width),
       Document::PixelToPoint(pixel_height)),
  talk(talk) {
  image = std::make_unique<ImageRGBA>(pixel_width, pixel_height);
  image->Clear32(0xFFFFFFFF);
}


void TalkPage::DrawText(const Font *font_in,
                        const std::string &text, double size,
                        double x, double y,
                        uint32_t color) {
  const TalkFont *font = (const TalkFont*)font_in;

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

void TalkPage::DrawImage(double x, double y,
                         double width, double height,
                         const ImageRGBA &sticker) {
  CHECK(image.get() != nullptr);
  printf("Add image at %.11g %.11g.\n", x, y);

  const int ww = std::round(width);
  const int hh = std::round(height);

  if (ww != sticker.Width() || hh != sticker.Height()) {
    // TODO: Sub-pixel resize and positioning.
    ImageRGBA resized = ImageResize::Resize(sticker, ww, hh);
    image->BlendImage((int)std::round(x), (int)std::round(y), resized);
  } else {
    image->BlendImage((int)std::round(x), (int)std::round(y), sticker);
  }
}

void TalkDocument::GenerateOutput(std::string_view filename_base,
                                 const std::map<int, DocTree> &pages) {
  Timer output_timer;

  std::string talk_dir = (std::string)filename_base;
  Util::MakeDir(talk_dir);

  std::string talk_filename =
    Util::dirplus(talk_dir, (std::string)filename_base + ".talk");
  std::string talk =
    "# Generated by BoVeX\n\n";

  int num_pages = 0;
  std::vector<std::unique_ptr<TalkPage>> pageptrs;
  for (const auto &[page_idx, doc] : pages) {
    pageptrs.push_back(std::make_unique<TalkPage>(
                           pixel_width, pixel_height, this));
    TalkPage *page = pageptrs.back().get();
    num_pages++;

    Context context;
    context.font = GetDefaultFont();
    context.color = 0x000000FF;
    Transform identity;
    identity.dx = 0.0;
    identity.dy = 0.0;

    PlaceStickersRec(context, identity, doc, page);
  }

  Asynchronously async(8);
  for (int i = 0; i < (int)pageptrs.size(); i++) {
    const auto &page = pageptrs[i];
    const std::string slidefile = StringPrintf("slide%d.png", i);
    StringAppendF(&talk,
                  "slide\n"
                  "  %s\n",
                  slidefile.c_str());

    async.Run([&talk_dir, &page, slidefile]() {
        const std::string slideabsfile = Util::dirplus(talk_dir, slidefile);
        page->image->Save(slideabsfile);
      });
  }

  Util::WriteFile(talk_filename, talk);

  double sec = output_timer.Seconds();
  printf("Wrote %d slide%s to " AGREEN("%s") " in %s.\n",
         num_pages, num_pages != 1 ? "s" : "",
         talk_filename.c_str(),
         ANSI::Time(sec).c_str());
}
