
#include "pdf-document.h"

#include <array>
#include <string>
#include <string>
#include <cstring>
#include <format>
#include <chrono>

#include "document.h"
#include "pdf.h"

PDFFont::PDFFont(const PDF::FontObj *f) : pdf_font(f) {}

const Font *PDFDocument::GetDescribedFont(const Document::TextProps &props) {
  auto Variant = [&]() -> int {
      return (props.font_bold ? 1 : 0) + (props.font_italic ? 2 : 0);
    };
  if (props.font_face == "helvetica") {
    static constexpr std::array hvariants = {
      PDF::HELVETICA,
      PDF::HELVETICA_BOLD,
      PDF::HELVETICA_OBLIQUE,
      PDF::HELVETICA_BOLD_OBLIQUE,
    };
    return GetBuiltInFont(hvariants[Variant()]);

  } else if (props.font_face == "courier") {
    static constexpr std::array cvariants = {
      PDF::COURIER,
      PDF::COURIER_BOLD,
      PDF::COURIER_OBLIQUE,
      PDF::COURIER_BOLD_OBLIQUE,
    };
    return GetBuiltInFont(cvariants[Variant()]);

  } else if (props.font_face == "times") {
    static constexpr std::array tvariants = {
      PDF::TIMES_ROMAN,
      PDF::TIMES_BOLD,
      PDF::TIMES_ITALIC,
      PDF::TIMES_BOLD_ITALIC,
    };
    return GetBuiltInFont(tvariants[Variant()]);

  } else if (props.font_face == "symbol") {
    return GetBuiltInFont(PDF::SYMBOL);
  } else if (props.font_face == "zapf_dingbats") {
    return GetBuiltInFont(PDF::ZAPF_DINGBATS);
  } else {
    // Somehow need to get the "italic name" and "bold name".
    return GetFontByName(props.font_face);
  }

}

const Font *PDFDocument::GetBuiltInFont(PDF::BuiltInFont bif) {
  // Easier to get this directly, but we make sure it's present in
  // the font map anyway.
  const PDF::FontObj *fobj = pdf->GetBuiltInFont(bif);
  CHECK(fobj != nullptr) << "PDF can't get built-in font?";
  const std::string name = fobj->BaseFont();
  auto &fptr = fonts[name];
  if (fptr.get() == nullptr) {
    fptr.reset(new PDFFont(fobj));
  }

  return fptr.get();
}


// Get a font from the hash map, maybe by loading and inserting it first.
const Font *PDFDocument::GetFontByName(const std::string &name) {
  LOG(FATAL) << "Need to implement font loading: " << name;
}

std::string PDFFont::Name() const {
  return pdf_font->BaseFont();
}

std::optional<double> PDFFont::GetKerning(int codepoint1, int codepoint2) const {
  return pdf_font->GetKerning(codepoint1, codepoint2);
}

double PDFFont::CharWidth(int codepoint) const {
  return pdf_font->CharWidth(codepoint);
}

// We have our own internal kerning calculation, so maybe we should be leaving
// this out?
double PDFFont::GetKernedWidth(const std::string &text) const {
  return pdf_font->GetKernedWidth(text);
}

static std::string DateTimeStamp() {
  return std::format("{:%Y-%m-%d %H:%M:%S}",
                     std::chrono::system_clock::now());
}

PDFDocument::PDFDocument(double width, double height) {
  PDF::Info info;
  // XXX make settable from document.
  strncpy(info.creator, "BoVeX", 63);
  strncpy(info.producer, "BoVeX", 63);

  strncpy(info.title, "Test Document", 63);
  strncpy(info.author, "Tom 7", 63);
  strncpy(info.subject, "Documents do not need a 'subject'", 63);

  strncpy(info.date, DateTimeStamp().c_str(), 63);

  pdf.reset(new PDF((float)width, (float)height, info));
}
