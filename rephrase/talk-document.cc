
#include "talk-document.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "bignum/big.h"
#include "fonts/ttf.h"
#include "image-resize.h"
#include "image.h"
#include "stb_truetype.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"
#include "periodically.h"

#include "document.h"

static constexpr bool VERBOSE = false;

static constexpr const char *DEFAULT_FONT_NAME = "FixederSys2x";
static constexpr const char *DEFAULT_FONT_FILE = "fixedersys2x.ttf";

static constexpr int DEFAULT_PIXEL_WIDTH = 1920;
static constexpr int DEFAULT_PIXEL_HEIGHT = 1080;

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


TalkDocument::TalkDocument(std::string_view dir) : Document(dir) {
  InitBuiltInFonts();
}

void TalkDocument::SetDocumentInfoStrings(
    const std::unordered_map<std::string, std::string> &m) {

  // TODO. Most of this stuff would be slide-level metadata,
  // but we have a document title at least?
}

void TalkDocument::SetPageInfo(
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


TalkPage::TalkPage(int pixel_width, int pixel_height,
                   TalkDocument *talk) :
  Page(Document::PixelToPoint(pixel_width),
       Document::PixelToPoint(pixel_height)),
  talk(talk) {
  image = std::make_unique<ImageRGBA>(pixel_width, pixel_height);
  image->Clear32(0xFFFFFFFF);
}

TalkPage::~TalkPage() {
}

void TalkPage::SetDuration(int dur) {
  duration = dur;
}

void TalkPage::SetTargetSec(int s) {
  target_sec = s;
}

void TalkPage::SetSection(const std::string &s) {
  section = s;
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
  if (VERBOSE > 1) {
    printf("Add %dx%d image at %.11g %.11g.\n",
           sticker.Width(), sticker.Height(),
           x, y);
  }

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

void TalkPage::DrawRect(double x, double y, double width, double height,
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

void TalkPage::DrawLine(double x0, double y0, double x1, double y1,
                        double border_width, uint32_t color) {
  // Should we AA it? Perhaps this should be a property.
  image->BlendThickLine32(x0, y0, x1, y1, border_width, color);
}

void TalkPage::DrawVideo(double x, double y,
                         double width, double height,
                         const std::string &src,
                         bool loop) {
  CHECK(!video.has_value()) << "Just one video per slide is supported "
    "in the talk format. It would not be that bad to support more; you "
    "just gotta make it work in talk.html. Had: " << video.value().src <<
    " and tried to add " << src;
  video.emplace(Video{.x = x, .y = y, .width = width, .height = height,
      .src = src, .loop = loop});
}


void TalkDocument::GenerateOutput(
    std::string_view filename_base,
    const std::map<int, std::map<int, DocTree>> &pages) {
  Timer output_timer;

  std::string section = "slide";

  int pixel_width = DEFAULT_PIXEL_WIDTH, pixel_height = DEFAULT_PIXEL_HEIGHT;
  if (width > 0.0 && height > 0.0) {
    pixel_width = (int)std::round(width);
    pixel_height = (int)std::round(height);
  }

  std::string talk_dir = (std::string)filename_base;
  Util::MakeDir(talk_dir);

  std::string talk_filename =
    Util::DirPlus(talk_dir, (std::string)filename_base + ".talk");
  std::string talk =
    "# Generated by BoVeX\n\n";

  int num_pages = 0;
  std::vector<std::vector<std::unique_ptr<TalkPage>>> slides;
  for (const auto &[page_idx, anim_frames] : pages) {
    std::vector<std::unique_ptr<TalkPage>> frames;
    auto pit = durations.find(page_idx);
    for (const auto &[frame_idx, doc] : anim_frames) {

      frames.push_back(std::make_unique<TalkPage>(
                           pixel_width, pixel_height, this));

      TalkPage *page = frames.back().get();
      num_pages++;

      Context context;
      context.font = GetDefaultFont();
      context.color = 0x000000FF;
      Transform identity;
      identity.dx = 0.0;
      identity.dy = 0.0;

      PlaceStickersRec(context, identity, doc, page);

      if (pit != durations.end()) {
        const auto &fdur = pit->second;
        auto fit = fdur.find(frame_idx);
        if (fit != fdur.end()) {
          page->SetDuration(fit->second);
        }
      }

      // XXX this should be slide-level. We just set
      // the target sec for every frame right now.
      if (auto target_it = targets.find(page_idx);
          target_it != targets.end()) {
        page->SetTargetSec(target_it->second);
      }

      // ditto.
      if (auto section_it = sections.find(page_idx);
          section_it != sections.end()) {
        page->SetSection(section_it->second);
      }
    }
    slides.push_back(std::move(frames));
  }

  Periodically status_per(1.0);
  std::mutex m;
  Timer frames_timer;
  int num_done = 0;

  const int total_frames = [&]() {
      int total = 0;
      for (const std::vector<std::unique_ptr<TalkPage>> &page : slides) {
        total += (int)page.size();
      }
      return total;
    }();

  // In case we reencounter a section, keep increasing the count.
  std::unordered_map<std::string, int> section_count;

  {
    Asynchronously async(12);
    for (int i = 0; i < (int)slides.size(); i++) {
      const std::vector<std::unique_ptr<TalkPage>> &page = slides[i];
      StringAppendF(&talk, "slide\n");

      std::optional<TalkPage::Video> video;
      for (const auto &frame : page) {
        if (frame->video.has_value()) {
          CHECK(!video.has_value()) << "Only one frame can have a video.";
          video = frame->video;
        }
      }

      if (video.has_value()) {
        const TalkPage::Video &v = video.value();
        StringAppendF(&talk,
                      "  video %d %d %d %d %s\n",
                      (int)std::round(v.x),
                      (int)std::round(v.y),
                      (int)std::round(v.width),
                      (int)std::round(v.height),
                      v.src.c_str());
        if (v.loop) {
          StringAppendF(&talk, "  loop-video\n");
        }
      }

      for (const auto &frame : page) {
        if (frame->target_sec > 0) {
          StringAppendF(&talk,
                        "  target %d\n",
                        frame->target_sec);
          // Just the first one we see.
          break;
        }
      }

      for (const auto &frame : page) {
        if (!frame->section.empty()) {
          section = frame->section;
        }
      }

      for (int f = 0; f < (int)page.size(); f++) {
        const auto &frame = page[f];

        if (frame->duration != 0) {
          StringAppendF(&talk,
                        "  dur %d\n",
                        frame->duration);
        }

        const std::string framefile =
          page.size() == 1 ?
          StringPrintf("%s-%d.png", section.c_str(), i) :
          StringPrintf("%s-%d_%d.png", section.c_str(), i, f);
        StringAppendF(&talk,
                      "  %s\n",
                      framefile.c_str());

        async.Run([&m, &num_done, &status_per, &talk_dir,
                   &total_frames, &frames_timer,
                   f = frame.get(), framefile]() {
            const std::string frameabsfile =
              Util::DirPlus(talk_dir, framefile);
            f->image->Save(frameabsfile);
            {
              MutexLock ml(&m);
              num_done++;

              if (status_per.ShouldRun()) {
                if (status_per.TimesRun() == 1) {
                  // The very first time, make space for the
                  // progress bar.
                  printf("\n");
                }

                printf(ANSI_UP "%s\n",
                       ANSI::ProgressBar(num_done, total_frames,
                                         "Write frames",
                                         frames_timer.Seconds()).c_str());
              }
            }
          });
      }
    }

    if (status_per.TimesRun() > 0) {
      // Remove status bar.
      printf(ANSI_UP);
    }

    Util::WriteFile(talk_filename, talk);
    // printf("Wait async...\n");
  }

  double sec = output_timer.Seconds();
  printf("Wrote %d frame%s to " AGREEN("%s") " in %s.\n",
         num_pages, num_pages != 1 ? "s" : "",
         talk_filename.c_str(),
         ANSI::Time(sec).c_str());
}
