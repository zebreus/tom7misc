
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bytecode.h"
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
#include "periodically.h"
#include "achievements.h"

struct BovexExecution : public bc::Execution {
  explicit BovexExecution(const bc::Program &pgm,
                          PDFDocument *pdf_document,
                          Rephrasing *rephrasing) :
    bc::Execution(pgm),
    pdf_document(pdf_document),
    rephrasing(rephrasing),
    save_rephrasing_per(60.0) {

  }

  std::map<int, std::vector<DocTree>> pages;
  double total_badness = 0.125;

  Document *DocumentHook() override { return pdf_document; }
  Rephrasing *RephrasingHook() override {
    if (save_rephrasing_per.ShouldRun()) {
      rephrasing->Save();
    }
    return rephrasing;
  }

  // The defaults for these are fine.
  // virtual void FailHook(const std::string &msg);
  // virtual void ConsoleHook(const std::string &msg);

  void OutputLayoutHook(int page_idx, const bc::Value *v) override {
    // printf(AGREEN("OUTPUT") "!\n");
    pages[page_idx].push_back(ValueToDocTree(v));
  }

  void EmitBadnessHook(double badness) override {
    total_badness += badness;
  }

  std::map<int, DocTree> ExtractDocument() {
    std::map<int, DocTree> out;
    for (auto &[page_idx, docs] : pages) {
      out[page_idx] = JoinDocs(std::move(docs));
    }
    pages.clear();
    return out;
  }

  PDFDocument *pdf_document = nullptr;
  Rephrasing *rephrasing = nullptr;

  // Periodically save the rephrasing database, so that even if we
  // kill the process, we don't lose work. (Unless you kill it during
  // the write to disk. Then you can lose everything!)
  Periodically save_rephrasing_per;
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
  compiler.SetVerbose(verbose);

  CHECK(!output_file.empty()) << "Need to explicitly specify an "
    "output file with -o output.pdf.\n";

  CHECK(leftover.size() == 1) << "Expected exactly one .bovex file "
    "on the command-line.";

  bc::Program pgm = compiler.Compile(leftover[0]);

  std::string rephrase_db = leftover[0];
  Util::TryStripSuffix(".bovex", &rephrase_db);
  rephrase_db += ".rdb";

  std::unique_ptr<Rephrasing> rephrasing(Rephrasing::Create(rephrase_db));
  CHECK(rephrasing.get() != nullptr);

  // TODO: In a loop!

  // Dimensions should be settable from within program!
  PDFDocument pdf_document(PDF::PDF_LETTER_WIDTH, PDF::PDF_LETTER_HEIGHT);
  BovexExecution execution(pgm, &pdf_document, rephrasing.get());
  BovexExecution::State state = execution.Start();

  if (verbose > 0) {
    printf(AWHITE("Running") ".\n");
    fflush(stdout);
  }
  execution.RunToCompletion(&state);

  std::map<int, DocTree> pages = execution.ExtractDocument();
  // Measure final badness?
  printf("Total badness: " ARED("%.11g") "\n", execution.total_badness);

  rephrasing->Save();

  if (verbose > 1) {
    printf(AWHITE("The document") ":\n");
    for (const auto &[page_idx, doc] : pages) {
      printf("==== PAGE %d ====\n", page_idx);
      DebugPrintDocTree(doc);
    }
  }

  pdf_document.GeneratePDF(output_file, pages);

  Achievements::Get().Achieve("Gen AI", "Generated a PDF!");

  const auto &[data_bytes, total_insts] = ProgramSize(pgm);
  printf("Program size: " ABLUE("%lld") " bytes data, "
         APURPLE("%lld") " insts.\n", data_bytes, total_insts);
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

