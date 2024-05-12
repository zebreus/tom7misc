#ifndef _REPHRASE_TALK_DOCUMENT_H
#define _REPHRASE_TALK_DOCUMENT_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "image.h"
#include "document.h"
#include "fonts/ttf.h"

struct TalkPage;
struct TalkDocument;

struct TalkFont : public Font {
  // Invalid state.
  TalkFont() = default;

  explicit TalkFont(const std::string &name,
                    const std::string &filename);
  std::string Name() const override;

  std::optional<double>
  GetKerning(int codepoint1, int codepoint2) const override;

  double CharWidth(int codepoint) const override;

 private:
  TalkFont(const TalkFont &other) = delete;
  TalkFont &operator =(const TalkFont &other) = delete;

  friend struct TalkDocument;
  friend struct TalkPage;
  std::string name;
  std::unique_ptr<TTF> ttf;
};

// TODO: This should probably be TalkFrame and we should have
// a notion of Slide separately. There are many places where
// we want to store something with the frame.
struct TalkPage : public Page {
  TalkPage(int pixel_width, int pixel_height,
           TalkDocument *talk);
  ~TalkPage() override;

  void DrawText(const Font *font,
                const std::string &text, double size,
                double x, double y,
                uint32_t color) override;

  void DrawImage(double x, double y,
                 double width, double height,
                 const ImageRGBA &img) override;


  void DrawRect(double x, double y, double width, double height,
                double border_width, uint32_t color_fill,
                uint32_t color_border) override;

  void DrawVideo(double x, double y,
                 double width, double height,
                 const std::string &src,
                 bool loop) override;

  void DrawLine(double x0, double y0, double x1, double y1,
                double stroke_width, uint32_t color) override;

  void SetDuration(int dur);
  void SetTargetSec(int sec);

 private:
  friend struct TalkDocument;
  TalkPage(const TalkPage &other) = delete;
  TalkPage &operator =(const TalkPage &other) = delete;

  const TalkDocument *talk = nullptr;

  int duration = 0;
  int target_sec = 0;
  std::unique_ptr<ImageRGBA> image;

  struct Video {
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    std::string src;
    bool loop = false;
  };

  std::optional<Video> video;
};

struct TalkDocument : public Document {
  TalkDocument(std::string_view program_dir);

  std::string LoadFontFile(const std::string &filename) override;

  void SetDocumentInfoStrings(
      const std::unordered_map<std::string, std::string> &info) override;

  const Font *GetDefaultFont() override;

  void GenerateOutput(
      std::string_view dirname,
      const std::map<int, std::map<int, DocTree>> &pages) override;

  void SetPageInfo(
      int page_idx, int frame_idx,
      const std::unordered_map<std::string, AttrVal> &attrs) override;

 private:
  void InitBuiltInFonts();
  int next_font_id = 0;
  // XXX make this more general
  std::map<int, std::map<int, int>> durations;
  // Just slide index.
  std::map<int, int> targets;
};

#endif
