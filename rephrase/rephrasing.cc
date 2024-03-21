#include "rephrasing.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>

#include "llama.h"
#include "document.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "crypt/sha256.h"
#include "timer.h"
#include "ansi.h"

#include "re2/re2.h"
#include "llm.h"
#include "models.h"
#include "util.h"
#include "html.h"
#include "color-util.h"

using Rephrasable = Rephrasing::Rephrasable;
using Candidates = LLM::Candidates;

static constexpr int VERBOSE = 1;

namespace {

static std::string ColorProb(float prob) {
  const auto &[r, g, b, a_] =
    ColorUtil::Unpack32(
        ColorUtil::LinearGradient32(ColorUtil::HEATED_TEXT, prob));

  return StringPrintf("%s%.4f%%" ANSI_RESET,
                      ANSI::ForegroundRGB(r, g, b).c_str(),
                      prob * 100.0);
}

// As we predict, we order the probabilities at each branch and
// we're mostly exploring the most probable path. This vector
// gives the depth of the sample we took (with the expectation
// that we've explored every shallower sample along this prefix),
// as well as the probability of this token.
struct Node {
  int token = 0;
  int depth = 0;
  // Total probability mass with lower depth.
  double p_skipped = 0.0;
  // Probability of this token.
  double p = 0.0;
};

// Paths often share prefixes.
// Could be better to store this as a tree explicitly. But these
// are so expensive to generate that we can also just loop over
// all of them, keeping this code simple for now.
using Path = std::vector<Node>;

// Collection of paths that have been explored. Implicitly associated
// with a specific prefix text (and model).
struct Paths {
  void AddPath(Path p) {
    vec.push_back(p);
  }

  // TODO: to/from disk

  std::vector<Path> vec;
};

// This represents a tree with one of its nodes selected.
// All of the paths go through this node.
struct PathPos {
  // invariant: cursor is less than the length of every path.
  // every path agrees up to the cursor.
  int cursor = 0;

  std::vector<Node> NodesHere() const {
    std::vector<Node> nodes;
    for (const Path &p : paths.vec) {
      CHECK(cursor < (int)p.size());
      nodes.push_back(p[cursor]);
    }
    return nodes;
  }

  // Keeping only the paths that use this token
  // at this point.
  void Advance(int token) {
    Paths new_paths;
    for (int i = 0; i < (int)paths.vec.size(); i++) {
      CHECK(cursor < (int)paths.vec[i].size());
      if (paths.vec[i][cursor].token == token) {
        new_paths.AddPath(std::move(paths.vec[i]));
      }
    }
    paths = std::move(new_paths);
  }

  Paths paths;
};

// Result from one run
struct DatabaseRow {
  // Plain-text result.
  std::string text;
  // Neither of these are normalized.
  // total probability mass skipped
  double skipped_loss;
  // total probability mass not from these tokens
  double other_loss;

  Path path;
  // True if we had normal termination and were able to
  // match it back up with the input.
  bool valid = false;
};

struct Database {
  // Indexed by database key.
  // We don't store the Rephrasable itself because we don't
  // want to serialize documents.
  std::unordered_map<std::string, std::vector<DatabaseRow>> entries;

  // TODO: to/from file
};

struct RephrasingImpl : public Rephrasing {

  RephrasingImpl(const std::string &database_file) {
    // TODO: Load from file
  }

  ~RephrasingImpl() override {}

