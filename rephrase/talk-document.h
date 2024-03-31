#ifndef _REPHRASE_TALK_DOCUMENT_H
#define _REPHRASE_TALK_DOCUMENT_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <optional>
#include <string_view>
#include <unordered_map>

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

struct TalkPage : public Page {
  TalkPage(int pixel_width, int pixel_height,
           TalkDocument *talk) : Page(Document::PixelToPoint(pixel_width),
                                      Document::PixelToPoint(pixel_height)),
                                 talk(talk) {
    image = std::make_unique<ImageRGBA>(pixel_width, pixel_height);
  }

  void DrawText(const Font *font,
                const std::string &text, double size,
                double x, double y,
                uint32_t color) override;

  void DrawImage(double x, double y,
                 double width, double height,
                 const ImageRGBA &img) override;

 private:
  friend struct TalkDocument;
  TalkPage(const TalkPage &other) = delete;
  TalkPage &operator =(const TalkPage &other) = delete;

  // TODO: Perhaps a "page" could be an animation, in which case there
  // would be multiple images or some meta representation.
  const TalkDocument *talk = nullptr;
  std::unique_ptr<ImageRGBA> image;
};

struct TalkDocument : public Document {
  TalkDocument(int pixel_width, int pixel_height);

  std::string LoadFontFile(const std::string &filename) override;

  void SetDocumentInfoStrings(
      const std::unordered_map<std::string, std::string> &info) override;

  const Font *GetDefaultFont() override;

  void GenerateOutput(std::string_view filename,
                      const std::map<int, DocTree> &pages) override;

 private:
  void InitBuiltInFonts();
  int next_font_id = 0;
  int pixel_width = 0, pixel_height = 0;
  // TODO: talk metadata
  std::map<int, std::unique_ptr<TalkPage>> pages;
};

#endif
