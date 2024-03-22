#include "rephrasing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string_view>
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

// Hack!
static constexpr int TAIL_TOKEN_HEADROOM = 5;

namespace {

static std::string ColorProb(float prob) {
  const auto &[r, g, b, a_] =
    ColorUtil::Unpack32(
        ColorUtil::LinearGradient32(ColorUtil::HEATED_TEXT, prob));

  return StringPrintf("%s%.2f%%" ANSI_RESET,
                      ANSI::ForegroundRGB(r, g, b).c_str(),
                      prob * 100.0);
}

// We store quantized (16-bit) probabilities in paths, mostly so that they
// are easier to look at when serializing. This also makes sure that they
// are actually in [0, 1].
struct QProb {
  std::string ToString() const {
    return StringPrintf("%04x", word);
  }
  QProb() {}
  explicit QProb(const std::string &s) {
    CHECK(s.size() == 4 &&
          Util::HexDigit(s[0]) &&
          Util::HexDigit(s[1]) &&
          Util::HexDigit(s[2]) &&
          Util::HexDigit(s[3])) << s;
    int w =
      (Util::HexDigitValue(s[0]) << 12) |
      (Util::HexDigitValue(s[1]) << 8) |
      (Util::HexDigitValue(s[2]) << 4) |
      (Util::HexDigitValue(s[3]) << 0);
    word = w;
  }

  explicit QProb(double d) {
    word = std::clamp((int)std::round(d * 65535.0), 0, 65535);
  }

  operator double() const {
    return (double)word / 65535.0;
  }

  uint16_t word = 0;
};

// As we predict, we order the probabilities at each branch and
// we're mostly exploring the most probable path. This vector
// gives the depth of the sample we took (with the expectation
// that we've explored every shallower sample along this prefix),
// as well as the probability of this token.
struct Node {
  int token = 0;
  int depth = 0;
  // Total probability mass with lower depth.
  QProb p_skipped;
  // Probability of this token.
  QProb p;
  // And of the token with the next depth.
  QProb p_next;
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

  static std::string Escape(const std::string &in) {
    std::string out;
    out.reserve(in.size());

    for (int i = 0; i < (int)in.size(); i++) {
      unsigned char c = in[i];
      if (c < 20 || c == '%') {
        out.push_back('%');
        out.push_back(Util::HexDigit((c >> 4) & 15));
        out.push_back(Util::HexDigit(c & 15));
      } else {
        out.push_back(c);
      }
    }

    return out;
  }

  static std::string Unescape(const std::string &in) {
    std::string out;
    out.reserve(in.size());

    for (int i = 0; i < (int)in.size(); i++) {
      unsigned char c = in[i];
      if (c == '%') {
        if (i + 2 < (int)in.size()) {
          out.push_back((Util::HexDigitValue(in[i + 1]) << 4) +
                        Util::HexDigitValue(in[i + 2]));
          i += 2;
        }
      } else {
        out.push_back(c);
      }
    }

    return out;
  }

  static std::string PathString(const Path &path) {
    std::string out;
    for (const Node &node : path) {
      StringAppendF(&out, "%d %d %s %s %s ",
                    node.token, node.depth,
                    node.p_skipped.ToString().c_str(),
                    node.p.ToString().c_str(),
                    node.p_next.ToString().c_str());
    }
    return out;
  }

  static Path ParsePath(const std::string &str) {
    static const RE2 node_re(
        " *([0-9]+) +([0-9]+) +([0-9A-Fa-f]+) +([0-9A-Fa-f]+) +([0-9A-Fa-f]+) *");
    Path path;
    re2::StringPiece input(str);
    // while (!input.empty() && input[0] == ' ') input.remove_prefix(1);
    int tok = 0, depth = 0;
    std::string pskip, p, pnext;
    while (RE2::Consume(&input, node_re, &tok, &depth, &pskip, &p, &pnext)) {
      Node node;
      node.token = tok;
      node.depth = depth;
      node.p_skipped = QProb(pskip);
      node.p = QProb(p);
      node.p_next = QProb(pnext);
      path.push_back(std::move(node));
    }

    CHECK(input.empty()) << "Unrecognized content left in Path:\n"
                         << std::string(input)
                         << "\nFrom original input:\n"
                         << str;

    return path;
  }

