
#include "pdfgen.h"

constexpr float INCH = 72.0f;

int main(int argc, char **argv) {

  struct pdf_info info = {
    .creator = "Tom 7",
    .producer = "testpdfgen.cc",
    .title = "Test Document",
    .author = "Tom 7",
    .subject = "NO SUBJECT",
    .date = "29 Dec 2023"
  };

  struct pdf_doc *pdf = pdf_create(
      PDF_LETTER_WIDTH,
      PDF_LETTER_HEIGHT,
      &info);

  // TODO: Embed fonts!
  pdf_set_font(pdf, "Times-Roman");

  pdf_append_page(pdf);
  pdf_add_text(pdf, nullptr, "I put text in it :)",
               12, 50, 20, PDF_BLACK);
  pdf_add_line(pdf, nullptr, 50, 24, 150, 24, 2, PDF_RED);
  pdf_save(pdf, "pdfgen-out.pdf");
  pdf_destroy(pdf);

  return 0;
}
