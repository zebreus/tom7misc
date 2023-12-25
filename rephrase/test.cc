#include <podofo/podofo.h>

int main() {
    // Create a new PDF document
    PoDoFo::PdfMemDocument document;

    // Load the TrueType font file
    PoDoFo::PdfFont* font = document.CreateFont("FixederSys2x.ttf");

    // Create a page in the document
    PoDoFo::PdfPage* page =
      document.CreatePage(PoDoFo::PdfPage::CreateStandardPageSize(
                              PoDoFo::ePdfPageSize_A4));

    // Create a text object and set the font
    PoDoFo::PdfPainter painter;
    painter.SetPage(page);
    painter.SetFont(font);

    // Add the text to the page
    painter.DrawText(50, 700,
                     "This text is using an embedded TrueType font.");

    // Save the PDF to a file
    document.Write("embedded_font_example.pdf");

    return 0;
}