  // Returns the next prefix to search, which consists of some
  // (maybe empty) series of forced tokens and then the *depth*
  // of the next token to sample.
  std::pair<Path, int> GetNextPrefix(const std::string &key) {
    const std::vector<DatabaseRow> &rows = db.entries[key];

    // Empty path is always an option.
    Path best_path;
    int best_next = 0;
    double best_score = 0.0;
    int best_row = -1;
    for (int row_idx = 0; row_idx < (int)rows.size(); row_idx++) {
      const DatabaseRow &row = rows[row_idx];
      double total_p = 0.0;
      const Path &path = row.path;

      if (VERBOSE > 1) {
        printf("For each node:\n");
      }
      for (int i = 0; i < (int)path.size(); i++) {
        const Node &node = path[i];
        total_p += node.p;

        if (VERBOSE > 1) {
          printf(AGREY("%d") "=%s " AGREY("d%d") " %s ~%s",
                 node.token,
                 llm->TokenString(node.token).c_str(),
                 node.depth,
                 ColorProb(path[i].p).c_str(),
                 ColorProb(total_p / (i + 1)).c_str());
        }

        // average probability of samples.
        double score = total_p / (i + 1);
        if (score > best_score) {
          best_path = path;
          best_next = node.depth + 1;
          // Not including this last node.
          best_path.resize(i);
          best_row = row_idx;
        }
      }
    }

    if (VERBOSE > 0) {
      if (best_row >= 0) {
        printf(ABLUE("Best existing text") ":\n"
               "%s\n"
               AWHITE("With score") ": %.11g\n"
               "Path length " APURPLE("%d")
               ", Next depth " APURPLE("%d") "\n",
               rows[best_row].text.c_str(),
               best_score, (int)best_path.size(), best_next);
        for (const Node &node : best_path) {
          printf("%s " AGREY("d%d") " ",
                 llm->TokenString(node.token).c_str(),
                 node.depth);
        }
        printf("\n");
      } else {
        printf("Best to start fresh.\n");
      }
    }

    return std::make_pair(std::move(best_path), best_next);
  }

  void LazyInit() {
    if (llm.get() != nullptr) return;

    #define USE_MODEL(name) \
      ContextParams cparams = Models:: name ;       \
      model_key = #name

    // This fits on the GPU, so inference is quite fast.
    USE_MODEL(LLAMA_7B_F16);

    // Best quality.
    // USE_MODEL(LLAMA_70B_F16);

    SamplerParams sparams;
    // These basically don't matter; we do our own sampling.
    sparams.type = SampleType::GREEDY;
    sparams.regex = ".*";

    Timer load_timer;
    llm.reset(new LLM(cparams, sparams));
    printf("Loaded " AWHITE("%s") " in %s.\n",
           model_key.c_str(),
           ANSI::Time(load_timer.Seconds()).c_str());

    prompt_header = Util::ReadFile("variations.txt");
    CHECK(!prompt_header.empty());

    prompt_header += "\nOriginal text:\n\n<P>";

    // Since we always use the same prompt header, play that once
    // and save the state.
    llm->DoPrompt(prompt_header);
    post_prompt_state.reset(new LLM::State(llm->SaveState()));
  }

  std::string DatabaseKey(const Rephrasable &rephrasable) override {
    // PERF:
    // We need the model key, but it would be nice if we didn't have
    // to load the model to have that.
    LazyInit();
    SHA256::Ctx hash;
    SHA256::Init(&hash);
    SHA256::UpdateString(&hash, model_key);
    // Make sure the context and text can't get mixed up, no matter what
    // they contain.
    SHA256::UpdateString(
        &hash, StringPrintf(".%d.%d.",
                            (int)rephrasable.context.size(),
                            (int)rephrasable.text.size()));
    SHA256::UpdateString(&hash, rephrasable.context);
    SHA256::UpdateString(&hash, rephrasable.text);
    return SHA256::Ascii(SHA256::FinalVector(&hash));
  }

