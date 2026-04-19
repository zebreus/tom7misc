// An Image Document is a series of images. It's used for
// "talk" output and "movie" output.

#ifndef _REPHRASE_IMAGE_DOCUMENT_H
#define _REPHRASE_IMAGE_DOCUMENT_H

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "document.h"
#include "fonts/ttf.h"
#include "image.h"
#include "svg.h"

struct ImagePage;
struct ImageDocument;
struct TalkDocument;

struct ImageFont : public Font {
  // Invalid state.
  ImageFont() = default;

  explicit ImageFont(std::string_view name,
                     std::string_view filename);
  std::string Name() const override;

  std::optional<double>
  GetKerning(int codepoint1, int codepoint2) const override;

  double CharWidth(int codepoint) const override;

 private:
  ImageFont(const ImageFont &other) = delete;
  ImageFont &operator =(const ImageFont &other) = delete;

  friend struct ImageDocument;
  friend struct ImagePage;
  std::string name;
  std::unique_ptr<TTF> ttf;
};

struct ImageImage : public Image {
  // Invalid state.
  ImageImage() = default;

  ImageImage(std::string_view name, std::unique_ptr<ImageRGBA> img);
  std::string Name() const override;

  double Width() const override;
  double Height() const override;

  bool IsRaster() const override;
  ImageRGBA GetRaster() const override;

 private:
  ImageImage(const ImageImage &other) = delete;
  ImageImage &operator =(const ImageImage &other) = delete;

  friend struct ImageDocument;
  friend struct ImagePage;
  std::string name;
  // SVG not supported (yet?)
  std::unique_ptr<ImageRGBA> img;
};

// TODO: This should probably be ImageFrame and we should have
// a notion of Slide separately. There are many places where
// we want to store something with the frame.
struct ImagePage : public Page {
  ImagePage(int pixel_width, int pixel_height,
           ImageDocument *image);
  ~ImagePage() override;

  void DrawText(const Font *font,
                std::string_view text, double size,
                double x, double y,
                uint32_t color) override;

  void DrawImage(const Image * img,
                 double x, double y,
                 double width, double height) override;

  void DrawRect(double x, double y, double width, double height,
                double border_width, uint32_t color_fill,
                uint32_t color_border) override;

  void DrawVideo(double x, double y,
                 double width, double height,
                 std::string_view src,
                 bool loop,
                 bool audio) override;

  void DrawLine(double x0, double y0, double x1, double y1,
                double stroke_width, uint32_t color) override;

  void SetDuration(int dur);
  void SetTargetSec(int sec);
  void SetSection(std::string_view s);

 private:
  friend struct ImageDocument;
  friend struct TalkDocument;
  friend struct MovieDocument;
  ImagePage(const ImagePage &other) = delete;
  ImagePage &operator =(const ImagePage &other) = delete;

  const ImageDocument *doc = nullptr;

  int duration = 0;
  std::optional<int> target_sec;
  std::string section;
  std::unique_ptr<ImageRGBA> image;

  struct Video {
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    std::string src;
    bool loop = false;
    bool audio = false;
  };

  std::optional<Video> video;
};

struct ImageDocument : public Document {
  ImageDocument(std::string_view program_dir);

  std::string LoadFontFile(std::string_view filename) override;

  std::string AddImage(std::unique_ptr<ImageRGBA> img) override;
  std::string AddImage(std::unique_ptr<SVG::Doc> img) override;

  void SetDocumentInfoStrings(
      const std::unordered_map<std::string, std::string> &info) override;

  const Font *GetDefaultFont() override;

  void GenerateOutput(
      std::string_view dirname,
      const std::map<int, std::map<int, DocTree>> &pages) override;

  void SetPageInfo(
      int page_idx, int frame_idx,
      const std::unordered_map<std::string, AttrVal> &attrs) override;

 protected:
  void InitBuiltInFonts();
  int next_font_id = 0;
  // XXX make this more general
  std::map<int, std::map<int, int>> durations;
  // Just slide index.
  std::map<int, int> targets;
  std::map<int, std::string> sections;
};

#endif
