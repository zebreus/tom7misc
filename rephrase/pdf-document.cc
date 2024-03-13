
#include "pdf-document.h"

#include <array>
#include <string>
#include <string>
#include <cstring>
#include <format>
#include <chrono>

#include "document.h"
#include "pdf.h"
#include "ansi.h"
#include "color-util.h"

PDFFont::PDFFont(const PDF::FontObj *f) : pdf_font(f) {
  CHECK(f != nullptr);
}

using Transform = PDFDocument::Transform;

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
  CHECK(fobj != nullptr) << "PDF can't get built-in font? " <<
    PDF::BuiltInFontName(bif);
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

// These draw routines are just like their counterparts in PDF, except
// except that the y coordinates are flipped. We only use y-down
// coordinates ("regular graphics coordinates") in BoVeX, and
// the PDF library only uses y-up. Both use "points" as the distance
// unit uniformly.
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
               x, FlipPageCoordinate(*page, y),
               PDFColor(color),
               page);
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

  // XXX text props from sticker
  // XXX scaling

  for (const std::shared_ptr<DocTree> &child : doc.children) {
    Transform ct = Translate(transform, *x, *y);
    PlaceStickersRec(context, ct, *child, page);
  }
}

void PDFDocument::GeneratePDF(const std::string &filename,
                              const DocTree &doc) {
  // XXX: Some way of having multiple pages. Maybe just a newpage
  // primop.
  PDF::Page *page = pdf->AppendNewPage();

  Context context;
  context.font = PDFFont(pdf->GetBuiltInFont(PDF::BuiltInFont::TIMES_ROMAN));
  Transform identity;
  // XXX default margins like this should come from the document
  identity.dx = 72.0 / 2.0;
  identity.dy = 72.0 / 2.0;
  PlaceStickersRec(context, identity, doc, page);

  pdf->Save(filename);
  printf("Wrote " AGREEN("%s") "\n", filename.c_str());
}
