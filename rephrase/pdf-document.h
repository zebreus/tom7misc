#ifndef _REPHRASE_PDF_DOCUMENT_H
#define _REPHRASE_PDF_DOCUMENT_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <variant>

#include "image.h"
#include "document.h"
#include "pdf.h"
#include "svg.h"

struct PDFFont : public Font {
  // Invalid state.
  PDFFont() = default;

  explicit PDFFont(const PDF::Font *f);
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
  const PDF::Font *pdf_font = nullptr;
};

struct PDFImage : public Image {
  // Invalid state.
  PDFImage() = default;

  PDFImage(std::string_view name, std::unique_ptr<ImageRGBA> img);
  PDFImage(std::string_view name, double w, double h,
           std::string svg_handle);
  std::string Name() const override;

  double Width() const override;
  double Height() const override;

  bool IsRaster() const override;
  ImageRGBA GetRaster() const override;

 private:
  PDFImage(const PDFImage &other) = delete;
  PDFImage &operator =(const PDFImage &other) = delete;

  friend struct PDFDocument;
  friend struct PDFPage;
  std::string name;

  // If string, then it's a svg handle in the PDF object.
  std::variant<std::string, ImageRGBA> img;
  double width = 0.0, height = 0.0;
};


struct PDFPage : public Page {
  PDFPage(double width, double height,
          PDF *pdf, PDF::Page *pdf_page) : Page(width, height),
                                           pdf(pdf), pdf_page(pdf_page) {}

  void DrawText(const Font *font,
                std::string_view text, double size,
                double x, double y,
                uint32_t color) override;

  void DrawImage(const Image *image,
                 double x, double y,
                 double width, double height) override;

  void DrawRect(double x, double y, double width, double height,
                double border_width, uint32_t color_fill,
                uint32_t color_border) override;

  void DrawLine(double x0, double y0,
                double x1, double y1,
                double line_width,
                uint32_t stroke_color) override;

  void DrawVideo(double x, double y,
                 double width, double height,
                 std::string_view src,
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

  std::string LoadFontFile(std::string_view filename) override;

  std::string AddImage(std::unique_ptr<ImageRGBA> img) override;
  std::string AddImage(std::unique_ptr<SVG::Doc> img) override;

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
  const PDF::Font *AnyFontByName(const std::string &font_name);
  std::unique_ptr<PDF> pdf;
};

#endif
