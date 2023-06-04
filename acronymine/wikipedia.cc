
#include "wikipedia.h"

#include <string>
#include <unordered_set>

#include "base/logging.h"
#include "re2/re2.h"
#include "util.h"

using namespace std;
using re2::RE2;
using re2::StringPiece;

using uint8 = uint8_t;
using int64 = int64_t;

namespace {

#define WS_RE "[ \r\n\t]*"
#define ANY_RE "(?:.|" WS_RE ")*"

struct BufferedFile {
  static constexpr int64 BYTES_AT_A_TIME = 1 << 24;
  explicit BufferedFile(const std::string &filename) :
    filename(filename) {
    f = fopen(filename.c_str(), "rb");
    CHECK(f != nullptr) << filename;
    Refresh();
  }

  bool AtEof() const { return at_eof; }

  std::optional<string> GetLine() {
    if (AtEof()) return {};
    string ret;
    for (int i = bpos; i < bytes.size(); i++) {
      if (bytes[i] == '\n') {
        ret.resize(i - bpos);
        memcpy(ret.data(), bytes.data() + bpos, i - bpos);
        bpos = i + 1;
        return {ret};
      }
    }

    // The line might span across the buffered chunk.
    // We don't need this to be fast but it has to
    // work.
    ret.resize(bytes.size() - bpos);
    memcpy(ret.data(), bytes.data() + bpos, bytes.size() - bpos);
    bpos = bytes.size();
    Refresh();
    auto rec = GetLine();
    if (rec.has_value())
      ret += rec.value();
    if (ret.empty())
      return {};
    return {ret};
  }

  int Get() {
    if (bpos < bytes.size()) {
      return bytes[bpos++];
    } else {
      if (at_eof)
        return EOF;

      Refresh();
      if (at_eof)
        return EOF;

      CHECK(bpos == 0 && !bytes.empty());
      // Return the first byte.
      bpos = 1;
      return bytes[0];
    }
  }

  ~BufferedFile() {
    fclose(f);
    f = nullptr;
  }

 private:

  // After Refresh, we are either at_eof or have bytes.
  void Refresh() {
    // Noticing we are at EOF for the first time?
    if (feof(f)) {
      bytes.clear();
      at_eof = true;
      return;
    }

    bytes.resize(BYTES_AT_A_TIME);
    const size_t bytes_read =
      fread(bytes.data(), 1, BYTES_AT_A_TIME, f);
    bytes.resize(bytes_read);

    if (!bytes_read) {
      // Possible?
      at_eof = true;
    }
    bpos = 0;
  }

  const string filename;
  int64 bpos = 0;
  vector<uint8> bytes;
  FILE *f = nullptr;
  bool at_eof = false;
};

static bool SelfExpandingTemplate(const string &t) {
  static std::unordered_set<string> &all = *new std::unordered_set<string>{
    "pi", "tau", "phi", "xi", "upsilon", "sigma", "mu", "lambda", "kappa",
    "theta", "epsilon", "gamma",
    // XXX lots more here..
  };
  return all.find(t) != all.end();
}

struct WikipediaImpl : public Wikipedia {
  explicit WikipediaImpl(const std::string &filename) :
    file(filename),
    redirect_re(WS_RE "#REDIRECT" ANY_RE)
  {
  }

  // We should probably explicitly buffer this; getc
  // is just not fast!
  optional<string> NextLine() {
    auto so = file.GetLine();
    if (so.has_value()) {
      // printf("Line [%s]\n", so.value().c_str());
    }
    return so;
    /*
    if (file.AtEof()) return {};
    std::string ret;
    for (;;) {
      int c = file.Get();
      if (c == EOF) {
        if (ret.empty()) return {};
        return {ret};
      }

      if (c == '\r') continue;
      if (c == '\n') return {ret};
      ret.push_back(c);
    }
    */

  }

  const string TITLE_START = "<title>";
  const string TITLE_END = "</title>";
  const string BODY_START = "<text xml:space=\"preserve\">";
  const string BODY_END = "</text>";

