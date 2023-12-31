
#include "pdf.h"

#include <cmath>
#include <numbers>

static std::vector<std::pair<float, float>> Star(
    float x, float y,
    float r1, float r2,
    int tines) {

  std::vector<std::pair<float, float>> ret;
  ret.reserve(tines * 2);
  for (int i = 0; i < tines; i++) {
    double theta1 = ((i * 2 + 0) / double(tines * 2)) *
      2.0 * std::numbers::pi;
    double theta2 = ((i * 2 + 1) / double(tines * 2)) *
      2.0 * std::numbers::pi;
    ret.emplace_back(x + cosf(theta1) * r1, y + sinf(theta1) * r1);
    ret.emplace_back(x + cosf(theta2) * r2, y + sinf(theta2) * r2);
  }
  return ret;
}

static void MakeSimplePDF() {
  PDF::Info info;
  sprintf(info.creator, "pdf_test.cc");
  sprintf(info.producer, "Tom 7");
  sprintf(info.title, "It is a test");
  sprintf(info.author, "None");
  sprintf(info.author, "No subject");
  sprintf(info.date, "30 Dec 2023");

  printf("Create PDF object.\n");
  PDF pdf(PDF::PDF_LETTER_WIDTH,
          PDF::PDF_LETTER_HEIGHT,
          info);

  PDF::Page *page = pdf.AppendNewPage();

  pdf.AddLine(3, 8, 100, 50, 2.0f,
              PDF_RGB(0x90, 0x40, 0x40));

  pdf.AddQuadraticBezier(3, 8, 100, 50,
                         75, 100,
                         2.0f,
                         PDF_RGB(0x40, 0x90, 0x40));

  pdf.AddCubicBezier(3, 8, 100, 50,
                     75, 5, 15, 120,
                     2.0f,
                     PDF_RGB(0x40, 0x40, 0x90),
                     page);

  pdf.AddCircle(400, 500, 75, 1,
                PDF_RGB(0x90, 0x60, 0x60),
                PDF_RGB(0xf0, 0xa0, 0xa0));

  pdf.AddRectangle(PDF::PDF_LETTER_WIDTH - 200,
                   PDF::PDF_LETTER_HEIGHT - 200,
                   100, 125,
                   2.0f,
                   PDF_RGB(0x40, 0x40, 0x10));

  pdf.AddFilledRectangle(PDF::PDF_LETTER_WIDTH - 200,
                         PDF::PDF_LETTER_HEIGHT - 400,
                         100, 125,
                         2.0f,
                         PDF_RGB(0xBB, 0xBB, 0xBB),
                         PDF_RGB(0x40, 0x40, 0x10));

  // TODO: AddCustomPath
  pdf.AddPolygon(Star(500, 150, 50, 100, 9), 2.0f,
                 PDF_RGB(0, 0, 0));

  pdf.AddFilledPolygon(Star(500, 150, 25, 75, 9), 1.0f,
                       PDF_RGB(0x70, 0x70, 0));


  printf("Save it...\n");

  pdf.Save("test.pdf");
}

int main(int argc, char **argv) {

  MakeSimplePDF();

  printf("OK\n");
  return 0;
}
