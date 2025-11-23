
// A partial port of ftfy, which fixes incorrectly-encoded UTF8.

#ifndef _CC_LIB_FIX_ENCODING_H
#define _CC_LIB_FIX_ENCODING_H

#include <optional>
#include <string>
#include <string_view>

struct FixEncoding {
  // Optional fixes.
  enum : uint64_t {
    // Translate stuff like “this” to "this".
    // There's nothing wrong with having this in text, so
    // it's not enabled by default.
    UNCURL_QUOTES = uint64_t{1} << 0,
  };

  static constexpr uint64_t DEFAULT_FIXMASK = 0;

  // str should be a UTF-8 string, but the point of this function
  // is to heuristically fix text that was encoded incorrectly,
  // producing a garbled result ("mojibake").
  static std::string Fix(std::string_view str,
                         uint64_t fixmask = DEFAULT_FIXMASK);

  // Is the string's encoding likely bad (not really UTF-8)?
  // This is heuristic.
  static bool IsBad(std::string_view str);


  // Advanced stuff, exposed mainly for testing.
  //
  // See the source files or ftfy for explanation.

  // Decode UTF-8 variants (if possible), returning regular UTF-8.
  // CESU-8 is surrogate pairs (for UTF-16 systems, awful) and Java is
  // that plus an additional code for nulls. Leaves unmatched surrogate
  // pairs in place.
  static std::optional<std::string> DecodeVariantUTF8(std::string_view bytes);

  // Replace C1 control characters by assuming they are actually
  // Latin-1 bytes that were misencoded.
  static std::string FixC1Controls(std::string_view str);
  static std::string RestoreByteA0(std::string_view text);
  static std::string ReplaceLossySequences(std::string_view str);
  static std::string RemoveBOM(std::string_view text);
  static std::string FixSurrogates(std::string_view text);
  static std::string FixLineBreaks(std::string_view text);
  static std::string FixLatinLigatures(std::string_view text);
  static std::string UncurlQuotes(std::string_view text);
  static std::string RemoveTerminalEscapes(std::string_view text);
  static std::string DecodeInconsistentUTF8(std::string_view text);

};

#endif
