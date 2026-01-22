
#include "pdf.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "image.h"
#include "svg.h"
#include "util.h"

static constexpr std::initializer_list<const char *> FONTS = {
  "cmunrm.ttf",
  // "c:\\windows\\fonts\\pala.ttf",
  "cour.ttf",
};

using SpacedLine = PDF::SpacedLine;
using Font = PDF::Font;

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

static void BuiltInWidths() {
  PDF pdf(PDF::PDF_LETTER_WIDTH, PDF::PDF_LETTER_HEIGHT);

  for (const PDF::BuiltInFont bif : {
      PDF::HELVETICA,
      PDF::HELVETICA_BOLD,
      PDF::HELVETICA_OBLIQUE,
      PDF::HELVETICA_BOLD_OBLIQUE,
      PDF::COURIER,
      PDF::COURIER_BOLD,
      PDF::COURIER_OBLIQUE,
      PDF::COURIER_BOLD_OBLIQUE,
      PDF::TIMES_ROMAN,
      PDF::TIMES_BOLD,
      PDF::TIMES_ITALIC,
      PDF::TIMES_BOLD_ITALIC,
      PDF::SYMBOL,
      PDF::ZAPF_DINGBATS }) {

    const Font *font = pdf.GetBuiltInFont(bif);
    CHECK(font->CharWidth('M') > 0) << PDF::BuiltInFontName(bif);

    {
      double w1 = font->GetKernedWidth("Portable");
      double w2 = font->GetKernedWidth("Potable");
      CHECK(w1 > w2) << w1 << " " << w2;
    }

    {
      float w1 = -1.0, w2 = -1.0;
      CHECK(pdf.GetTextWidth("Congenital", 14.0, &w1, font));
      CHECK(pdf.GetTextWidth("Congenial", 14.0, &w2, font));
      CHECK(w1 > 32.0f && w2 > 32.0f && w1 > w2) << w1 << " " << w2;
    }
  }
}

static void SpaceLine() {
  PDF pdf(PDF::PDF_LETTER_WIDTH, PDF::PDF_LETTER_HEIGHT);
  PDF::Info info;
  info.creator = "pdf_test.cc";
  info.producer = "Tom 7";
  info.title = "It is a test";
  info.author = "None";
  info.subject = "No subject";
  info.date = "6 Apr 2025";
  pdf.SetInfo(info);

  const Font *times = pdf.GetBuiltInFont(PDF::TIMES_ROMAN);

  {
    std::vector<PDF::SpacedLine> lines =
      pdf.SpaceLines("simple", 1000, times);
    CHECK(lines.size() == 1);
    const SpacedLine &line = lines[0];
    CHECK(line.size() == 1);
    CHECK(line[0].first == "simple");
  }

  {
    std::vector<PDF::SpacedLine> lines =
      pdf.SpaceLines("simple one", 1000, times);
    CHECK(lines.size() == 1);
    const SpacedLine &line = lines[0];
    CHECK(line.size() == 3);
    CHECK(line[0].first == "simple");
    CHECK(line[1].first == " ");
    CHECK(line[2].first == "one");
  }

  {
    double width = 0.9 * times->GetKernedWidth("simple one");
    std::vector<PDF::SpacedLine> lines =
      pdf.SpaceLines("simple one", width, times);
    CHECK(lines.size() == 2) << lines.size();
    const SpacedLine &line1 = lines[0];
    CHECK(line1.size() == 1);
    CHECK(line1[0].first == "simple");
    const SpacedLine &line2 = lines[1];
    CHECK(line2.size() == 1);
    CHECK(line2[0].first == "one");
  }

  printf("SpaceLine " AGREEN("OK") ".\n");
}