  void SaveToFile(const std::string &file) const {

    std::vector<std::pair<const std::string *,
                          const std::vector<DatabaseRow> *>> sorted;
    sorted.reserve(entries.size());
    for (const auto &[key, rows] : entries) {
      sorted.emplace_back(&key, &rows);
    }

    std::sort(sorted.begin(), sorted.end(),
              [](const auto &a, const auto &b) {
                return *a.first < *b.first;
              });

    std::string out;
    for (const auto &[key, rows_in] : sorted) {
      // The order doesn't matter, but it is nice to keep them sorted
      // so that we can see the shared prefixes of the text.
      std::vector<const DatabaseRow *> rows;
      rows.reserve(rows_in->size());
      for (const auto &row : *rows_in) rows.push_back(&row);
      std::sort(rows.begin(), rows.end(),
                [](const DatabaseRow *a, const DatabaseRow *b) {
                  return a->text < b->text;
                });

      // Key and number of rows.
      // They are ASCII SHA-256.
      StringAppendF(&out, "%s %d\n", key->c_str(),
                    (int)rows_in->size());

      for (const DatabaseRow *row : rows) {
        std::string path_string = PathString(row->path);
        // The text sometimes starts with space, so we use | to
        // make sure we can just strip off leading whitespace.
        StringAppendF(&out, "  |%s\n", Escape(row->text).c_str());
        StringAppendF(&out, "    %s\n", path_string.c_str());
        StringAppendF(&out, "    %c %.17g %.17g\n",
                      row->valid ? 'V' : 'X',
                      row->skipped_loss,
                      row->other_loss);
      }
    }

    Util::WriteFile(file, out);
  }

  void ReadFromFile(const std::string &file) {
    auto Error = [&file](const std::string &msg) {
        return StringPrintf(ARED("Bad rephrase database file") " %s:\n"
                            ABLUE("%s") "\n"
                            "You should probably just delete it?\n",
                            file.c_str(), msg.c_str());
      };

    static const RE2 key_re("([A-Za-z0-9]+) +([0-9]+) *");
    static const RE2 text_re(" *\\|(.*)");
    // very permissive
    #define FLOAT_RE "[-+0-9.eE]+"
    static const RE2 score_re(" *([VX]) +(" FLOAT_RE ") +(" FLOAT_RE ") *");

    entries.clear();
    std::vector<std::string> lines = Util::ReadFileToLines(file);
    for (int lidx = 0; lidx < (int)lines.size(); /* in loop */) {
      std::string &key_line = lines[lidx];
      // Expect a key.
      std::string key;
      int num_rows = -1;
      CHECK(RE2::FullMatch(key_line, key_re, &key, &num_rows))
        << Error("key") << "\n" << key_line;
      lidx++;

      int num_valid = 0;
      static constexpr int LINES_PER_ROW = 3;
      std::vector<DatabaseRow> rows;
      rows.reserve(num_rows);
      for (int ridx = 0; ridx < num_rows; ridx++) {
        int roff = lidx + ridx * LINES_PER_ROW;
        CHECK(roff + (LINES_PER_ROW - 1) < (int)lines.size())
          << Error("num_rows");
        std::string &text_line = lines[roff + 0];
        std::string &path_line = lines[roff + 1];
        std::string &score_line = lines[roff + 2];
        DatabaseRow row;
        CHECK(RE2::FullMatch(text_line, text_re, &row.text)) << Error("text");
        row.path = ParsePath(path_line);
        char valid = 0;
        CHECK(RE2::FullMatch(score_line, score_re,
                             &valid, &row.skipped_loss, &row.other_loss))
          << Error("score") << "\n" << score_line;
        row.valid = (valid == 'V');
        if (row.valid) num_valid++;

        rows.push_back(std::move(row));
      }

      if (VERBOSE > 0) {
        std::string kt = key.size() > 20 ? key.substr(0, 17) + "..." : key;
        printf(AGREY("%s") ": Read " ACYAN("%d") " rows (%d valid)\n",
               kt.c_str(), (int)rows.size(), num_valid);
      }

      entries[key] = std::move(rows);
      lidx += num_rows * LINES_PER_ROW;
    }

    if (VERBOSE > 0) {
      printf("Read " ACYAN("%d") " entries from " AWHITE("%s") "\n",
             (int)entries.size(), file.c_str());
    }

  }

};

struct RephrasingImpl : public Rephrasing {

  RephrasingImpl(const std::string &database_file) :
    db_filename(database_file) {
    db.ReadFromFile(db_filename);
  }

  ~RephrasingImpl() override {}

  bool IsPathNew(
      const std::vector<DatabaseRow> &rows,
      const Path &new_path, int prefix_size, int next_depth) {
    for (const DatabaseRow &row : rows) {
      const Path &path = row.path;
      if (prefix_size + 1 < (int)path.size()) {
        for (int i = 0; i < prefix_size; i++) {
          if (new_path[i].depth != path[i].depth) goto try_next;
        }
        if (path[prefix_size].depth == next_depth) return false;
      }

    try_next:;
    }
    return true;
  }

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

      const Path &path = row.path;

      if (VERBOSE > 1) {
        printf("For each node:\n");
      }

      // So that we can get a probability for the first token,
      // we do some "laplace smoothing".
      double laplace_numer = 1.0;
      double laplace_denom = 1.0;

