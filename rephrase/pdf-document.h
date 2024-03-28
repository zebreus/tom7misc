#ifndef _REPHRASE_PDF_DOCUMENT_H
#define _REPHRASE_PDF_DOCUMENT_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <optional>
#include <string_view>
#include <unordered_map>

#include "image.h"
#include "document.h"
#include "pdf.h"

struct PDFFont : public Font {
  // Invalid state.
  PDFFont() = default;
  // It's just a pointer. Copy freely.
  PDFFont(const PDFFont &other) = default;
  PDFFont &operator =(const PDFFont &other) = default;

  explicit PDFFont(const PDF::FontObj *f);
  std::string Name() const override;

  std::optional<double>
  GetKerning(int codepoint1, int codepoint2) const override;

  double CharWidth(int codepoint) const override;
  double GetKernedWidth(const std::string &text) const override;

 private:
  friend struct PDFDocument;
  // Not owned.
  const PDF::FontObj *pdf_font = nullptr;
};

struct PDFDocument : public Document {
  PDFDocument(double width, double height);

  std::string LoadFontFile(const std::string &filename) override;

  void SetDocumentInfoStrings(
      const std::unordered_map<std::string, std::string> &info) override;

  const Font *GetBuiltInFont(PDF::BuiltInFont bif);

  void GenerateOutput(std::string_view filename,
                      const std::map<int, DocTree> &pages) override;

  void GeneratePDF(const std::string &filename,
                   const std::map<int, DocTree> &pages);

  // This stuff should probably be generic, since we'd use this for
  // any backend.
  struct Transform {
    double dx = 0.0, dy = 0.0;
    double sx = 1.0, sy = 1.0;
  };

  struct Context {
    PDFFont font;
    double font_size = 12.0;
    uint32_t color = 0x000000FF;
  };

 private:
  void InitBuiltInFonts();
  const PDF::FontObj *AnyFontByName(const std::string &font_name);

  double FlipPageCoordinate(const PDF::Page &page, double y);

  void DrawText(const PDFFont &font,
                const std::string &text, double size,
                double x, double y,
                uint32_t color,
                PDF::Page *page);

  void DrawImage(double x, double y,
                 double width, double height,
                 const ImageRGBA &image,
                 PDF::Page *page);

  void PlaceStickersRec(Context context,
                        Transform transform,
                        const DocTree &doc,
                        PDF::Page *page);

  std::unique_ptr<PDF> pdf;
};

#endif