static constexpr std::string_view WARNING_SVG = R"(
<?xml version="1.0" encoding="UTF-8"?>
<svg id="Layer_1" xmlns="http://www.w3.org/2000/svg" version="1.1" viewBox="0 0 432 379.442">
  <g>
    <path d="M34.285,359.405c-5.944,0-11.267-3.073-14.239-8.221-2.972-5.148-2.972-11.295,0-16.442L199.404,24.086c2.972-5.148,8.295-8.221,14.239-8.221s11.267,3.073,14.239,8.221l179.357,310.656c2.973,5.147,2.972,11.294,0,16.442-2.972,5.147-8.295,8.221-14.239,8.221H34.285Z" fill="#fff"/>
    <path d="M213.643,21.865c1.813,0,6.322.509,9.043,5.221l179.357,310.656c2.721,4.712.906,8.872,0,10.442-.906,1.57-3.602,5.221-9.043,5.221H34.285c-5.441,0-8.137-3.651-9.043-5.221-.906-1.57-2.721-5.73,0-10.442L204.6,27.086c2.721-4.712,7.23-5.221,9.043-5.221M213.643,9.865c-7.558,0-15.116,3.74-19.435,11.221L14.85,331.742c-8.638,14.961,2.159,33.663,19.435,33.663h358.715c17.276,0,28.073-18.702,19.435-33.663L233.078,21.086c-4.319-7.481-11.877-11.221-19.435-11.221h0Z"/>
  </g>
  <path d="M209.455,37.575L36.841,336.551c-1.853,3.209.463,7.221,4.169,7.221h345.228c3.706,0,6.022-4.012,4.169-7.221L217.793,37.575c-1.853-3.209-6.485-3.209-8.338,0Z" fill="#f9ff00"/>
  <polygon points="124.279 226.837 124.744 198.465 194.047 79.163 206.14 103.116 124.279 226.837" fill="#fff" opacity=".6"/>
  <g opacity=".4">
    <g>
      <path d="M216.488,372.641c-16.774,0-28.95-12.762-28.95-30.346s12.175-30.347,28.95-30.347c17.046,0,28.95,12.479,28.95,30.347s-11.904,30.346-28.95,30.346ZM200.67,277.263c-.812,0-1.476-.646-1.5-1.456l-6.514-223.322c-.012-.405.141-.798.423-1.089.283-.291.671-.455,1.076-.455h44.663c.405,0,.794.164,1.076.455.282.291.435.684.423,1.089l-6.514,223.322c-.023.811-.688,1.456-1.499,1.456h-31.636Z"/>
      <path d="M238.82,52.441l-6.514,223.322h-31.636l-6.514-223.322h44.664M216.488,313.448c16.748,0,27.45,12.097,27.45,28.846,0,16.284-10.702,28.846-27.45,28.846-16.284,0-27.45-12.562-27.45-28.846,0-16.749,11.631-28.846,27.45-28.846M238.82,49.441h-44.664c-.811,0-1.587.328-2.152.91-.565.582-.87,1.367-.847,2.178l6.514,223.322c.047,1.622,1.376,2.913,2.999,2.913h31.636c1.623,0,2.951-1.29,2.999-2.913l6.514-223.322c.024-.81-.282-1.596-.847-2.178-.565-.582-1.341-.91-2.152-.91h0ZM238.82,55.441h.005-.005ZM216.488,310.448c-8.492,0-16.244,3.221-21.827,9.071-5.561,5.826-8.623,13.914-8.623,22.775,0,8.733,2.992,16.75,8.424,22.576,5.575,5.978,13.397,9.271,22.025,9.271,8.807,0,16.696-3.297,22.212-9.284,5.312-5.766,8.238-13.779,8.238-22.562,0-18.75-12.521-31.846-30.45-31.846h0Z"/>
    </g>
  </g>
  <g>
    <path d="M211.48,366.665c-16.774,0-28.95-12.762-28.95-30.346s12.175-30.347,28.95-30.347c17.046,0,28.95,12.479,28.95,30.347s-11.904,30.346-28.95,30.346ZM195.662,271.287c-.812,0-1.476-.646-1.5-1.456l-6.514-223.322c-.012-.405.141-.798.423-1.089.283-.291.671-.455,1.076-.455h44.664c.405,0,.794.164,1.076.455.282.291.435.684.423,1.089l-6.515,223.322c-.023.811-.688,1.456-1.499,1.456h-31.636Z" fill="#d00"/>
    <path d="M233.812,46.465l-6.514,223.322h-31.636l-6.514-223.322h44.664M211.481,307.473c16.748,0,27.45,12.097,27.45,28.846,0,16.284-10.702,28.846-27.45,28.846-16.284,0-27.45-12.562-27.45-28.846,0-16.749,11.631-28.846,27.45-28.846M233.812,43.465h-44.664c-.811,0-1.587.328-2.152.91-.565.582-.87,1.367-.847,2.178l6.514,223.322c.047,1.622,1.376,2.913,2.999,2.913h31.636c1.623,0,2.951-1.29,2.999-2.913l6.514-223.322c.024-.81-.282-1.596-.847-2.178-.565-.582-1.341-.91-2.152-.91h0ZM233.812,49.465h.005-.005ZM211.481,304.473c-8.492,0-16.244,3.221-21.827,9.071-5.561,5.826-8.623,13.914-8.623,22.775,0,8.733,2.992,16.75,8.424,22.576,5.575,5.978,13.397,9.271,22.025,9.271,8.807,0,16.696-3.297,22.212-9.284,5.312-5.766,8.238-13.779,8.238-22.562,0-18.75-12.521-31.846-30.45-31.846h0Z"/>
  </g>
</svg>
)";

