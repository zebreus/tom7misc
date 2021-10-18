
#include "wikipedia.h"

#include "base/logging.h"
#include "re2/re2.h"
#include "util.h"

using namespace std;
using re2::RE2;
using re2::StringPiece;

namespace {

#define WS_RE "[ \r\n\t]*"
#define ANY_RE "(?:.|" WS_RE ")*"

struct WikipediaImpl : public Wikipedia {
  explicit WikipediaImpl(const std::string &filename) :
    filename(filename),
    redirect_re(WS_RE "#REDIRECT" ANY_RE)
  {
    f = fopen(filename.c_str(), "rb");
    CHECK(f != nullptr);
  }

  // We should probably explicitly buffer this; getc
  // is just not fast!
  optional<string> NextLine() {
    if (feof(f)) return {};
    std::string ret;
    for (;;) {
      int c = getc(f);
      if (c == EOF) {
        if (ret.empty()) return {};
        return {ret};
      }

      if (c == '\r') continue;
      if (c == '\n') return {ret};
      ret.push_back(c);
    }
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

  ~WikipediaImpl() override {
    fclose(f);
    f = nullptr;
  }

  bool IsRedirect(const Article &art) override {
    return RE2::FullMatch(art.body, redirect_re);
  }

  string ReplaceEntities(std::string body) override {
    // TODO: preconstruct these regexes (and perhaps benchmark)
    RE2::GlobalReplace(&body, "&lt;", "<");
    RE2::GlobalReplace(&body, "&gt;", ">");
    RE2::GlobalReplace(&body, "&quot;", "\"");
    // ... more here?

    // Last!
    RE2::GlobalReplace(&body, "&amp;", "&");
    return body;
  }

  string RemoveTags(string body) override {
    string out;
    out.reserve(body.size());

    RE2::Options first;
    first.set_longest_match(false);
    RE2 ref_start_re("<ref(?:" WS_RE "name=\"[^\">]*\")?" WS_RE ">");
    RE2 ref_end_re("</ref>");

    StringPiece input(body);

    size_t pos = 0;
    StringPiece submatch;
    while (ref_start_re.Match(input,
                              pos,
                              input.size(),
                              RE2::UNANCHORED,
                              &submatch, 1)) {
      // Copy up until the match.
      size_t matchpos = submatch.data() - input.data();
      out += body.substr(pos, matchpos - pos);

      pos = matchpos + submatch.size();
      // Now same to find the first end tag.

      if (!ref_end_re.Match(input,
                            pos,
                            input.size(),
                            RE2::UNANCHORED,
                            &submatch, 1)) {
        printf("Unclosed <ref> tag!\n");
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

    #if 0
    StringPiece input(body);
    string prefix;
    while (RE2::Consume(&input, ref_re, &prefix))
      out += prefix;

    // XXX add remainder of input too right?
    out += input.as_string();

    // XXX seems this is "longest match," not "first..."a
    // RE2::GlobalReplace(&body, ref_re, "");
    return out;
    #endif
  }

  const std::string filename;
  FILE *f = nullptr;
  const RE2 redirect_re;
  // const RE2 title_re, title_end_re;
  // const RE2 body_re, body_end_re;
};

}  // namespace

Wikipedia *Wikipedia::Create(const string &filename) {
  return new WikipediaImpl(filename);
}
