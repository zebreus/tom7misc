#include <iostream>
#include <podofo/podofo.h>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "re2/re2.h"

using namespace PoDoFo;
using namespace std;

// Executive 7x10
constexpr int WIDTH = 7 * 72;
constexpr int HEIGHT = 10 * 72;

static string RectString(PdfRect r) {
  return StringPrintf("bot %f left %f width %f height %f",
					  r.GetBottom(),
					  r.GetLeft(),
					  r.GetWidth(),
					  r.GetHeight());
}

static void PageSize(const string &infile,
				  const string &outfile) {
  unique_ptr<PdfMemDocument> doc(new PdfMemDocument(infile.c_str()));

  int num_pages = doc->GetPageCount();
  for (int p = 0; p < num_pages; p++) {
	PdfPage *page = doc->GetPage(p);

	PdfRect old = page->GetPageSize();
	printf("old page %s\n", RectString(old).c_str());

	PdfRect oldtrim = page->GetTrimBox();
	printf("old trim %s\n", RectString(oldtrim).c_str());
	
	// US Letter
	page->SetPageWidth(WIDTH);
	page->SetPageHeight(HEIGHT);
	
	page->SetRotation(0);

	PdfRect newtrim(0.0, 0.0, WIDTH, HEIGHT);
	page->SetTrimBox(newtrim);
  }
  
  if (true) {
	PdfWriter writer(&doc->GetObjects(),
					 new PdfObject(*doc->GetTrailer()));
	writer.SetWriteMode(ePdfWriteMode_Clean);
	writer.Write(outfile.c_str());
  }
}

int main(int argc, char **argv) {
  CHECK(argc == 3) << "pagesize.exe infile.pdf outfile.pdf\n";
  string infile = argv[1];
  string outfile = argv[2];  

  PdfRect letter = PdfPage::CreateStandardPageSize(ePdfPageSize_Letter);
  printf("LETTER: bot %f left %f width %f height %f\n",
		 letter.GetBottom(),
		 letter.GetLeft(),
		 letter.GetWidth(),
		 letter.GetHeight());

  try {
	PageSize(infile, outfile);
	return 0;
  } catch (const PdfError &eCode) {
	fprintf(stderr, "Uncaught exception\n");
	return eCode.GetError();
  }
  
}

