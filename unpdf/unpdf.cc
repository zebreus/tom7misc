#include <iostream>
#include <podofo/podofo.h>

#include "base/logging.h"
#include "re2/re2.h"

using namespace PoDoFo;
using namespace std;

#if 0
    /*
     * Set some additional information on the PDF file.
     */
    document.GetInfo()->SetCreator ( PdfString("examplahelloworld - A PoDoFo test application") );
    document.GetInfo()->SetAuthor  ( PdfString("Dominik Seichter") );
    document.GetInfo()->SetTitle   ( PdfString("Hello World") );
    document.GetInfo()->SetSubject ( PdfString("Testing the PoDoFo PDF Library") );
    document.GetInfo()->SetKeywords( PdfString("Test;PDF;Hello World;") );
#endif

static bool MatchesForRemoval(const string &contents) {
  return contents.find("/DeviceRGB cs") != string::npos ||
	contents.find("/Im1 Do") != string::npos ||
	contents.find("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">") != string::npos;
}

static string ProcessWithRegex(string contents) {
  // Line numbers
  RE2::GlobalReplace(&contents,
					 " (-?[0-9.]+) (-?[0-9.]+) Td \\[\\([0-9]+\\)\\]TJ\n"
					 "0 g 0 G\n"
					 "0 g 0 G\n"
					 ,
					 " \\1 \\2 Td [()]TJ\n"
					 "0 g 0 G\n"
					 "0 g 0 G\n");

  return contents;
}

static void UnPdf(const string &infile,
				  const string &outfile) {
  unique_ptr<PdfMemDocument> doc(new PdfMemDocument(infile.c_str()));
  
  for (PdfObject *obj : doc->GetObjects()) {
	// This is basically just a pair of its object and generation number.
	/*
	const PdfReference &ref = obj->Reference();
	printf("Obj %d %d%s\n", ref.ObjectNumber(),
		   ref.GenerationNumber(),
		   obj->HasStream() ? " (stream) " : "");
	*/
	
	
	if (obj->HasStream()) {
	  PdfMemStream *stream = dynamic_cast<PdfMemStream*>(obj->GetStream());
	  CHECK(stream != nullptr);

	  stream->Uncompress();

	  // Check if it matches.
	  std::string contents;
	  contents.resize(stream->GetLength());
	  memcpy(contents.data(), stream->Get(), contents.size());

	  if (MatchesForRemoval(contents)) {
		// Filter it out by making the stream blank.
		stream->Set("", 0);
	  } else {
		const string new_contents = ProcessWithRegex(contents);
		stream->Set(new_contents.c_str(), new_contents.size());
		// everything just ends up blank??
		// stream->FlateCompress();
	  }

	  // Recompress?
	}
  }
  
  {
	PdfWriter writer(&doc->GetObjects(),
					 new PdfObject(*doc->GetTrailer()));
	writer.SetWriteMode(ePdfWriteMode_Clean);
	writer.Write(outfile.c_str());
  }
}

int main(int argc, char **argv) {
  CHECK(argc == 3) << "unpdf.exe infile.pdf outfile.pdf\n";
  string infile = argv[1];
  string outfile = argv[2];  
  
  try {
	UnPdf(infile, outfile);
	return 0;
  } catch (const PdfError &eCode) {
	fprintf(stderr, "Uncaught exception\n");
	return eCode.GetError();
  }
  
}

