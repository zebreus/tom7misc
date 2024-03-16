
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

static constexpr bool VERBOSE = false;

namespace {
struct InclusionImpl {

  InclusionImpl(const std::vector<std::string> &includepaths) :
    includepaths(includepaths),
    source_map{
      .filecover = IntervalCover<std::string>("OUT_OF_RANGE"),
      .linecover = IntervalCover<int>(-1),
    } {}

  std::string ResolveInclude(const std::string &filename) {
    // TODO: Use include paths to resolve the file.
    return filename;
  }

  // Yuck! If I have to do any more with manipulating token streams,
  // I should instead build some more high-level routines for
  // tokens and their associated bytes.
  void Append(const std::string &filename) {
    // Lines are one-based.
    int current_line = 1;

    if (VERBOSE) {
      printf("Append %s\n", filename.c_str());
    }

    size_t this_file_pos = source.size();
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

    // Shift the coordinates of a token to refer to the source appended at
    // this_file_pos instead of 0.
    const auto ShiftToken = [&this_file_pos](Token t) {
        t.start += this_file_pos;
        return t;
    };

    const auto AppendByte = [this, &filename, &current_line](uint8_t c) {
        source_map.filecover.SetPoint(source.size(), filename);
        source_map.linecover.SetPoint(source.size(), current_line);
        if (c == '\n') current_line++;
        source.push_back(c);
      };

    size_t input_pos = 0;
    // Now append, processing recursively.
    for (int i = 0; i < (int)ftokens.size(); i++) {
      const Token &token = ftokens[i];

      if (token.type == el::IMPORT) [[unlikely]] {

        // We remove the import statement from the source, so
        // we need to know its length to adjust the offsets.
        const size_t import_start = token.start;

        // Copy any leading whitespace.
        while (input_pos < import_start) {
          AppendByte(contents[input_pos]);
          input_pos++;
        }

        i++;
        CHECK(i < (int)ftokens.size()) << "Lexing " << filename
                                       << ": File ends with IMPORT.";
        const Token &target = ftokens[i];
        CHECK(target.type == el::STR_LIT) << "Lexing " << filename << ": "
          "Expected string literal immediately after IMPORT.\n";

        std::string included_file =
          ResolveInclude(ReadStringLit(target));

        // Append recursively here.
        const size_t size_before = source.size();
        Append(included_file);
        const size_t size_after = source.size();
        CHECK(size_after >= size_before);


        // Skip the tokens and source characters corresponding to
        // the import.
        input_pos = target.start + target.length;
        const size_t bytes_deleted = input_pos - import_start;
        // For the remainder of this file, account for additional
        // shift due to the inserted text from the included file, and
        // also the text deleted from the include statement.
        if (VERBOSE) {
          printf("before %d, after %d.\n"
                 "deleted %d bytes\n"
                 "this_file was %d\n",
                 (int)size_before, (int)size_after,
                 (int)bytes_deleted,
                 (int)this_file_pos);
        }
        this_file_pos += (size_after - size_before);
        CHECK(this_file_pos >= bytes_deleted);
        this_file_pos -= bytes_deleted;
        if (VERBOSE) {
          printf("Now input_pos %d, this_file %d\n",
                 (int)input_pos, (int)this_file_pos);
        }
      } else {
        // Normal tokens.

        // Copy the token, including any leading whitespace.
        while (input_pos < token.start + token.length) {
          AppendByte(contents[input_pos]);
          input_pos++;
        }

        // Now copy the token.
        tokens.push_back(ShiftToken(token));
      }
    }

    // Include untokenized trailing whitespace.
    while (input_pos < contents.size()) {
      AppendByte(contents[input_pos]);
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

SourceMap Inclusion::SimpleSourceMap(const std::string &file,
                                     const std::string &content) {
  SourceMap source_map{
    .filecover = IntervalCover<std::string>{"OUT_OF_RANGE"},
    .linecover = IntervalCover<int>{-1},
  };

  source_map.filecover.SetSpan(0, content.size(), file);

  int current_line = 0;
  for (size_t idx = 0; idx < content.size(); idx++) {
    char c = content[idx];
    source_map.linecover.SetPoint(idx, current_line);
    if (c == '\n') current_line++;
  }

  return source_map;
}
