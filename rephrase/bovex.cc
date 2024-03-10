
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "compiler.h"
#include "frontend.h"
#include "execution.h"

#include "ansi.h"
#include "base/logging.h"
#include "document.h"
#include "pdf-document.h"
#include "pdf.h"
#include "rephrasing.h"
#include "timer.h"
#include "util.h"

static void GeneratePDF(const std::string &filename) {
  PDF::Info info;
  sprintf(info.creator, "bovex.cc");
  sprintf(info.producer, "Tom 7");
  sprintf(info.title, "It is a test");
  sprintf(info.author, "None");
  sprintf(info.author, "No subject");
  sprintf(info.date, "deleteme");

  PDF pdf(PDF::PDF_LETTER_WIDTH, PDF::PDF_LETTER_HEIGHT, info);

  [[maybe_unused]]
  PDF::Page *page = pdf.AppendNewPage();

  pdf.SetFont(PDF::TIMES_ROMAN);
  CHECK(pdf.AddTextWrap(
            "This is just test output. I need to make BoVeX "
            "actually generate a PDF that depends on your input!",
            20,
            36, PDF::PDF_LETTER_HEIGHT - 72 - 36 - 48,
            0.0f,
            PDF_RGB(0, 0, 0),
            PDF_INCH_TO_POINT(3.4f),
            PDF::PDF_ALIGN_JUSTIFY));

  pdf.Save(filename);
  printf("Wrote %s\n", filename.c_str());
}

struct BovexExecution : public bc::Execution {
  explicit BovexExecution(const bc::Program &pgm,
                          PDFDocument *pdf_document) :
    bc::Execution(pgm),
    pdf_document(pdf_document) {

  }

  std::vector<DocTree> docs;

  // The defaults for these are fine.
  // virtual void FailHook(const std::string &msg);
  // virtual void ConsoleHook(const std::string &msg);

  void OutputLayoutHook(const bc::Value *v) override {
    docs.push_back(ValueToDocTree(v));
  }

  DocTree ExtractDocument() {
    DocTree ret = JoinDocs(std::move(docs));
    docs.clear();
    return ret;
  }

  PDFDocument *pdf_document;
};

static int Bovex(const std::vector<std::string> &args) {
  Timer timer;
  Compiler compiler;

  int verbose = 0;

  std::string output_file;
  std::vector<std::string> leftover;
  for (int i = 0; i < (int)args.size(); i++) {
    std::string_view arg(args[i]);
    if (Util::TryStripPrefix("-v", &arg)) {
      verbose++;
      printf("Verbosity: %d\n", verbose);
    } else if (Util::TryStripPrefix("-I", &arg)) {
      // Then this is -Istdlib
      if (!arg.empty()) {
        compiler.frontend.AddIncludePath((std::string)arg);
      } else {
        CHECK(i + 1 < (int)args.size()) << "Trailing -I argument";
        i++;
        compiler.frontend.AddIncludePath(args[i]);
      }
    } else if (arg == "-o") {
      CHECK(i + 1 < (int)args.size()) << "Trailing -o argument";
      i++;
      output_file = args[i];
    } else {
      leftover.push_back((std::string)arg);
    }
  }

  if (verbose > 0) {
    printf(AWHITE("Remaining args:"));
    for (const std::string &arg : leftover) {
      printf(" " AGREY("[") "%s" AGREY("]"), arg.c_str());
    }
    printf("\n");
  }

  compiler.frontend.SetVerbose(verbose);

  CHECK(!output_file.empty()) << "Need to explicitly specify an "
    "output file with -o output.pdf.\n";

  CHECK(leftover.size() == 1) << "Expected exactly one .bovex file "
    "on the command-line.";

  bc::Program pgm = compiler.Compile(leftover[0]);

  Rephrasing rephrasing;

  // TODO: In a loop!

  // Dimensions should be settable from within program!
  PDFDocument pdf_document(PDF::PDF_LETTER_WIDTH, PDF::PDF_LETTER_HEIGHT);
  BovexExecution execution(pgm, &pdf_document);
  BovexExecution::State state = execution.Start();
  execution.RunToCompletion(&state);

  DocTree doc = execution.ExtractDocument();

  printf(AWHITE("The document") ":\n");
  DebugPrintDocTree(doc);

  // XXX, using pdf_document
  GeneratePDF(output_file);

  printf("Finished in %s\n", ANSI::Time(timer.Seconds()).c_str());
  return 0;
}

int main(int argc, char **argv) {
  ANSI::Init();
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.emplace_back(argv[i]);
  }

  return Bovex(args);
}