  std::optional<string> NextDelimited(const std::string &start, const std::string &end) {
    for (;;) {
      std::optional<string> lineo = NextLine();
      if (!lineo.has_value()) return {};
      const string &line = lineo.value();
      const auto p = line.find(start);
      if (p != string::npos) {
        // Line after <start>...
        std::string s = line.substr(p + start.size(), string::npos);
        // (which could have </end> on the same line...)
        auto e = s.rfind(end);
        if (e != string::npos) {
          // Already done, then...
          return {s.substr(0, e)};
        } else {
          // Otherwise, keep reading lines until we see the end.
          std::vector<std::string> lines = {s};
          for (;;) {
            std::optional<string> line2o = NextLine();
            if (!line2o.has_value()) return {};
            string line2 = std::move(line2o.value());
            auto ee = line2.rfind(end);
            if (ee != string::npos) {
              lines.push_back(line2.substr(0, ee));
              return Util::Join(lines, "\n");
            }
            // Otherwise, add the entire line
            lines.push_back(std::move(line2));
          }
        }
      }
    }
  }

  std::optional<Article> Next() override {
    std::optional<string> title = NextDelimited(TITLE_START, TITLE_END);
    if (!title.has_value()) return {};
    std::optional<string> body = NextDelimited(BODY_START, BODY_END);
    if (!body.has_value()) return {};
    return {Article{.title = std::move(title.value()),
                    .body = std::move(body.value())}};
  }

  bool IsRedirect(const Article &art) override {
    return RE2::FullMatch(art.body, redirect_re);
  }

  string ReplaceEntities(std::string body) override {
    // TODO: preconstruct these regexes (and perhaps benchmark)
    RE2::GlobalReplace(&body, "&lt;", "<");
    RE2::GlobalReplace(&body, "&gt;", ">");
    RE2::GlobalReplace(&body,
                       "(?:&quot;|&rdquo;|&ldquo;|"
                       "&#8221;|&#8220;|"
                       "&#[Xx]201[Cc];|&#[Xx]201[Dd];)", "\"");
    // ... more here?

    // These are not faithful, but better to have ascii
    // versions.
    RE2::GlobalReplace(&body, "(?:&nbsp;|&thinsp;)", " ");

    RE2::GlobalReplace(&body, "&ndash;", "-");
    RE2::GlobalReplace(&body, "&mdash;", "--");
    RE2::GlobalReplace(&body, "&minus;", "-");
    RE2::GlobalReplace(&body, "&prime;", "'");

    // left-to-right and right-to-left marks
    RE2::GlobalReplace(&body, "(?:&lrm;|&rlm;)", "");

    // Last! The corpus does include lots of double-escaped
    // entities, by the way. Even though it's presumably
    // erroneous, running this function repeatedly might
    // still make sense?
    RE2::GlobalReplace(&body, "&amp;", "&");
    return body;
  }

  string DeleteMatching(const string &body,
                        const RE2 &start_re,
                        const RE2 &end_re,
                        const char *what) {
    string out;
    out.reserve(body.size());

    StringPiece input(body);

    size_t pos = 0;
    StringPiece submatch;
    while (start_re.Match(input,
                          pos,
                          input.size(),
                          RE2::UNANCHORED,
                          &submatch, 1)) {
      // Copy up until the match.
      size_t matchpos = submatch.data() - input.data();
      out += body.substr(pos, matchpos - pos);

      pos = matchpos + submatch.size();
      // Now same to find the first end tag.

      if (!end_re.Match(input,
                        pos,
                        input.size(),
                        RE2::UNANCHORED,
                        &submatch, 1)) {
        // printf("Unclosed %s tag!\n", what);
        return out;
      } else {
        // and skip until after this </tag>
        size_t match2pos = submatch.data() - input.data();
        pos = match2pos + submatch.size();
      }
    }

    // ... and the tail of the string
    out += body.substr(pos, string::npos);
    return out;
  }

  string RemoveTags(const string &body) override {
    RE2 ref_start_re("<ref(?:" WS_RE "(?:name|group)"
                     WS_RE "=" WS_RE
                     // "quoted arg" or just arg
                     "(?:\"[^\">]*\"|[-_A-Za-z0-9]*))?" WS_RE ">");
    RE2 ref_end_re("</ref>");

    // same but like <ref/>
    RE2 ref_void_re("<ref(?:" WS_RE "(?:name|group)"
                     WS_RE "=" WS_RE
                     // "quoted arg" or just arg
                     "(?:\"[^\">]*\"|[-_A-Za-z0-9]*))?" WS_RE "/>");
    RE2 references_re("<references" WS_RE "/>\n?");

    RE2 comment_start_re("<!--");
    RE2 comment_end_re("-->");

    string out =
      DeleteMatching(body, ref_start_re, ref_end_re, "ref");
    out = DeleteMatching(
        out, comment_start_re, comment_end_re, "comment");

    RE2::GlobalReplace(&out, ref_void_re, "");
    RE2::GlobalReplace(&out, references_re, "");

    return out;
  }

