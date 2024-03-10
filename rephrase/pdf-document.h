#ifndef _REPHRASE_PDF_DOCUMENT_H
#define _REPHRASE_PDF_DOCUMENT_H

#include "document.h"
#include "pdf.h"

struct PDFFont : public Font {
  explicit PDFFont(const PDF::FontObj *f);
  std::string Name() const override;
  std::optional<double> GetKerning(int codepoint1, int codepoint2) const override;
  double CharWidth(int codepoint) const override;
  double GetKernedWidth(const std::string &text) const override;

 private:
  const PDF::FontObj *pdf_font;
};

struct PDFDocument : public Document {
  PDFDocument(double width, double height);

  const Font *GetDescribedFont(const TextProps &props) override;

  const Font *GetBuiltInFont(PDF::BuiltInFont bif);
  const Font *GetFontByName(const std::string &s);

  std::unique_ptr<PDF> pdf;
};

#endif
