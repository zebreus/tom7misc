
// A partial port of ftfy, which fixes incorrectly-encoded UTF8.

#ifndef _CC_LIB_FIX_ENCODING_H
#define _CC_LIB_FIX_ENCODING_H

#include <cstdint>
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
    // Replace something like "ﬂan" with "flan".
    LATIN_LIGATURES = uint64_t{1} << 1,
  };

  static constexpr uint64_t DEFAULT_FIXMASK = LATIN_LIGATURES;

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

  // In python, an encoder takes a sequence of codepoints and returns
  // a sequence of bytes.
  // In this C++ port, we represent both of these as std::string. A
  // sequence of codepoints is expected to be UTF-8 encoded. A sequence
  // of bytes is a string containing anything.
  struct TextCodec {
    virtual ~TextCodec() {}
    virtual std::optional<std::string> Encode(std::string_view str) const = 0;
    virtual std::optional<std::string> EncodeSloppy(std::string_view str) const = 0;
    virtual std::optional<std::string> Decode(std::string_view bytes) const = 0;
    virtual std::string DecodeSloppy(std::string_view bytes) const = 0;
  };

  // Singletons
  static const TextCodec &Latin1();
  static const TextCodec &Windows1252();
  static const TextCodec &Windows1251();
  static const TextCodec &Windows1250();
  static const TextCodec &Windows1253();
  static const TextCodec &Windows1254();
  static const TextCodec &Windows1257();
  static const TextCodec &ISO8859_2();
  static const TextCodec &MacRoman();
  static const TextCodec &CP437();
};

#endif
