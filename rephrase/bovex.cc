
#include <string>
#include <string_view>
#include <chrono>
#include <format>
#include <unordered_set>
#include <functional>

#include "compiler.h"
#include "frontend.h"
#include "execution.h"

#include "timer.h"
#include "util.h"
#include "base/logging.h"
#include "ansi.h"
#include "pdf.h"
#include "base/stringprintf.h"

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

struct AttrVal {
  using t = std::variant<
    BigInt,
    std::string,
    uint64_t,
    double
    >;
  t v;
};

static std::string AttrValString(const AttrVal &val) {
  if (const std::string *s = std::get_if<std::string>(&val.v)) {
    return StringPrintf("\"%s\"", s->c_str());
  } else if (const uint64_t *u = std::get_if<uint64_t>(&val.v)) {
    return StringPrintf("%llu", *u);
  } else if (const double *d = std::get_if<double>(&val.v)) {
    return StringPrintf("%.17g", *d);
  } else if (const BigInt *b = std::get_if<BigInt>(&val.v)) {
    return b->ToString();
  } else {
    return "??INVALID??";
  }
}

struct Document {
  std::unordered_map<std::string, AttrVal> attrs;
  std::string text;
  std::vector<std::shared_ptr<Document>> children;
};

static AttrVal ConvertAttrVal(const std::string &field, const bc::Value &val) {
  if (const std::string *s = std::get_if<std::string>(&val.v)) {
    return AttrVal{.v = *s};
  } else if (const uint64_t *u = std::get_if<uint64_t>(&val.v)) {
    return AttrVal{.v = *u};
  } else if (const double *d = std::get_if<double>(&val.v)) {
    return AttrVal{.v = *d};
  } else if (const BigInt *b = std::get_if<BigInt>(&val.v)) {
    return AttrVal{.v = *b};
  } else {
    LOG(FATAL) << "Unsupported attribute type in layout. It must be "
      "BigInt, string, uint64_t, or double. The field was: " << field;
  }
}

// We should need to copy the value, since the program
// could modify it in the heap, or garbage collect it.
// It also gives us a chance to clean up the format a little.
static Document ValueToDoc(const bc::Value *v) {
  std::unordered_set<const bc::Value *> seen;
  std::function<Document(const bc::Value *)> Rec =
      [&seen, &Rec](const bc::Value *v) -> Document {
        CHECK(!seen.contains(v)) << "Cycle in document! "
          "What is this, some kind of joke!?";
        seen.insert(v);

        using map_type = std::unordered_map<std::string, bc::Value *>;
        using vec_type = std::vector<bc::Value *>;

        Document doc;
        if (const std::string *s = std::get_if<std::string>(&v->v)) {
          doc.text = *s;
          return doc;
        } else if (const map_type *obj = std::get_if<map_type>(&v->v)) {
          auto ait = obj->find(bc::NODE_ATTRS_LABEL);
          CHECK(ait != obj->end()) << "Nodes always have an attribute object, "
            "even if it is empty.";
          const map_type *aobj = std::get_if<map_type>(&ait->second->v);
          CHECK(aobj != nullptr) << "In a node, the attribute field is always "
            "an object, even if it is empty.";

          auto cit = obj->find(bc::NODE_CHILDREN_LABEL);
          CHECK(cit != obj->end()) << "Nodes always have a children vector, "
            "even if it is empty.";
          const vec_type *cvec = std::get_if<vec_type>(&cit->second->v);
          CHECK(cvec != nullptr) << "In a node, the children field is always "
            "a vector.";

          for (const auto &[k, v] : *aobj) {
            doc.attrs[k] = ConvertAttrVal(k, *v);
          }

          for (const bc::Value *v : *cvec) {
            doc.children.emplace_back(std::make_shared<Document>(Rec(v)));
          }

          return doc;
        } else {
          LOG(FATAL) << "Bug: Layout values should be represented as either "
            "strings or maps (objects).";
          return doc;
        }
      };
  return Rec(v);
}

struct BovexExecution : public bc::Execution {
  using bc::Execution::Execution;

  std::vector<Document> docs;

  // The defaults for these are fine.
  // virtual void FailHook(const std::string &msg);
  // virtual void ConsoleHook(const std::string &msg);

  void DocumentHook(const bc::Value *v) override {
    docs.push_back(ValueToDoc(v));
  }

  Document ExtractDocument() {
    if (docs.empty()) {
      return Document();
    }

    if (docs.size() == 1) {
      Document doc = std::move(docs[0]);
      docs.clear();
      return doc;
    }

    Document doc;
    doc.children.resize(docs.size());
    for (Document &d : docs) {
      doc.children.emplace_back(std::make_shared<Document>(std::move(d)));
    }
    docs.clear();
    return doc;
  }
};

static std::string Pad(int depth) {
  return std::string(depth, ' ');
}

static void DebugPrintDoc(const Document &doc) {
  std::function<void(int, const Document &)> Rec =
    [&Rec](int depth, const Document &doc) {
      if (doc.attrs.empty() && doc.children.empty()) {

        // We should be careful about normalizing whitespace here,
        // since
        const std::string t = Util::NormalizeWhitespace(doc.text);
        // If it has no attrs or children, it is a text node (even if
        // text is also empty).
        printf("%s" AWHITE("%s") "\n",
               Pad(depth).c_str(),
               t.c_str());
      } else {
        // Then it is a regular node.
        if (!doc.text.empty()) {
          printf(ARED("INVALID TEXT \"") "%s" ARED("\""),
                 doc.text.c_str());
        }

        printf("%s" ABLUE("<"), Pad(depth).c_str());
        bool first = true;
        for (const auto &[k, v] : doc.attrs) {
          if (!first) printf(", ");
          printf(AYELLOW("%s") " = " APURPLE("%s"),
                 k.c_str(), AttrValString(v).c_str());
          first = false;
        }
        printf(ABLUE(">") "\n");
        for (const auto &child : doc.children) {
          Rec(depth + 2, *child);
        }
        printf("%s" ABLUE("</>") "\n", Pad(depth).c_str());
      }
    };
  Rec(0, doc);
}

static int Bovex(const std::vector<std::string> &args) {
  Timer timer;
  Compiler compiler;
  // Parse command-line arguments.
  // printf("Date: %s\n", DateTimeStamp().c_str());

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

  BovexExecution execution(pgm);
  BovexExecution::State state = execution.Start();
  execution.RunToCompletion(&state);

  Document doc = execution.ExtractDocument();

  printf(AWHITE("The document") ":\n");
  DebugPrintDoc(doc);

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