  string RemoveTemplate(const std::string &in) {
    string out;
    out.reserve(in.size());

    // if > 0, then we're inside a {{template}}.
    int template_start = 0;
    int depth = 0;
    for (int i = 0; i < in.size(); i++) {
      if (in[i] == '{' && i < in.size() - 1 &&
          in[i + 1] == '{') {
        depth++;
        // skip second brace
        i++;
        // (and ignore it)
        if (depth == 1) template_start = i + 1;
      } else if (in[i] == '}' && i < in.size() - 1 &&
                 in[i + 1] == '}') {
        i++;
        if (depth == 0) {
          // printf("Template closed while not in template\n");
          return out;
        } else {
          depth--;
        }

        if (depth == 0) {
          // Upon exiting a template, parse it.
          string body = in.substr(template_start, (i - 1) - template_start);
          string lbody = Util::lcase(body);
          // Most of them we just drop, but some common templates
          // can be implemented in place.
          if (lbody.starts_with("nowrap|")) {
            out += body.substr(7, string::npos);
          } else if (SelfExpandingTemplate(lbody)) {
            out += body;
          }
          // TODO: {{convert|6|mm}}
        }

      } else {
        if (depth == 0) {
          out.push_back(in[i]);
        }
      }
    }
    return out;
  }

  // [[Link]] -> Link
  // [[Link|alt]] -> alt
  // [[Link (dab)|]] -> Link
  // Drop [[File:]] [[Image:]] [[Category:]] etc.
  string RemoveLink(const std::string &in) {
    string out;
    out.reserve(in.size());

    // if > 0, then we're inside a {{template}}.
    int depth = 0;
    int link_start = 0;
    for (int i = 0; i < in.size(); i++) {
      if (in[i] == '[' && i < in.size() - 1 &&
          in[i + 1] == '[') {
        depth++;
        // skip second bracket
        i++;
        // (and ignore it)
        if (depth == 1) link_start = i + 1;
      } else if (in[i] == ']' && i < in.size() - 1 &&
                 in[i + 1] == ']') {
        i++;
        if (depth == 0) {
          // printf("Link closed while not in link\n");
          return out;
        } else {
          depth--;
        }

        if (depth == 0) {
          // Upon exiting a link, parse it.
          string body = in.substr(link_start, (i - 1) - link_start);
          // printf("[[%s]]\n", body.c_str());
          string lbody = Util::lcase(body);
          if (lbody.starts_with("file:") ||
              lbody.starts_with("image:") ||
              lbody.starts_with("category:")) {
            // Drop it completely.
            continue;
          }

          auto pos = body.find("|");
          if (pos == string::npos) {
            out += body;
          } else {
            string pre = body.substr(0, pos);
            string post = body.substr(pos + 1, string::npos);
            if (post.empty()) {
              // This is actually usually because the rhs was
              // a template, and we dropped it. In this case,
              // use the pre part.
              // printf("MT: [%s] = [%s][%s]\n",
              // body.c_str(), pre.c_str(), post.c_str());
              //
              out += pre;

            } else {
              out += post;
            }
          }
        }

      } else {
        if (depth == 0) {
          out.push_back(in[i]);
        }
      }
    }
    return out;
  }

  void RemoveWikilinks(string *body) override {
    // RE2 category_re("\\[\\[[Cc]ategory:[^\\]]*\\]\\]\n?");
    // RE2::GlobalReplace(&body, category_re, "");
    string s = RemoveTemplate(*body);
    *body = RemoveLink(s);
  }

  void RemoveMarkup(string *body) override {
    RE2::GlobalReplace(body, "'''", "");
    RE2::GlobalReplace(body, "''", "");
  }

  void RemoveTables(string *body) override {
    // TODO {| ... |}
  }

  void ASCIIify(string *body) override {
    // Fancy double quotes
    RE2::GlobalReplace(body, "(?:\u201C|\u201D)", "\"");
    // Minus sign and en dash.
    RE2::GlobalReplace(body, "(?:\u2212|\u2013)", "-");
    // Em dash.
    RE2::GlobalReplace(body, "(?:\u2014)", "--");

    // TODO: fancy single quotes
  }

  BufferedFile file;
  const RE2 redirect_re;
  // const RE2 title_re, title_end_re;
  // const RE2 body_re, body_end_re;
};

}  // namespace

Wikipedia *Wikipedia::Create(const string &filename) {
  return new WikipediaImpl(filename);
}
