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

 private:
  double FlipPageCoordinate(double y) const;
  // Not owned.
  PDF *pdf = nullptr;
  PDF::Page *pdf_page = nullptr;
};

struct PDFDocument : public Document {
  PDFDocument(double width, double height);

  std::string LoadFontFile(const std::string &filename) override;

  void SetDocumentInfoStrings(
      const std::unordered_map<std::string, std::string> &info) override;

  const Font *GetBuiltInFont(PDF::BuiltInFont bif);
  const Font *GetDefaultFont() override;

  void GenerateOutput(std::string_view filename,
                      const std::map<int, DocTree> &pages) override;

  void GeneratePDF(const std::string &filename,
                   const std::map<int, DocTree> &pages);

 private:
  void InitBuiltInFonts();
  const PDF::FontObj *AnyFontByName(const std::string &font_name);
  std::unique_ptr<PDF> pdf;
};

#endif
