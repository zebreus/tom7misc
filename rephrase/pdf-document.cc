
#include "pdf-document.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string>
#include <cstring>
#include <format>
#include <chrono>
#include <unordered_map>
#include <utility>

#include "bignum/big.h"
#include "image.h"
#include "document.h"
#include "pdf.h"
#include "ansi.h"
#include "color-util.h"
#include "base/logging.h"

PDFFont::PDFFont(const PDF::FontObj *f) : pdf_font(f) {
  CHECK(f != nullptr);
}

using Transform = PDFDocument::Transform;

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

static inline Transform Translate(Transform t, double dx, double dy) {
  t.dx += dx;
  t.dy += dy;
  return t;
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
double PDFDocument::FlipPageCoordinate(const PDF::Page &page, double y) {
  const double height = page.Height();
  return height - y;
}

// The PDF lib uses (1-A)RGB colors, but we always use RGBA here.
static uint32_t PDFColor(uint32_t rgba) {
  const auto &[r, g, b, a] = ColorUtil::Unpack32(rgba);
  return ColorUtil::Pack32(255 - a, r, g, b);
}

void PDFDocument::DrawText(const PDFFont &font,
                           const std::string &text, double size,
                           double x, double y,
                           uint32_t color,
                           PDF::Page *page) {
  CHECK(page != nullptr);
  if (text.empty()) return;

  CHECK(font.pdf_font != nullptr);
  pdf->SetFont(font.pdf_font);
  pdf->AddText(text, size,
               // We flip the y coordinate, but also then need to
               // measure from the top of the text, not its baseline.
               x, FlipPageCoordinate(*page, y + size),
               PDFColor(color),
               page);
}

void PDFDocument::DrawImage(double x, double y,
                            double width, double height,
                            const ImageRGBA &image,
                            PDF::Page *page) {
  CHECK(page != nullptr);

  printf("Add image at %.11g %.11g.\n", x, y);
  CHECK(pdf->AddImageRGB(
            // Images are also measured from their baselines.
            x, FlipPageCoordinate(*page, y + height),
            width, height,
            image.IgnoreAlpha(),
            PDF::CompressionType::PNG,
            page));
}

void PDFDocument::PlaceStickersRec(Context context,
                                   Transform transform,
                                   const DocTree &doc,
                                   PDF::Page *page) {
  if (doc.IsText()) {
    // Place the text with the current transform.
    DrawText(context.font,
             doc.text,
             context.font_size,
             transform.dx, transform.dy,
             context.color,
             page);
    return;
  }

  if (doc.IsEmpty())
    return;

  if (doc.IsGroup()) {
    for (const std::shared_ptr<DocTree> &child : doc.children) {
      PlaceStickersRec(context, transform, *child, page);
    }
    return;
  }

  // Otherwise, the node should be a sticker.
  const std::string *display = doc.GetStringAttr("display");
  CHECK(display != nullptr) << "Any non-group node has to have a display "
    "when rendering the page.";

  CHECK(*display == "sticker") << "At this point everything should be "
    "stickers. Got node with display=" << *display;

  const double *x = doc.GetDoubleAttr("x");
  const double *y = doc.GetDoubleAttr("y");
  CHECK(x != nullptr && y != nullptr) << "Every sticker should have "
    "its final x= and y= coordinates.";

  if (const std::string *img = doc.GetStringAttr("img")) {
    const double *width = doc.GetDoubleAttr("img-width");
    const double *height = doc.GetDoubleAttr("img-height");
    CHECK(width != nullptr && height != nullptr) << "An img=\"\" on a "
      "sticker also requires img-width=\"\" and img-height\"\" (doubles).";
    const ImageRGBA *image = GetImageByName(*img);
    Transform ct = Translate(transform, *x, *y);
    if (image == nullptr) {
      fprintf(stderr, ARED("Missing image: ") "%s\n", img->c_str());
    } else {
      DrawImage(ct.dx, ct.dy, *width, *height, *image, page);
    }
  }

  if (const std::string *font_name = doc.GetStringAttr("font-name")) {
    const Font *f = GetFontByName(*font_name);
    if (f == nullptr) {
      fprintf(stderr, ARED("Missing font: ") "%s\n", font_name->c_str());
    } else {
      context.font = *(const PDFFont *)f;
    }
  }

  if (const double *font_size = doc.GetDoubleAttr("font-size")) {
    context.font_size = *font_size;
  }

  if (const BigInt *bc = doc.GetIntAttr("font-color")) {
    auto co = bc->ToInt();
    CHECK(co.has_value() && co.value() >= 0 &&
          co.value() <= int64_t{0xFFFFFFFF}) << "Color is out "
      "of range. Must be in [0, 0xFFFFFFFF]: " << bc->ToString();
    context.color = (uint32_t)co.value();
  }

  // XXX scaling

  for (const std::shared_ptr<DocTree> &child : doc.children) {
    Transform ct = Translate(transform, *x, *y);
    PlaceStickersRec(context, ct, *child, page);
  }
}

void PDFDocument::GeneratePDF(const std::string &filename,
                              const std::map<int, DocTree> &pages) {
  // We ignore gaps in the pages. If you want a blank page, make
  // a blank document.
  int num_pages = 0;
  for (const auto &[page_idx, doc] : pages) {
    PDF::Page *page = pdf->AppendNewPage();
    num_pages++;

    Context context;
    context.font = PDFFont(pdf->GetBuiltInFont(PDF::BuiltInFont::TIMES_ROMAN));
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
