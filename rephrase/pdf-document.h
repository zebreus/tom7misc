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

  explicit PDFFont(const PDF::FontObj *f);
  std::string Name() const override;

  std::optional<double>
  GetKerning(int codepoint1, int codepoint2) const override;

  double CharWidth(int codepoint) const override;

 private:
  PDFFont(const PDFFont &other) = delete;
  PDFFont &operator =(const PDFFont &other) = delete;

  friend struct PDFDocument;
  friend struct PDFPage;
  // Not owned.
  const PDF::FontObj *pdf_font = nullptr;
};

struct PDFPage : public Page {
  PDFPage(double width, double height,
          PDF *pdf, PDF::Page *pdf_page) : Page(width, height),
                                           pdf(pdf), pdf_page(pdf_page) {}

  void DrawText(const Font *font,
                const std::string &text, double size,
                double x, double y,
                uint32_t color) override;

  void DrawImage(double x, double y,
                 double width, double height,
                 const ImageRGBA &image) override;

  void DrawRect(double x, double y, double width, double height,
                double border_width, uint32_t color_fill,
                uint32_t color_border) override;

  void DrawLine(double x0, double y0,
                double x1, double y1,
                double line_width,
                uint32_t stroke_color) override;

  void DrawVideo(double x, double y,
                 double width, double height,
                 const std::string &src,
                 bool loop) override;

 private:
  PDFPage(const PDFPage &other) = delete;
  PDFPage &operator =(const PDFPage &other) = delete;

  double FlipPageCoordinate(double y) const;
  // Not owned.
  PDF *pdf = nullptr;
  PDF::Page *pdf_page = nullptr;
};

struct PDFDocument : public Document {
  PDFDocument(std::string_view program_dir);

  std::string LoadFontFile(const std::string &filename) override;

  void SetDocumentInfoStrings(
      const std::unordered_map<std::string, std::string> &info) override;

  const Font *GetBuiltInFont(PDF::BuiltInFont bif);
  const Font *GetDefaultFont() override;

  void GenerateOutput(
      std::string_view filename_base,
      const std::map<int, std::map<int, DocTree>> &pages) override;

  void GeneratePDF(const std::string &filename,
                   const std::map<int, std::map<int, DocTree>> &pages);

 private:
  double page_width = PDF::PDF_LETTER_WIDTH;
  double page_height = PDF::PDF_LETTER_HEIGHT;
  void InitBuiltInFonts();
  const PDF::FontObj *AnyFontByName(const std::string &font_name);
  std::unique_ptr<PDF> pdf;
};

#endif
