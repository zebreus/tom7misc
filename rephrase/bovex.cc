
#include <string>
#include <string_view>
#include <chrono>
#include <format>

#include "compiler.h"
#include "frontend.h"
#include "il.h"
#include "execution.h"

#include "util.h"
#include "base/logging.h"
#include "ansi.h"
#include "pdf.h"
#include "timer.h"


static std::string DateTimeStamp() {
  return std::format("{:%Y-%m-%d %H:%M:%S}",
                     std::chrono::system_clock::now());
}

static void GeneratePDF(const std::string &filename) {
  PDF::Info info;
  sprintf(info.creator, "bovex.cc");
  sprintf(info.producer, "Tom 7");
  sprintf(info.title, "It is a test");
  sprintf(info.author, "None");
  sprintf(info.author, "No subject");

  sprintf(info.date, "%s", DateTimeStamp().c_str());

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


static int Bovex(const std::vector<std::string> &args) {
  Timer timer;
  Compiler compiler;
  // Parse command-line arguments.
  printf("Date: %s\n", DateTimeStamp().c_str());

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

  bc::Execution execution(pgm);
  bc::Execution::State state = execution.Start();
  execution.RunToCompletion(&state);

  // XXX
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