  bool Rephrase(const Rephrasable &rephrasable) override {
    LazyInit();

    Timer prep_timer;
    // PERF: Save some of the recent texts (by database key) so that
    // we don't need to replay them.
    llm->LoadState(*post_prompt_state);

    // Prompt already contains instructions and "Original text:\n\n<P>"
    std::string input =
      StringPrintf("%s</P>\n"
                   "\n"
                   "Rephrased text:\n\n"
                   "<P>",
                   rephrasable.text.c_str());

    printf(AGREY("Completed prompt: [%s]") "\n", input.c_str());

    llm->DoPrompt(input);
    // Reset regex, since the prompt may not have followed it.
    llm->sampler.SetRegEx(".*</P>");

    const double prep_sec = prep_timer.Seconds();
    printf("[finished prep in %s]\n", ANSI::Time(prep_sec).c_str());

    const std::string key = DatabaseKey(rephrasable);

    // Get starting path.
    const auto &[path_in, next_depth_in] = GetNextPrefix(key);

    const int max_tokens =
      2 * (int)llm->context.Tokenize(rephrasable.text, false).size();

    Path path = path_in;
    int next_depth = next_depth_in;

    std::string text;

    Timer replay_timer;
    if (!path.empty()) {
      std::vector<llama_token> tokens;
      tokens.reserve(path.size());
      for (const Node &node : path) {
        tokens.push_back(node.token);
        text += llm->context.TokenString(node.token);
      }
      llm->TakeTokenBatch(tokens, true);
    }
    const double replay_sec = replay_timer.Seconds();
    printf("Replayed path in %s\n", ANSI::Time(replay_sec).c_str());

    Timer inference_timer;
    while ((int)path.size() < max_tokens) {
      // No need to filter by NFA, since the NFA is just used to
      // tell when we are done. We could consider applying
      // temperature, though?
      std::vector<std::pair<llama_token, double>> probdist =
        Sampler::ProbDist(llm->context.GetCandidates());

      CHECK(next_depth < (int)probdist.size()) << "I guess this could "
        "happen if we sample thousands of times at the same "
        "position?";
      double p_skipped = 0;
      for (int i = 0; i < next_depth; i++)
        p_skipped += probdist[i].second;

      const auto &[id, p] = probdist[next_depth];
      path.push_back(Node{
          .token = id,
          .depth = next_depth,
          .p_skipped = p_skipped,
          .p = p});
      // From now on, take the toap token.
      next_depth = 0;

      llm->TakeTokenBatch({id});
      std::string tok = llm->context.TokenString(id);
      text += tok;
      if (id == llm->context.EOSToken())
        break;
      printf("%s", tok.c_str());
      if (llm->sampler.Accepting() || llm->sampler.Stuck()) {
        break;
      }
      if ((int)path.size() % 3 == 0) fflush(stdout);
    }

    // Would be nice if we could remove the nodes from the path
    // as well, but it doesn't really affect anything since we
    // aren't trying to keep a correspondence between that and
    // the string.
    (void)Util::TryStripSuffix("</P>", &text);
    // XXX one tokenization of </ P >.
    // A better way would be to stringify the tokens and see if
    // they match the string </P>.
    if (!path.empty() && path.back().token == 29958) path.pop_back();
    if (!path.empty() && path.back().token == 29925) path.pop_back();
    if (!path.empty() && path.back().token == 829) path.pop_back();

    printf("\nFinal text: " AYELLOW("%s") "\n", text.c_str());

    // Check validity.
    DocTree doc;
    std::string error;
    bool valid = Rejoin(rephrasable, text, &doc, &error);
    if (valid) {
      printf(AGREEN("Valid:") "\n");
      DebugPrintDocTree(doc);
    } else {
      printf(ARED("Not valid") ": %s\n", error.c_str());
    }

    DatabaseRow row;
    row.text = std::move(text);
    row.skipped_loss = 0.0;
    row.other_loss = 0.0;
    for (const Node &node : path) {
      row.skipped_loss += node.p_skipped;
      row.other_loss += (1.0 - node.p);
    }
    row.path = std::move(path);
    row.valid = valid;
    db.entries[key].push_back(std::move(row));

    return valid;
  }

  int GetNumRephrasings(const Rephrasable &rephrasable) override {
    const std::string key = DatabaseKey(rephrasable);
    const auto it = db.entries.find(key);
    if (it == db.entries.end()) return {};
    int num = 0;
    for (const DatabaseRow &row : it->second)
      if (row.valid)
        num++;
    return num;
  }

  std::vector<std::pair<double, std::string>> GetRephrasings(
      const Rephrasable &rephrasable) override {
    const std::string key = DatabaseKey(rephrasable);
    const auto it = db.entries.find(key);
    if (it == db.entries.end()) return {};

    std::vector<std::pair<double, std::string>> ret;
    for (const DatabaseRow &row : it->second) {
      if (row.valid) {
        ret.emplace_back(row.skipped_loss, row.text);
      }
    }

    std::sort(ret.begin(), ret.end(),
              [](const auto &a, const auto &b) {
                return a.first < b.first;
              });

    return ret;
  }

  std::string model_key;
  std::unique_ptr<LLM> llm;
  std::string prompt_header;
  std::unique_ptr<LLM::State> post_prompt_state;
  Database db;
};

}  // namespace

