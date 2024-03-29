
#include "pdf-document.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "color-util.h"
#include "image.h"
#include "pdf.h"

#include "document.h"

PDFFont::PDFFont(const PDF::FontObj *f) : pdf_font(f) {
  CHECK(f != nullptr);
}

using Transform = PDFDocument::Transform;

const Font *PDFDocument::GetDefaultFont() {
  return GetFontByName(
      pdf->GetBuiltInFont(PDF::TIMES_ROMAN)->BaseFont());
}

std::string PDFDocument::LoadFontFile(const std::string &filename) {
  std::string name = pdf->AddTTF(filename);
  const PDF::FontObj *fobj = pdf->GetFontByName(name);
  CHECK(fobj != nullptr) << "Couldn't find just-loaded font? " << filename;
  fonts[name] = std::make_unique<PDFFont>(fobj);
  return name;
}

void PDFDocument::InitBuiltInFonts() {
  CHECK(pdf.get() != nullptr) << "Needs the PDF object to be created first!";
  // Returns pointer; stores in fonts vector to maintain lifetime.
  auto GetBIF = [this](PDF::BuiltInFont bif) -> const PDFFont * {
      const PDF::FontObj *fobj = pdf->GetBuiltInFont(bif);
      auto font = std::make_unique<PDFFont>(fobj);
      const PDFFont *f = font.get();
      CHECK(f != nullptr) << "Failed to load built-in font??";
      fonts[f->Name()] = std::move(font);
      return f;
    };

  auto RegisterFourFonts = [&](const std::string &family,
                               PDF::BuiltInFont regular,
                               PDF::BuiltInFont bold,
                               PDF::BuiltInFont italic,
                               PDF::BuiltInFont bold_italic) {
      FontDescription desc;
      desc.font_family = family;

      desc.font_bold = false;
      desc.font_italic = false;

      RegisterFont(desc, GetBIF(regular));

      desc.font_bold = true;
      RegisterFont(desc, GetBIF(bold));

      desc.font_italic = true;
      RegisterFont(desc, GetBIF(bold_italic));

      desc.font_bold = false;
      RegisterFont(desc, GetBIF(italic));
    };

  RegisterFourFonts("helvetica",
                    PDF::HELVETICA,
                    PDF::HELVETICA_BOLD,
                    PDF::HELVETICA_OBLIQUE,
                    PDF::HELVETICA_BOLD_OBLIQUE);

  RegisterFourFonts("courier",
                    PDF::COURIER,
                    PDF::COURIER_BOLD,
                    PDF::COURIER_OBLIQUE,
                    PDF::COURIER_BOLD_OBLIQUE);

  RegisterFourFonts("times",
                    PDF::TIMES_ROMAN,
                    PDF::TIMES_BOLD,
                    PDF::TIMES_ITALIC,
                    PDF::TIMES_BOLD_ITALIC);

  {
    FontDescription desc;
    desc.font_family = "symbol";
    desc.font_bold = false;
    desc.font_italic = false;
    RegisterFont(desc, GetBIF(PDF::SYMBOL));
  }

  {
    FontDescription desc;
    desc.font_family = "zapf-dingbats";
    desc.font_bold = false;
    desc.font_italic = false;
    RegisterFont(desc, GetBIF(PDF::ZAPF_DINGBATS));
  }
}

std::string PDFFont::Name() const {
  return pdf_font->BaseFont();
}

std::optional<double>
PDFFont::GetKerning(int codepoint1, int codepoint2) const {
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
  pdf.reset(new PDF((float)width, (float)height));

  PDF::Info info;
  strncpy(info.creator, "BoVeX", 63);
  strncpy(info.producer, "BoVeX", 63);
  strncpy(info.title, "Untitled", 63);
  strncpy(info.author, "", 63);
  strncpy(info.subject, "", 63);
  strncpy(info.date, DateTimeStamp().c_str(), 63);
  pdf->SetInfo(info);

  InitBuiltInFonts();
}

