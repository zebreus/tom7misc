
#include "pdf-document.h"

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
      TextProps props;
      props.font_family = family;
      // Unused
      props.font_size = 1.0;

      props.font_bold = false;
      props.font_italic = false;

      RegisterFont(props, GetBIF(regular));

      props.font_bold = true;
      RegisterFont(props, GetBIF(bold));

      props.font_italic = true;
      RegisterFont(props, GetBIF(bold_italic));

      props.font_bold = false;
      RegisterFont(props, GetBIF(italic));
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
    TextProps props;
    props.font_family = "symbol";
    props.font_bold = false;
    props.font_italic = false;
    RegisterFont(props, GetBIF(PDF::SYMBOL));
  }

  {
    TextProps props;
    props.font_family = "zapf-dingbats";
    props.font_bold = false;
    props.font_italic = false;
    RegisterFont(props, GetBIF(PDF::ZAPF_DINGBATS));
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
  PDF::Info info;
  // XXX make settable from document.
  strncpy(info.creator, "BoVeX", 63);
  strncpy(info.producer, "BoVeX", 63);

  strncpy(info.title, "Test Document", 63);
  strncpy(info.author, "Tom 7", 63);
  strncpy(info.subject, "Documents do not need a 'subject'", 63);

  strncpy(info.date, DateTimeStamp().c_str(), 63);

  pdf.reset(new PDF((float)width, (float)height, info));

  InitBuiltInFonts();
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

#if 0
// For fonts that have already been loaded; returns nullptr if not
// found. This is like PDF::GetFontByName but it allows naming
// built-in fonts as well.
const PDF::FontObj *PDFDocument::AnyFontByName(const std::string &font_name) {
  const auto it = builtin_fonts.find(font_name);
  if (it != builtin_fonts.end()) {
    const PDF::FontObj *f = pdf->GetBuiltInFont(it->second);
    CHECK(f != nullptr) << "Built-in fonts should always succeed.";
    return f;
  }

  return pdf->GetFontByName(font_name);
}
#endif

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

  if (const std::string *font_name = doc.GetStringAttr("font-name")) {
    const Font *f = GetFontByName(*font_name);
    if (f == nullptr) {
      fprintf(stderr, ARED("Missing font: ") "%s\n", font_name->c_str());
    } else {
      context.font = *(const PDFFont *)f;
    }
  }

  // XXX text props from sticker:
  //   font size
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
  identity.dx = 0.0;
  identity.dy = 0.0;
  PlaceStickersRec(context, identity, doc, page);

  pdf->Save(filename);
  printf("Wrote " AGREEN("%s") "\n", filename.c_str());
}