bool Rephrasing::Rejoin(
    const Rephrasable &rephrasable,
    const std::string &text,
    DocTree *doc,
    std::string *error) {
  std::string html_error;
  std::vector<HTMLNode> nodes = HTML::Parse(text, &html_error);
  if (!html_error.empty()) {
    printf(ARED("Couldn't parse as HTML") ": %s\n", html_error.c_str());
    if (error != nullptr) *error = html_error;
    return false;
  }

  bool failed = false;
  std::function<DocTree(const HTMLNode &node)> Rec =
    [&rephrasable, error, &failed, &Rec](const HTMLNode &node) -> DocTree {
      if (failed) return DocTree();
      if (node.is_tag) {
        if (node.str == "span") {
          auto it = node.attrs.find("class");
          if (it == node.attrs.end()) {
            failed = true;
            if (error != nullptr) *error = "missing class attr on span";
            return DocTree();
          }

          static RE2 class_re("c([0-9]+)");
          int class_num = -1;
          if (RE2::FullMatch(it->second, class_re, &class_num) &&
              class_num >= 0 && class_num < (int)rephrasable.classes.size()) {
            DocTree span;
            span.attrs = rephrasable.classes[class_num];
            for (const HTMLNode &child : node.children) {
              span.AddChild(Rec(child));
            }
            return span;

          } else {
            failed = true;
            if (error != nullptr)
              *error = StringPrintf("class not the right format: [%s]",
                                    it->second.c_str());
            return DocTree();
          }

        } else if (node.str == "img") {
          auto it = node.attrs.find("src");
          if (it == node.attrs.end()) {
            failed = true;
            if (error != nullptr) *error = "missing src on img";
            return DocTree();
          }

          static RE2 img_re("img([0-9]+).png");
          int img_num = -1;
          if (RE2::FullMatch(it->second, img_re, &img_num) &&
              img_num >= 0 && img_num < (int)rephrasable.images.size()) {
            return *rephrasable.images[img_num];

          } else {
            failed = true;
            if (error != nullptr) *error = "img not the right format";
            return DocTree();
          }

        } else {
          failed = true;
          if (error != nullptr) *error = "unknown tag";
          return DocTree();
        }
      } else {
        // Text nodes become text docs.
        return TextDoc(node.str);
      }
    };

  std::vector<DocTree> ret;
  ret.reserve(nodes.size());
  for (const HTMLNode &node : nodes)
    ret.push_back(Rec(node));

  if (failed) return false;
  *doc = JoinDocs(std::move(ret));
  return true;
}

Rephrasable Rephrasing::GetTextToRephrase(const DocTree &doc) {
  Rephrasable rephrasable;
  std::function<void(const DocTree &)> Rec =
    [&rephrasable, &Rec](const DocTree &doc) -> void {
      if (doc.IsText()) {

        std::string normtext = NormalizeWhitespace(doc.text);
        rephrasable.text += normtext;

      } else {
        if (const std::string *display = doc.GetStringAttr("display")) {
          if (*display == "box") {
            // This is already a box with a fixed size, so we just copy it.
            StringAppendF(&rephrasable.text, "<img src=\"img%d.png\">",
                          (int)rephrasable.images.size());
            rephrasable.images.push_back(std::make_shared<DocTree>(doc));
            return;
          } else if (*display == "span") {
            bool has_style = doc.GetStringAttr("font-face") != nullptr ||
              doc.GetDoubleAttr("font-size") != nullptr ||
              doc.GetBoolAttr("font-bold") != nullptr ||
              doc.GetBoolAttr("font-italic") != nullptr;

            if (has_style) {
              // Process children.
              StringAppendF(&rephrasable.text, "<span class=\"c%d\">",
                            (int)rephrasable.classes.size());
              rephrasable.classes.push_back(doc.attrs);
              for (const std::shared_ptr<DocTree> &child : doc.children) {
                Rec(*child);
              }
              StringAppendF(&rephrasable.text, "</span>");
              return;
            }

          } else {
            LOG(FATAL) << "Unknown display: " << *display;
          }
        }

        // Groups and unstyled spans are ignored.
        for (const std::shared_ptr<DocTree> &child : doc.children) {
          Rec(*child);
        }
      }
    };

  Rec(doc);
  return rephrasable;
}

Rephrasing::Rephrasing() {}
Rephrasing::~Rephrasing() {}

Rephrasing *Rephrasing::Create(const std::string &database_file) {
  return new RephrasingImpl(database_file);
}

