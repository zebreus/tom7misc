
#include "inclusion.h"

#include <vector>
#include <string>
#include <tuple>
#include <unordered_set>

#include "interval-cover.h"
#include "lex.h"
#include "util.h"

using Token = el::Token;
using Lexing = el::Lexing;

namespace {
struct InclusionImpl {

  InclusionImpl(const std::vector<std::string> &includepaths) :
    includepaths(includepaths),
    source_map{.cover = IntervalCover<std::string>("OUT_OF_RANGE")} {}

  std::string ResolveInclude(const std::string &filename) {
    // TODO: Use include paths to resolve the file.
    return filename;
  }

  void Append(const std::string &filename) {
    const size_t start_pos = source.size();
    CHECK(!stack.contains(filename)) << "Cycle in includes: " << filename;

    // Effectively the empty file.
    if (already_loaded.contains(filename))
      return;

    already_loaded.insert(filename);
    stack.insert(filename);

    const std::string contents = Util::ReadFile(filename);
    CHECK(!contents.empty()) << "Included file could not be loaded "
      // XXX should allow loading empty files...
      "(or is empty): " << filename;

    std::string lex_error;
    std::optional<std::vector<Token>> otokens =
      Lexing::Lex(contents, &lex_error);
    CHECK(otokens.has_value()) << "Lexing " << filename << ":\n"
      "Invalid syntax: " << lex_error;

    const std::vector<Token> ftokens = otokens.value();

    auto TokenStr = [&contents](Token t) {
        CHECK(t.start <= contents.size());
        CHECK(t.start + t.length <= contents.size());
        return std::string(contents.substr(t.start, t.length));
      };

    auto ReadStringLit = [&TokenStr](Token t) {
        // Remove leading and trailing double quotes. Process escapes.
        std::string s = TokenStr(t);
        CHECK(s.size() >= 2) << "Bug: The double quotes are included "
          "in the token.";
        return Lexing::UnescapeStrLit(s.substr(1, s.size() - 2));
      };

    // Shift a token to refer to the source appended at
    // start_pos instead of 0.
    const auto ShiftToken = [start_pos](Token t) {
        t.start += start_pos;
        return t;
    };

    size_t input_pos = 0;
    // Now append, processing recursively.
    for (int i = 0; i < (int)ftokens.size(); i++) {
      const Token &token = ftokens[i];
      if (token.type == el::INCLUDE) [[unlikely]] {
        i++;
        CHECK(i < (int)ftokens.size()) << "Lexing " << filename
                                       << ": File ends with INCLUDE.";
        const Token &target = ftokens[i];
        CHECK(target.type == el::STR_LIT) << "Lexing " << filename << ": "
          "Expected string literal immediately after INCLUDE.\n";

        std::string included_file =
          ResolveInclude(ReadStringLit(target));

        // Append recursively here.
        Append(included_file);

        // Skip the tokens and source characters corresponding to
        // the import.
        input_pos = target.start + target.length;

      } else {
        // Normal tokens.

        // Copy the token, including any leading whitespace.
        while (input_pos < token.start + token.length) {
          source_map.cover.SetPoint(source.size(), filename);
          source.push_back(contents[input_pos]);
          input_pos++;
        }

        // Now copy the token.
        tokens.push_back(ShiftToken(token));
      }
    }

    // Include untokenized trailing whitespace.
    while (input_pos < contents.size()) {
      source.push_back(contents[input_pos]);
      source_map.cover.SetPoint(input_pos, filename);
      input_pos++;
    }

    stack.erase(filename);
  }

  const std::vector<std::string> &includepaths;

  std::unordered_set<std::string> already_loaded;

  // Accumulated source, tokens, and source map.
  std::string source;
  std::vector<el::Token> tokens;
  std::unordered_set<std::string> stack;
  SourceMap source_map;
};
}  // namespace

std::tuple<std::string, std::vector<el::Token>, SourceMap>
Inclusion::Process(const std::vector<std::string> &includepaths,
                   const std::string &filename) {
  InclusionImpl inc(includepaths);
  inc.Append(filename);

  return std::make_tuple(std::move(inc.source),
                         std::move(inc.tokens),
                         std::move(inc.source_map));
}