      double total_p = (laplace_numer * laplace_denom);
      for (int i = 0; i < (int)path.size() - TAIL_TOKEN_HEADROOM; i++) {
        // average probability of samples up to here
        double avg_p = total_p / (i + laplace_denom);

        const Node &node = path[i];
        total_p += node.p;

        double score = avg_p * node.p_next;

        if (VERBOSE > 1) {
          /*
          printf(AGREY("%d") "=%s " AGREY("d%d") " %s ~%s",
                 node.token,
                 llm->TokenString(node.token).c_str(),
                 node.depth,
                 ColorProb(path[i].p).c_str(),
                 ColorProb(total_p / (i + 1)).c_str());
          */
          printf(AGREY("%s") " " ACYAN("%.3f") " * " ABLUE("%.3f")
                 " = %s ",
                 llm->TokenString(node.token).c_str(),
                 avg_p,
                 (double)node.p_next,
                 ColorProb(score).c_str());
        }

        if (score > best_score) {
          // We know that this next depth won't match this path
          // (since we're incrementing the depth), but we could
          // have explored it already. Only consider it if it's
          // new (not a prefix of any path).
          if (IsPathNew(rows, path, i, node.depth + 1)) {
            best_path = path;
            best_next = node.depth + 1;
            // Not including this last node.
            best_path.resize(i);
            best_row = row_idx;
            best_score = score;
            if (VERBOSE > 1) {
              printf(AGREEN("♥"));
            }

          } else {
            if (VERBOSE > 1) {
              printf(ARED("💣"));
            }
          }

        }
      }

      if (VERBOSE > 1) {
        printf("\n");
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
    int next_node_depth = next_depth_in;

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

    printf(AGREY("%s"), text.c_str());

    Timer inference_timer;
    while ((int)path.size() < max_tokens) {
      // No need to filter by NFA, since the NFA is just used to
      // tell when we are done. We could consider applying
      // temperature, though?
      std::vector<std::pair<llama_token, double>> probdist =
        Sampler::ProbDist(llm->context.GetCandidates());

      CHECK(next_node_depth < (int)probdist.size()) << "I guess this could "
        "happen if we sample thousands of times at the same "
        "position?";
      double p_skipped = 0;
      for (int i = 0; i < next_node_depth; i++)
        p_skipped += probdist[i].second;
      // For this node, what't the probability of the next best token?
      // We'll use this later to see if this is a good place to try
      // a different alternative path.
      const double p_next =
        next_node_depth + 1 < (int)probdist.size() ?
        probdist[next_node_depth + 1].second : 0.0;

      const auto &[id, p] = probdist[next_node_depth];
      path.push_back(Node{
          .token = id,
          .depth = next_node_depth,
          .p_skipped = QProb(p_skipped),
          .p = QProb(p),
          .p_next = QProb(p_next),
        });
      // From now on, take the top token.
      next_node_depth = 0;

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

    // Strip this from the text, and also try to strip it from
    // the path.
    (void)Util::TryStripSuffix("</P>", &text);

    #if 0
    std::string suffix = " </P>";
    while (!suffix.empty() && !path.empty()) {
      int tok = path.back().token;
      std::string stok = llm->TokenString(tok);

      if (Util::TryStripSuffix(stok, &suffix)) {
        printf("Stripped %d = " APURPLE("%s") "\n", tok, stok.c_str());
        path.pop_back();
      } else {
        if (suffix == " ") {
          // OK to leave this.
          printf("Stripping up to space character.");
        } else {
          printf(ARED("Unable to strip </P> suffix :(") "\n");
        }
        break;
      }
    }
    #endif

    #if 0
    // XXX one tokenization of </ P >.
    // A better way would be to stringify the tokens and see if
    // they match the string </P>.
    if (!path.empty() && path.back().token == 29958) path.pop_back();
    if (!path.empty() && path.back().token == 29925) path.pop_back();
    if (!path.empty() && path.back().token == 829) path.pop_back();
    #endif

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
    if (it == db.entries.end()) return 0;
    int num = 0;
    for (const DatabaseRow &row : it->second) {
      if (row.valid) {
        num++;
      }
    }
    return num;
  }

  std::vector<std::pair<double, std::string>> GetRephrasings(
      const Rephrasable &rephrasable) override {
    const std::string key = DatabaseKey(rephrasable);
    const auto it = db.entries.find(key);
    if (it == db.entries.end()) return {};

    // It's possible to get the same text through two different
    // tokenizations! We should not allow that.

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

  void Save() override {
    db.SaveToFile(db_filename);
    if (VERBOSE > 0) {
      printf("Saved rephrasing database to " AWHITE("%s") "\n",
             db_filename.c_str());
    }
  }

  std::string db_filename;
  std::string model_key;
  std::unique_ptr<LLM> llm;
  std::string prompt_header;
  std::unique_ptr<LLM::State> post_prompt_state;
  Database db;
};

}  // namespace

bool Rephrasing::Rejoin(
    const Rephrasable &rephrasable,
    const std::string &text_in,
    DocTree *doc,
    std::string *error) {

  const std::string &before_text = rephrasable.text;
  const bool orig_space_before =
    !before_text.empty() && before_text[0] == ' ';
  const bool orig_space_after =
    !before_text.empty() && before_text.back() == ' ';

  std::string_view text(text_in);
  if (!orig_space_before) {
    while (!text.empty() && text[0] == ' ') text.remove_prefix(1);
  }
  if (!orig_space_after) {
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
  }

  std::string html_error;
  std::vector<HTMLNode> nodes = HTML::Parse(std::string(text), &html_error);
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

