#include <podofo/podofo.h>

int main() {
  // Create a new PDF document
  PoDoFo::PdfMemDocument document;

  // Load the TrueType font file
  PoDoFo::PdfFont &font = document.GetFonts().
    GetOrCreateFont("FixederSys2x.ttf");

  // Create a page in the document
  PoDoFo::PdfPage &page =
    document.GetPages().CreatePage(
        PoDoFo::PdfPage::CreateStandardPageSize(
            PoDoFo::PdfPageSize::Letter));

  // Create a text object and set the font
  PoDoFo::PdfPainter painter;
  painter.SetCanvas(page);
  painter.TextState.SetFont(font, 24.0);

  // Add the text to the page
  painter.DrawText("This text is using an embedded TrueType font.",
                   50, 700);
  painter.FinishDrawing();

  // Save the PDF to a file
  document.Save("embedded_font_example.pdf");

  return 0;
}