static void MakeSimplePDF() {
  ArcFour rc("pdf");

  static constexpr bool COMPRESS_TEST_PDF = false;

  printf("Create PDF object.\n");
  PDF pdf(PDF::PDF_LETTER_WIDTH, PDF::PDF_LETTER_HEIGHT,
          PDF::Options{.use_compression = COMPRESS_TEST_PDF});

  PDF::Info info;
  info.creator = "pdf_test.cc";
  info.producer = "Tom 7";
  info.title = "It is a test";
  info.author = "None";
  info.subject = "No subject";
  info.date = "6 Apr 2025";
  pdf.SetInfo(info);

  std::string warning = pdf.AddSVG(SVG::ParseOrDie(WARNING_SVG));

  std::string pasement_name = pdf.AddTTF("fonts/DFXPasement9px.ttf",
                                         PDF::FontEncoding::UNICODE);

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

    CHECK(pdf.AddFilledPolygon(Star(500, 150, 25, 75, 9),
                               PDF_RGB(0x00, 0xFF, 0x20),
                               PDF_RGB(0xAA, 0x70, 0),
                               2.0f));

    CHECK(pdf.AddFilledPolygon(Star(500, 150, 15, 20, 9),
                               PDF_RGB(0xAA, 0x00, 0x00)));

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
    pdf.AddSpacedLine({{"Over", -16.0f}, {"lapping", 100.0f}},
                      16,
                      36, 200, PDF_RGB(80, 0, 0));


    pdf.DrawSVG(warning,
                PDF_INCH_TO_POINT(4.6),
                PDF_INCH_TO_POINT(3.0),
                PDF_INCH_TO_POINT(0.5),
                PDF_INCH_TO_POINT(0.5));
  }

  {
    printf(AWHITE("Kerning page") ".\n");
    [[maybe_unused]]
    PDF::Page *page = pdf.AppendNewPage();

    float ypos = PDF::PDF_LETTER_HEIGHT - 72 - 36;

    for (const char *filename : FONTS) {
      if (Util::ExistsFile(filename)) {
        float ycolumn2 = ypos;

        const std::string embedded_name = pdf.AddTTF(filename);
        const PDF::Font *embedded = pdf.GetFontByName(embedded_name);
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

        CompareKern("To Await Is To Worry.", 24);
        CompareKern("BoVeX.", 24);
        CompareKern("Mr. Jock, T.V. Quiz Ph.D., bags few lynx.", 12);

        ypos -= 96.0f;

        // Now some narrow text.
        // Built-in-wrapping.
        const char *text =
          "The story: These large sheets of polarizing "
          "material were made specifically for the optical industry. The "
          "manufacturer who made them maintains very high quality "
          "standards. Whenever defects, even minute ones, appear in any "
          "portion of a sheet, the entire piece is rejected. Thus, these "
          "are rejects, because of very minor imperfections, which we can "
          "offer at a fraction of their normal price. The polarizing "
          "material is sandwiched between two clear sheets of butyrate "
          "plastic. It is rigid, easily cut with scissors, and measures "
          ".030\" thick.";
        constexpr double width = 72.0 * 1.5;
        constexpr double size = 9.0f;

        const double left = 4.9 * 72;
        const double padding = 0.10 * 72;
        const double gutter = 0.25 * 72;
        const double right = left + width + gutter;

        auto Rules = [&](float x) {
            pdf.AddLine(x - padding, ycolumn2 + 14,
                        x - padding, 150,
                        1.0f, PDF_RGB(180, 180, 180));

            pdf.AddLine(x + width + padding, ycolumn2 + 14,
                        x + width + padding, 150,
                        1.0f, PDF_RGB(180, 180, 180));

          };

        Rules(left);
        Rules(right);

        pdf.AddTextWrap(text, size,
                        left, ycolumn2,
                        0.0f,
                        PDF_RGB(0, 0, 0),
                        width, PDF::PDF_ALIGN_LEFT);

        std::vector<SpacedLine> spaced =
          pdf.SpaceLines(text, width / size, embedded);
        // TODO: Function that applies standard leading!
        float yy = ycolumn2;
        const float xx = right;
        for (const auto &line : spaced) {
          CHECK(pdf.AddSpacedLine(line, size, xx, yy,
                                  PDF_RGB(0, 0, 0),
                                  0.0f, page)) << filename;
          yy -= size;
        }

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


    // Second column.
    static constexpr float QR_SIZE = BARCODE_WIDTH * 2.0;
    y = PDF::PDF_LETTER_HEIGHT - GAP - QR_SIZE;

    pdf.AddFilledRectangle(256, y, QR_SIZE, QR_SIZE, 0,
                           PDF_RGB(245, 245, 255),
                           PDF::COLOR_NONE);
    CHECK(pdf.AddQRCode(256, y, QR_SIZE, "HTTP:\\\\SIGBOVIK.ORG",
                        PDF_RGB(40, 0, 0)));
    y -= QR_SIZE + GAP;

    static constexpr int qw = 29;
    const float qp = QR_SIZE / (double)qw;
    const float rx = 256 + 2 * qp;
    const float ry = y + 2 * qp;

    CHECK(pdf.AddQRCode(256, y, QR_SIZE,
                        "pls stop invading my personal space"));

    pdf.AddFilledRectangle(rx, ry, qp * 3, qp * 3, 0,
                           PDF_RGB(255, 255, 255),
                           PDF::COLOR_NONE);
    CHECK(pdf.AddQRCode(rx, ry, qp * 3, "so nosy :("));

    y -= QR_SIZE + GAP;
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
    CHECK(pdf.AddImageRGB(
              100, PDF::PDF_LETTER_HEIGHT - 72 * 3, -1.0, 72 * 2,
              rand, PDF::CompressionType::PNG, page)) << "Error: " <<
      pdf.GetErr();

    ImageRGB rand2 = RandomRGB(&rc, 384, 256);
    CHECK(pdf.AddImageRGB(
              320, PDF::PDF_LETTER_HEIGHT - 72 * 3, -1.0, 72 * 2,
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
              "!@#$%^&*()-=  ♥♥ok",
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

static void MakeMinimalPDF() {
  printf("Create PDF object.\n");
  PDF::Options opt;
  opt.use_compression = false;
  PDF pdf(PDF::PDF_LETTER_WIDTH,
          PDF::PDF_LETTER_HEIGHT,
          opt);

  [[maybe_unused]]
  PDF::Page *page = pdf.AppendNewPage();
  PDF::Info info;
  info.creator = "pdf_test.cc";
  info.producer = "Tom 7";
  info.title = "Minimal PDF";
  info.author = "None";
  info.subject = "No subject";
  info.date = "6 Apr 2025";
  pdf.SetInfo(info);

  std::string pasement_name = pdf.AddTTF("fonts/DFXPasement9px.ttf",
                                         PDF::FontEncoding::UNICODE);

  pdf.SetFont(pasement_name);
  CHECK(pdf.AddText("Title of PDF",
                    72,
                    30, PDF::PDF_LETTER_HEIGHT - 72 - 36,
                    PDF_RGB(0, 0, 0)));

  pdf.Save("minimal.pdf");
  printf("Wrote " AGREEN("minimal.pdf") "\n");
}

static void SimpleUnicode() {
  printf("Create PDF object.\n");
  PDF::Options opt;
  opt.use_compression = false;
  PDF pdf(PDF::PDF_LETTER_WIDTH,
          PDF::PDF_LETTER_HEIGHT,
          opt);

  [[maybe_unused]]
  PDF::Page *page = pdf.AppendNewPage();
  PDF::Info info;
  info.creator = "pdf_test.cc";
  info.producer = "Tom 7";
  info.title = "Simple Unicode PDF";
  info.author = "None";
  info.subject = "No subject";
  info.date = "6 Apr 2025";
  pdf.SetInfo(info);

  std::string pala_name = pdf.AddTTF("pala.ttf",
                                     PDF::FontEncoding::UNICODE);

  int yy = PDF::PDF_LETTER_HEIGHT - 72 - 36;
  pdf.SetFont(pala_name);
  CHECK(pdf.AddText("High-way ! \"OK\"?",
                    36,
                    30, yy,
                    PDF_RGB(0, 0, 0)));
  yy -= 36 + 12;

  pdf.SetFont(pala_name);
  CHECK(pdf.AddText("It’s “simple.” ... OK?",
                    36,
                    30, yy,
                    PDF_RGB(0, 0, 0)));
  yy -= 36 + 12;

  pdf.SetFont(PDF::BuiltInFont::TIMES_ROMAN);
  pdf.AddText("It’s “simple.” ... OK?",
              36,
              30, yy,
              PDF_RGB(0, 0, 0));
  yy -= 36 + 12;

  pdf.SetFont(PDF::BuiltInFont::HELVETICA_OBLIQUE);
  pdf.AddText("It’s “simple.” ... OK?",
              36,
              30, yy,
              PDF_RGB(0, 0, 0));
  yy -= 36 + 12;

  pdf.Save("simple.pdf");
  printf("Wrote " AGREEN("simple.pdf") "\n");
}


int main(int argc, char **argv) {
  ANSI::Init();

  BuiltInWidths();
  SpaceLine();

  MakeMinimalPDF();
  SimpleUnicode();
  MakeSimplePDF();

  printf("OK\n");
  return 0;
}
