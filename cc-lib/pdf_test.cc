
#include "pdf.h"

#include <cmath>
#include <numbers>
#include <string>
#include <vector>
#include <utility>
#include <optional>

#include "base/logging.h"
#include "ansi.h"
#include "image.h"
#include "arcfour.h"
#include "randutil.h"
#include "util.h"

static constexpr std::initializer_list<const char *> FONTS = {
  "cmunrm.ttf",
  "c:\\windows\\fonts\\pala.ttf",
};

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

static ImageRGB RandomRGB(ArcFour *rc, int width, int height) {
  ImageRGB img(width, height);
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      uint8_t r = rc->Byte();
      uint8_t g = rc->Byte();
      uint8_t b = rc->Byte();
      img.SetPixel(x, y, r, g, b);
    }
  }
  return img;
}

static void MakeSimplePDF() {
  ArcFour rc("pdf");

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

  std::string pasement_name = pdf.AddTTF("fonts/DFXPasement9px.ttf");

  {
    printf(AWHITE("Shapes page") ".\n");
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
    CHECK(pdf.AddPolygon(Star(500, 150, 50, 100, 9), 2.0f,
                         PDF_RGB(0, 0, 0)));

    CHECK(pdf.AddFilledPolygon(Star(500, 150, 25, 75, 9), 1.0f,
                               PDF_RGB(0x70, 0x70, 0)));

    pdf.SetFont(PDF::TIMES_ROMAN);
    CHECK(pdf.AddText("Title of PDF", 72,
                      30, PDF::PDF_LETTER_HEIGHT - 72 - 36,
                      PDF_RGB(0, 0, 0)));
    pdf.SetFont(PDF::HELVETICA);
    CHECK(pdf.AddTextWrap(
              "Lorem ipsum dolor sit amet, consectetur adipiscing elit, "
              "sed do eiusmod tempor incididunt ut labore et dolore magna "
              "aliqua. Ut enim ad minim veniam, quis nostrud exercitation "
              "ullamco laboris nisi ut aliquip ex ea commodo consequat. "
              "Duis aute irure dolor in reprehenderit in voluptate velit "
              "esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
              "occaecat cupidatat non proident, sunt in culpa qui officia "
              "deserunt mollit anim id est laborum.",
              20,
              36, PDF::PDF_LETTER_HEIGHT - 72 - 36 - 48,
              0.0f,
              PDF_RGB(0, 0, 0),
              PDF_INCH_TO_POINT(3.4f),
              PDF::PDF_ALIGN_JUSTIFY));

    pdf.SetFont(PDF::TIMES_ROMAN);
    CHECK(pdf.AddTextRotate(
              "Camera-Ready Copy",
              12,
              350, 36,
              // almost 180 degrees
              3.0,
              PDF_RGB(0x70, 0, 0)));

    pdf.SetFont(pasement_name);

    pdf.AddText("Embedded font text", 16,
                36, 150, PDF_RGB(0, 0, 70));

    pdf.SetFont(PDF::HELVETICA_OBLIQUE);
    pdf.AddSpacedLine({{"Over", -1.0f / 16.0f}, {"lapping", 100.0f}},
                      16,
                      36, 200, PDF_RGB(80, 0, 0));
  }

  {
    printf(AWHITE("Kerning page") ".\n");
    [[maybe_unused]]
    PDF::Page *page = pdf.AppendNewPage();

    float ypos = PDF::PDF_LETTER_HEIGHT - 72 - 36;

    for (const char *filename : FONTS) {
      if (Util::ExistsFile(filename)) {
        const std::string embedded_name = pdf.AddTTF(filename);
        PDF::FontObj *embedded = pdf.GetFontByName(embedded_name);
        CHECK(embedded != nullptr) << filename << " exists but can't be "
          "loaded?";
        pdf.SetFont(embedded_name);

        CHECK(pdf.AddText(filename, 36,
                          30, ypos,
                          PDF_RGB(0, 0, 0)));
        ypos -= 42.0;

        auto CompareKern = [&](const std::string &s, float size) {
            CHECK(pdf.AddText(s, size,
                              30, ypos,
                              PDF_RGB(0, 0, 0)));

            ypos -= size * 1.1;

            PDF::SpacedLine kerned = embedded->KernText(s);
            CHECK(pdf.AddSpacedLine(kerned, size,
                                    30, ypos,
                                    PDF_RGB(0, 0, 0),
                                    0.0f));
            ypos -= size * 1.2;
          };

        CompareKern("To Await Is To Worry.", 36);
        CompareKern("Mr. Jock, T.V. Quiz Ph.D., bags few lynx.", 18);

        ypos -= 72.0f;

      } else {
        printf("Missing " ARED("%s") "\n", filename);
        CHECK(pdf.AddText("Missing " + std::string(filename), 36,
                          30, PDF::PDF_LETTER_HEIGHT - 72 - 36,
                          PDF_RGB(0, 0, 0)));
      }
    }
  }

  {
    printf(AWHITE("Barcode page") ".\n");
    PDF::Page *page = pdf.AppendNewPage();

    static constexpr float GAP = 38.0f;
    static constexpr float BARCODE_HEIGHT = PDF_INCH_TO_POINT(1.0f);
    static constexpr float BARCODE_WIDTH = BARCODE_HEIGHT * std::numbers::phi;
    float y = PDF::PDF_LETTER_HEIGHT - GAP - BARCODE_HEIGHT;
    CHECK(pdf.AddBarcode128a(45, y, BARCODE_WIDTH, BARCODE_HEIGHT,
                             "SIGBOVIK.ORG", PDF_RGB(0, 0, 0)));
    y -= BARCODE_HEIGHT + GAP;

    CHECK(pdf.AddBarcode39(45, y, BARCODE_WIDTH, BARCODE_HEIGHT,
                           "SIGBOVIK.ORG", PDF_RGB(0, 0, 0), page));

    y -= BARCODE_HEIGHT + GAP;

    CHECK(pdf.AddBarcodeEAN13(45, y, BARCODE_WIDTH, BARCODE_HEIGHT,
                              "9780000058898", PDF_RGB(0, 0, 0))) <<
      "Error: " << pdf.GetErr();

    y -= BARCODE_HEIGHT + GAP;

    CHECK(pdf.AddBarcodeUPCA(45, y, BARCODE_WIDTH, BARCODE_HEIGHT,
                             "101234567897", PDF_RGB(0, 0, 0))) <<
      "Error: " << pdf.GetErr();

    y -= BARCODE_HEIGHT + GAP;

    CHECK(pdf.AddBarcodeEAN8(45, y, BARCODE_WIDTH, BARCODE_HEIGHT,
                             "80084321", PDF_RGB(0, 0, 0))) <<
      "Error: " << pdf.GetErr();

    y -= BARCODE_HEIGHT + GAP;

    CHECK(pdf.AddBarcodeUPCE(45, y, BARCODE_WIDTH, BARCODE_HEIGHT,
                             "042100005264", PDF_RGB(0, 0, 0))) <<
      "Error: " << pdf.GetErr();
  }

  {
    printf(AWHITE("Image page") ".\n");
    // Barcode page.
    PDF::Page *page = pdf.AppendNewPage();

    ImageRGBA img(160, 80);
    img.Clear32(0xFFFFFFFF);
    img.BlendFilledCircleAA32(50, 40, 31.1, 0x903090AA);

    CHECK(pdf.AddImageRGB(300, 300, 72.0, -1.0,
                          img.IgnoreAlpha())) << "Error: " <<
      pdf.GetErr();

    ImageRGB rand = RandomRGB(&rc, 384, 256);
    CHECK(pdf.AddImageRGB(100, PDF::PDF_LETTER_HEIGHT - 72 * 3, -1.0, 72 * 2,
                          rand, PDF::CompressionType::PNG, page)) << "Error: " <<
      pdf.GetErr();

    ImageRGB rand2 = RandomRGB(&rc, 384, 256);
    CHECK(pdf.AddImageRGB(320, PDF::PDF_LETTER_HEIGHT - 72 * 3, -1.0, 72 * 2,
                          rand2, PDF::CompressionType::JPG_10, page)) << "Error: " <<
      pdf.GetErr();

    pdf.SetFont(pasement_name);

    static constexpr char insist_utf8[4] = "\u2014";
    static_assert((uint8_t)insist_utf8[0] == 0xE2);
    static_assert((uint8_t)insist_utf8[1] == 0x80);
    static_assert((uint8_t)insist_utf8[2] == 0x94);
    static_assert((uint8_t)insist_utf8[3] == 0x00);

    CHECK(pdf.AddTextWrap(
              "WELCOME TO THE WWW INTERNET. One of the finest interns of all "
              "time: E.T.. It is illicitly lilliputian. "
              "'lillili.' "
              "@WASTE@ #NOT#. &WANT& !NOT!. ,FONT, \u2014NAUGHT\u2014. "
              "!@#$%^&*()-=",
              18,
              36, 72 * 3,
              0.0f,
              PDF_RGB(0, 0, 0),
              PDF_INCH_TO_POINT(7.0f),
              PDF::PDF_ALIGN_JUSTIFY)) << "Error: " << pdf.GetErr();
  }

  printf("Save it...\n");

  pdf.Save("test.pdf");
  printf("Wrote " AGREEN("test.pdf") "\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  MakeSimplePDF();

  printf("OK\n");
  return 0;
}