void PDFDocument::SetDocumentInfoStrings(
    const std::unordered_map<std::string, std::string> &m) {

  PDF::Info info = pdf->GetInfo();
  auto AddIf = [&m](const std::string &key, char *field) {
      auto it = m.find(key);
      if (it == m.end()) return;
      strncpy(field, it->second.data(), 63);
      field[64] = '\0';
    };

  AddIf("creator", info.creator);
  AddIf("producer", info.producer);
  AddIf("title", info.title);
  AddIf("author", info.author);
  AddIf("title", info.title);
  AddIf("subject", info.subject);
  AddIf("date", info.date);

  pdf->SetInfo(info);
}

// These draw routines are just like their counterparts in PDF, except
// except that the y coordinates are flipped. We only use y-down
// coordinates ("regular graphics coordinates") in BoVeX, and
// the PDF library only uses y-up. Both use "points" as the distance
// unit uniformly.
//
// Note that many objects in PDF (text, images) are measured from their
// bottom-left coordinate, so you may also need to add the object height
// before flipping.
double PDFPage::FlipPageCoordinate(double y) const {
  const double height = Height();
  return height - y;
}

// The PDF lib uses (1-A)RGB colors, but we always use RGBA here.
static uint32_t PDFColor(uint32_t rgba) {
  const auto &[r, g, b, a] = ColorUtil::Unpack32(rgba);
  return ColorUtil::Pack32(255 - a, r, g, b);
}

void PDFPage::DrawText(const Font *font_in,
                       const std::string &text, double size,
                       double x, double y,
                       uint32_t color) {
  const PDFFont *font = (const PDFFont*)font_in;

  CHECK(pdf_page != nullptr);
  if (text.empty()) return;

  CHECK(font->pdf_font != nullptr);
  pdf->SetFont(font->pdf_font);
  pdf->AddText(text, size,
               // We flip the y coordinate, but also then need to
               // measure from the top of the text, not its baseline.
               x, FlipPageCoordinate(y + size),
               PDFColor(color),
               pdf_page);
}

void PDFPage::DrawImage(double x, double y,
                        double width, double height,
                        const ImageRGBA &image) {
  CHECK(pdf_page != nullptr);

  printf("Add image at %.11g %.11g.\n", x, y);
  CHECK(pdf->AddImageRGB(
            // Images are also measured from their baselines.
            x, FlipPageCoordinate(y + height),
            width, height,
            image.IgnoreAlpha(),
            PDF::CompressionType::PNG,
            pdf_page));
}

void PDFDocument::GenerateOutput(std::string_view filename,
                                 const std::map<int, DocTree> &pages) {
  return GeneratePDF(std::string(filename) + ".pdf", pages);
}

void PDFDocument::GeneratePDF(const std::string &filename,
                              const std::map<int, DocTree> &pages) {
  // We ignore gaps in the pages. If you want a blank page, make
  // a blank document.
  int num_pages = 0;
  std::vector<std::unique_ptr<PDFPage>> pageptrs;
  for (const auto &[page_idx, doc] : pages) {
    PDF::Page *pdf_page = pdf->AppendNewPage();
    pageptrs.emplace_back(std::make_unique<PDFPage>(
                              pdf_page->Width(), pdf_page->Height(),
                              pdf.get(), pdf_page));
    Page *page = pageptrs.back().get();
    num_pages++;

    Context context;
    context.font = GetDefaultFont();
    // PDFFont(pdf->GetBuiltInFont(PDF::BuiltInFont::TIMES_ROMAN));
    context.color = 0x000000FF;
    Transform identity;
    identity.dx = 0.0;
    identity.dy = 0.0;
    PlaceStickersRec(context, identity, doc, page);
  }

  pdf->Save(filename);
  printf("Wrote %d page%s to " AGREEN("%s") ".\n",
         num_pages, num_pages != 1 ? "s" : "",
         filename.c_str());
}
