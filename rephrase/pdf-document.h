#ifndef _REPHRASE_PDF_DOCUMENT_H
#define _REPHRASE_PDF_DOCUMENT_H

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
  const PDF::FontObj *pdf_font = nullptr;
};

struct PDFDocument : public Document {
  PDFDocument(double width, double height);

  const Font *GetDescribedFont(const TextProps &props) override;

  const Font *GetBuiltInFont(PDF::BuiltInFont bif);
  const Font *GetFontByName(const std::string &s);

  void GeneratePDF(const std::string &filename,
                   const DocTree &doc);

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

  double FlipPageCoordinate(const PDF::Page &page, double y);

  void DrawText(const PDFFont &font,
                const std::string &text, double size,
                double x, double y,
                uint32_t color,
                PDF::Page *page);

  void PlaceStickersRec(Context context,
                        Transform transform,
                        const DocTree &doc,
                        PDF::Page *page);

  std::unique_ptr<PDF> pdf;
};

#endif
