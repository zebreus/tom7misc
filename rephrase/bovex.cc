
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "achievements.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bc.h"
#include "compiler.h"
#include "document.h"
#include "execution.h"
#include "frontend.h"
#include "movie-document.h"
#include "opt/opt-seq.h"
#include "pdf-document.h"
#include "periodically.h"
#include "rephrasing.h"
#include "talk-document.h"
#include "timer.h"
#include "util.h"

enum class OutputType {
  PDF,
  TALK,
  MOVIE,
};

namespace {
struct BovexOpt {
  std::vector<std::tuple<std::string, double, double, double>> vars;
  std::unordered_map<std::string, int> var_nums;
  BovexOpt(int verbose) : verbose(verbose) {}

  bool Continue() const {
    return seq.get() != nullptr;
  }

  void Next() {
    // No optimization vars.
    if (vars.empty()) {
      args = {};
      return;
    }

    // Sequence was resized, so we need to start over.
    if (seq.get() == nullptr) {
      std::vector<std::pair<double, double>> bounds;
      bounds.reserve(vars.size());
      for (const auto &[v, lo, st, hi] : vars) {
        bounds.emplace_back(lo, hi);
      }
      seq.reset(new OptSeq(std::move(bounds)));
    }

    args = seq->Next();
  }

  void Result(double r) {
    if (seq.get() != nullptr) {
      CHECK(seq->size() == args.size());
      seq->Result(r);
    }
  }

  double GetValue(const std::string &var,
                  double low, double start, double high) {
    auto it = var_nums.find(var);
    if (it == var_nums.end()) {
      if (verbose > 0) {
        Print("New optimization var " ACYAN("{}") "\n", var);
      }
      var_nums[var] = (int)vars.size();
      vars.emplace_back(var, low, start, high);
      args.push_back(start);
      return start;

    } else {
      int idx = it->second;
      CHECK(idx >= 0 && idx < (int)args.size());
      return args[idx];
    }
  }

  int verbose = 0;
  std::unique_ptr<OptSeq> seq;
  std::vector<double> args;
};

struct BovexExecution : public bc::Execution {
  explicit BovexExecution(int verbose,
                          const std::vector<std::string> &include_paths,
                          const bc::Program &pgm,
                          Document *document,
                          Rephrasing *rephrasing,
                          BovexOpt *opt) :
    bc::Execution(pgm),
    verbose(verbose),
    include_paths(include_paths),
    document(document),
    rephrasing(rephrasing),
    opt(opt),
    // Save periodically, but not immediately!
    save_rephrasing_per(60.0, false) {
    opt->Next();
  }

  Document *DocumentHook() override { return document; }
  Rephrasing *RephrasingHook() override {
    if (rephrasing->IsDirty() && save_rephrasing_per.ShouldRun()) {
      rephrasing->Save();
    }
    did_rephrase = true;
    return rephrasing;
  }

  // The defaults for these are fine.
  // virtual void FailHook(const std::string &msg);
  // virtual void ConsoleHook(const std::string &msg);

  void OutputLayoutHook(int page_idx, int frame_idx,
                        const bc::Value *v) override {
    // Print(AGREEN("OUTPUT") "!\n");
    pages[std::make_pair(page_idx, frame_idx)].push_back(ValueToDocTree(v));
  }

  void EmitBadnessHook(double badness) override {
    total_badness += badness;
  }

  std::map<int, std::map<int, DocTree>> ExtractPages() {
    std::map<int, std::map<int, DocTree>> out;
    for (auto &[idx, docs] : pages) {
      const auto &[page_idx, frame_idx] = idx;
      out[page_idx][frame_idx] = JoinDocs(std::move(docs));
    }
    pages.clear();
    return out;
  }

  double OptimizationHook(const std::string &name,
                          double low,
                          double start,
                          double high) override {
    return opt->GetValue(name, low, start, high);
  }

  std::string FindFileHook(std::string_view filename) override {
    // We take the first existing file.
    for (const std::string &ipath : include_paths) {
      std::string f = Util::DirPlus(ipath, filename);
      if (Util::ExistsFile(f)) {
        Print("Found " AGREEN("{}") " in " ACYAN("{}") "\n",
              filename, f);
        return f;
      }
    }
    if (verbose > 0) {
      Print("File " ARED("{}") " not found in any include path:\n",
            filename);
      for (const std::string &ipath : include_paths) {
        Print("  " AGREY("{}") "\n", ipath);
      }
    }
    return std::string(filename);
  }

  int verbose = 0;

  std::map<std::pair<int, int>, std::vector<DocTree>> pages;
  double total_badness = 0.125;

  bool did_rephrase = false;

  std::vector<std::string> include_paths;

  // Not owned!
  Document *document = nullptr;
  Rephrasing *rephrasing = nullptr;
  BovexOpt *opt = nullptr;

  // Periodically save the rephrasing database, so that even if we
  // kill the process, we don't lose work. (Unless you kill it during
  // the write to disk. Then you can lose everything!)
  Periodically save_rephrasing_per;
};
}  // namespace

static int Bovex(const std::string &program_dir,
                 const std::vector<std::string> &args) {
  Timer timer;
  Compiler compiler;

  int verbose = 0;

  // Always include the BoVeX program directory.
  compiler.frontend.AddIncludePath(program_dir);

  std::string output_file;
  std::vector<std::string> leftover;
  for (int i = 0; i < (int)args.size(); i++) {
    std::string_view arg(args[i]);
    if (Util::TryStripPrefix("-v", &arg)) {
      verbose++;
      Print("Verbosity: {}\n", verbose);
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
    Print(AWHITE("Remaining args:"));
    for (const std::string &arg : leftover) {
      Print(" " AGREY("[") "{}" AGREY("]"), arg);
    }
    Print("\n");
  }

  if (verbose > 16) {
    Achievements::Get().Achieve("Logorrhea",
                                "Have more than 16 verbosity enabled.");
  }

  compiler.frontend.SetVerbose(verbose);
  compiler.SetVerbose(verbose);

  CHECK(!output_file.empty()) << "Need to explicitly specify an "
    "output file with -o output.pdf.\n";

  std::string_view ext = Util::FileExtOf(output_file);
  std::string_view output_base = Util::FileBaseOf(output_file);

  const OutputType output_type = [&ext]() {
      if (ext == "pdf") return OutputType::PDF;
      if (ext == "talk") return OutputType::TALK;
      if (ext == "mov") return OutputType::MOVIE;
      LOG(FATAL) << "Unsupported output type: " << ext;
    }();

  CHECK(leftover.size() == 1) << "Expected exactly one .bovex file "
    "on the command-line.";

  bc::Program pgm = compiler.Compile(leftover[0]);

  std::string rephrase_db = leftover[0];
  Util::TryStripSuffix(".bovex", &rephrase_db);
  rephrase_db += ".rdb";

  std::unique_ptr<Rephrasing> rephrasing(Rephrasing::Create(rephrase_db));
  CHECK(rephrasing.get() != nullptr);

  std::optional<double> best_badness;
  std::optional<std::map<int, std::map<int, DocTree>>> best_pages;
  std::optional<std::unique_ptr<Document>> best_document;
  bool did_rephrase = false;

  BovexOpt opt(verbose);

  int64_t total_collected = 0;
  for (;;) {

    std::unique_ptr<Document> document = [program_dir, output_type]() ->
      std::unique_ptr<Document> {
      switch (output_type) {
      case OutputType::PDF:
        return std::make_unique<PDFDocument>(program_dir);
      case OutputType::TALK:
        return std::make_unique<TalkDocument>(program_dir);
      case OutputType::MOVIE:
        return std::make_unique<MovieDocument>(program_dir);
      }
      LOG(FATAL) << "Unimplemented document type";
      return std::unique_ptr<Document>(nullptr);
    }();

    BovexExecution execution(verbose,
                             compiler.frontend.GetIncludePaths(),
                             pgm, document.get(), rephrasing.get(),
                             &opt);
    BovexExecution::State state = execution.Start();

    if (verbose > 0) {
      Print(AWHITE("Running") ".\n");
      fflush(stdout);
    }
    Timer exec_timer;
    execution.RunToCompletion(&state);
    const double exec_sec = exec_timer.Seconds();
    total_collected += state.collected;

    if (verbose > 0) {
      Print(AWHITE("Executed") " in {}.\n", ANSI::Time(exec_sec));
    }

    did_rephrase = did_rephrase || execution.did_rephrase;

    std::map<int, std::map<int, DocTree>> pages = execution.ExtractPages();

    // Measure final badness.
    const double total_badness = execution.total_badness;
    Print("Total badness: " ARED("{:.11g}") "\n", total_badness);
    opt.Result(total_badness);

    if (!best_badness.has_value() || total_badness < best_badness.value()) {
      best_badness = total_badness;
      best_pages.emplace(std::move(pages));
      best_document.emplace(std::move(document));
    }

    if (!opt.Continue()) {
      break;
    }
  }

  CHECK(best_badness.has_value() && best_pages.has_value() &&
        best_document.has_value());
  const double total_badness = best_badness.value();
  const std::map<int, std::map<int, DocTree>> &pages = best_pages.value();
  Document *document = best_document.value().get();

  if (pages.size() >= 5 && total_badness < 1000.0 * pages.size()) {
    Achievements::Get().Achieve("Not bad",
                                "Generate a document that's at least 5 pages "
                                "with less than 1000 badness per page.");
  }

  if (total_badness >= +1e100) {
    Achievements::Get().Achieve("SUPERBAD",
                                "Generate a document with a googol badness.");
  }

  if (std::isnan(total_badness)) {
    Achievements::Get().Achieve("IEEEeeee!",
                                "Generate a document with NaN badness.");
  }

  rephrasing->Save();

  if (verbose > 1) {
    Print(AWHITE("The document") ":\n");
    for (const auto &[page_idx, anim] : pages) {
      for (const auto &[frame_idx, doc] : anim) {
        Print("==== PAGE {} FRAME {} ====\n", page_idx, frame_idx);
        DebugPrintDocTree(doc);
      }
    }
  }

  document->GenerateOutput(output_base, pages);

  if (did_rephrase) {
    Achievements::Get().Achieve("Gen AI",
                                "Generated a document that used the rephrase "
                                "functionality.");
  }

  const auto &[data_bytes, total_insts] = ProgramSize(pgm);
  Print("Program size: " ABLUE("{}") " bytes data, "
        APURPLE("{}") " insts.\n", data_bytes, total_insts);
  Print("Collected " AWHITE("{}") " total cells\n",
        total_collected);
  Print("Finished in {}\n", ANSI::Time(timer.Seconds()));
  return 0;
}

int main(int argc, char **argv) {
  ANSI::Init();
  std::vector<std::string> args;
  for (int i = 1; i < argc; i++) {
    args.emplace_back(argv[i]);
  }

  return Bovex(Util::BinaryDir(argv[0]), args);
}

