// Fixes text encodings. FIXME: Incomplete and does nothing!

// This file only: A rough port of "ftfy" ("fixes text for you") by
// Robyn Speer (see APACHE20.txt for license).

#include "fix-encoding.h"

#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "chardata.h"
#include "hexdump.h"
#include "re2-util.h"
#include "re2/re2.h"
#include "text-codec.h"
#include "utf8.h"

using RE2 = re2::RE2;

// These are character classes that are used for "badness" heuristics.


static_assert(sizeof("€") == 4 &&
              "€"[0] == '\xE2' && "€"[1] == '\x82' && "€"[2] == '\xAC' &&
              "This library requires string literals to be UTF-8 encoded.");

// Characters that appear in many different contexts. Sequences that contain
// them are not inherently mojibake
static std::string_view COMMON = {
  "\u00a0"  // NO-BREAK SPACE
  "\u00ad"  // SOFT HYPHEN
  "\u00b7"  // MIDDLE DOT
  "\u00b4"  // ACUTE ACCENT
  "\u2013"  // EN DASH
  "\u2014"  // EM DASH
  "\u2015"  // HORIZONTAL BAR
  "\u2026"  // HORIZONTAL ELLIPSIS
  "\u2019"  // RIGHT SINGLE QUOTATION MARK
};

// the C1 control character range, which have no uses outside of mojibake anymore
// Note that ftfy often uses something like \x80 in a string, which could
// (especially in this context!) be taken to mean the byte 0x80. But if the
// string is not using the b (binary) prefix, it always consists of unicode
// codepoints. So we actually want \xYY to be written \u00YY in C++.
static std::string_view C1 = {
  "\u0080-\u009f",
};

// Characters that are nearly 100% used in mojibake
static std::string_view BAD = {
  "\u00a6"  // BROKEN BAR
  "\u00a4"  // CURRENCY SIGN
  "\u00a8"  // DIAERESIS
  // my beloved negation?? -tom7
  "\u00ac"  // NOT SIGN
  "\u00af"  // MACRON
  "\u00b8"  // CEDILLA
  "\u0192"  // LATIN SMALL LETTER F WITH HOOK
  // it's not a modifier
  "\u02c6"  // MODIFIER LETTER CIRCUMFLEX ACCENT
  "\u02c7"  // CARON
  "\u02d8"  // BREVE
  "\u02db"  // OGONEK
  "\u02dc"  // SMALL TILDE
  "\u2020"  // DAGGER
  "\u2021"  // DOUBLE DAGGER
  "\u2030"  // PER MILLE SIGN
  "\u2310"  // REVERSED NOT SIGN
  "\u25ca"  // LOZENGE
  "\ufffd"
  // Theoretically these would appear in 'numeric' contexts, but when they
  // co-occur with other mojibake characters, it's not really ambiguous
  "\u00aa"  // FEMININE ORDINAL INDICATOR
  "\u00ba"  // MASCULINE ORDINAL INDICATOR
};

// Characters used in legalese.
static std::string_view LAW = {
  "\u00b6"  // PILCROW SIGN,
  "\u00a7"  // SECTION SIGN,
};

static std::string_view CURRENCY = {
  "\u00a2"  // CENT SIGN
  "\u00a3"  // POUND SIGN
  "\u00a5"  // YEN SIGN
  "\u20a7"  // PESETA SIGN
  "\u20ac"  // EURO SIGN
};

static std::string_view START_PUNCTUATION = {
  "\u00a1"  // INVERTED EXCLAMATION MARK
  "\u00ab"  // LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
  "\u00bf"  // INVERTED QUESTION MARK
  "\u00a9"  // COPYRIGHT SIGN
  "\u0384"  // GREEK TONOS
  "\u0385"  // GREEK DIALYTIKA TONOS
  "\u2018"  // LEFT SINGLE QUOTATION MARK
  "\u201a"  // SINGLE LOW-9 QUOTATION MARK
  "\u201c"  // LEFT DOUBLE QUOTATION MARK
  "\u201e"  // DOUBLE LOW-9 QUOTATION MARK
  "\u2022"  // BULLET
  "\u2039"  // SINGLE LEFT-POINTING ANGLE QUOTATION MARK
  // OS-specific symbol, usually the Apple logo
  "\uf8ff"
};

static std::string_view END_PUNCTUATION = {
  "\u00ae"  // REGISTERED SIGN
  "\u00bb"  // RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
  "\u02dd"  // DOUBLE ACUTE ACCENT
  "\u201d"  // RIGHT DOUBLE QUOTATION MARK
  "\u203a"  // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK
  "\u2122"  // TRADE MARK SIGN
};

static std::string_view NUMERIC = {
  "\u00b2"  // SUPERSCRIPT TWO
  "\u00b3"  // SUPERSCRIPT THREE
  "\u00b9"  // SUPERSCRIPT ONE
  "\u00b1"  // PLUS-MINUS SIGN
  "\u00bc"  // VULGAR FRACTION ONE QUARTER
  "\u00bd"  // VULGAR FRACTION ONE HALF
  "\u00be"  // VULGAR FRACTION THREE QUARTERS
  "\u00d7"  // MULTIPLICATION SIGN
  "\u00b5"  // MICRO SIGN
  "\u00f7"  // DIVISION SIGN
  "\u2044"  // FRACTION SLASH
  "\u2202"  // PARTIAL DIFFERENTIAL
  "\u2206"  // INCREMENT
  "\u220f"  // N-ARY PRODUCT
  "\u2211"  // N-ARY SUMMATION
  "\u221a"  // SQUARE ROOT
  "\u221e"  // INFINITY
  "\u2229"  // INTERSECTION
  "\u222b"  // INTEGRAL
  "\u2248"  // ALMOST EQUAL TO
  "\u2260"  // NOT EQUAL TO
  "\u2261"  // IDENTICAL TO
  "\u2264"  // LESS-THAN OR EQUAL TO
  "\u2265"  // GREATER-THAN OR EQUAL TO
  "\u2116"  // NUMERO SIGN
};

// Letters that might be used to make emoticon faces (kaomoji), and
// therefore might need to appear in more improbable-looking contexts.
//
// These are concatenated character ranges for use in a regex. I know
// they look like faces themselves. I think expressing the ranges like
// this helps to illustrate why we need to be careful with these
// characters.
static std::string_view KAOMOJI = {
  "Ò-Ö"
  "Ù-Ü"
  "ò-ö"
  "ø-ü"
  "\u0150"  // LATIN CAPITAL LETTER O WITH DOUBLE ACUTE
  "\u014c"  // LATIN CAPITAL LETTER O WITH MACRON
  "\u016a"  // LATIN CAPITAL LETTER U WITH MACRON
  "\u0172"  // LATIN CAPITAL LETTER U WITH OGONEK
  "\u00b0"  // DEGREE SIGN
};

static std::string_view UPPER_ACCENTED = {
  // LATIN CAPITAL LETTER A WITH GRAVE - LATIN CAPITAL LETTER N WITH TILDE
  "\u00c0-\u00d1"
  // skip capital O's and U's that could be used in kaomoji, but
  // include Ø because it's very common in Arabic mojibake:
  "\u00d8"  // LATIN CAPITAL LETTER O WITH STROKE
  "\u00dc"  // LATIN CAPITAL LETTER U WITH DIAERESIS
  "\u00dd"  // LATIN CAPITAL LETTER Y WITH ACUTE
  "\u0102"  // LATIN CAPITAL LETTER A WITH BREVE
  "\u0100"  // LATIN CAPITAL LETTER A WITH MACRON
  "\u0104"  // LATIN CAPITAL LETTER A WITH OGONEK
  "\u0106"  // LATIN CAPITAL LETTER C WITH ACUTE
  "\u010c"  // LATIN CAPITAL LETTER C WITH CARON
  "\u010e"  // LATIN CAPITAL LETTER D WITH CARON
  "\u0110"  // LATIN CAPITAL LETTER D WITH STROKE
  "\u0118"  // LATIN CAPITAL LETTER E WITH OGONEK
  "\u011a"  // LATIN CAPITAL LETTER E WITH CARON
  "\u0112"  // LATIN CAPITAL LETTER E WITH MACRON
  "\u0116"  // LATIN CAPITAL LETTER E WITH DOT ABOVE
  "\u011e"  // LATIN CAPITAL LETTER G WITH BREVE
  "\u0122"  // LATIN CAPITAL LETTER G WITH CEDILLA
  "\u0130"  // LATIN CAPITAL LETTER I WITH DOT ABOVE
  "\u012a"  // LATIN CAPITAL LETTER I WITH MACRON
  "\u0136"  // LATIN CAPITAL LETTER K WITH CEDILLA
  "\u0139"  // LATIN CAPITAL LETTER L WITH ACUTE
  "\u013d"  // LATIN CAPITAL LETTER L WITH CARON
  "\u0141"  // LATIN CAPITAL LETTER L WITH STROKE
  "\u013b"  // LATIN CAPITAL LETTER L WITH CEDILLA
  "\u0143"  // LATIN CAPITAL LETTER N WITH ACUTE
  "\u0147"  // LATIN CAPITAL LETTER N WITH CARON
  "\u0145"  // LATIN CAPITAL LETTER N WITH CEDILLA
  "\u0152"  // LATIN CAPITAL LIGATURE OE
  "\u0158"  // LATIN CAPITAL LETTER R WITH CARON
  "\u015a"  // LATIN CAPITAL LETTER S WITH ACUTE
  "\u015e"  // LATIN CAPITAL LETTER S WITH CEDILLA
  "\u0160"  // LATIN CAPITAL LETTER S WITH CARON
  "\u0162"  // LATIN CAPITAL LETTER T WITH CEDILLA
  "\u0164"  // LATIN CAPITAL LETTER T WITH CARON
  "\u016e"  // LATIN CAPITAL LETTER U WITH RING ABOVE
  "\u0170"  // LATIN CAPITAL LETTER U WITH DOUBLE ACUTE
  "\u0178"  // LATIN CAPITAL LETTER Y WITH DIAERESIS
  "\u0179"  // LATIN CAPITAL LETTER Z WITH ACUTE
  "\u017b"  // LATIN CAPITAL LETTER Z WITH DOT ABOVE
  "\u017d"  // LATIN CAPITAL LETTER Z WITH CARON
  "\u0490"  // CYRILLIC CAPITAL LETTER GHE WITH UPTURN
};

static std::string_view LOWER_ACCENTED = {
  "\u00df"  // LATIN SMALL LETTER SHARP S
  // LATIN SMALL LETTER A WITH GRAVE - LATIN SMALL LETTER N WITH TILDE
  "\u00e0-\u00f1"
  // skip o's and u's that could be used in kaomoji
  "\u0103"  // LATIN SMALL LETTER A WITH BREVE
  "\u0105"  // LATIN SMALL LETTER A WITH OGONEK
  "\u0101"  // LATIN SMALL LETTER A WITH MACRON
  "\u0107"  // LATIN SMALL LETTER C WITH ACUTE
  "\u010d"  // LATIN SMALL LETTER C WITH CARON
  "\u010f"  // LATIN SMALL LETTER D WITH CARON
  "\u0111"  // LATIN SMALL LETTER D WITH STROKE
  "\u0119"  // LATIN SMALL LETTER E WITH OGONEK
  "\u011b"  // LATIN SMALL LETTER E WITH CARON
  "\u0113"  // LATIN SMALL LETTER E WITH MACRON
  "\u0117"  // LATIN SMALL LETTER E WITH DOT ABOVE
  "\u011f"  // LATIN SMALL LETTER G WITH BREVE
  "\u0123"  // LATIN SMALL LETTER G WITH CEDILLA
  "\u012f"  // LATIN SMALL LETTER I WITH OGONEK
  "\u012b"  // LATIN SMALL LETTER I WITH MACRON
  "\u0137"  // LATIN SMALL LETTER K WITH CEDILLA
  "\u013a"  // LATIN SMALL LETTER L WITH ACUTE
  "\u013e"  // LATIN SMALL LETTER L WITH CARON
  "\u0142"  // LATIN SMALL LETTER L WITH STROKE
  "\u013c"  // LATIN SMALL LETTER L WITH CEDILLA
  "\u0153"  // LATIN SMALL LIGATURE OE
  "\u0155"  // LATIN SMALL LETTER R WITH ACUTE
  "\u015b"  // LATIN SMALL LETTER S WITH ACUTE
  "\u015f"  // LATIN SMALL LETTER S WITH CEDILLA
  "\u0161"  // LATIN SMALL LETTER S WITH CARON
  "\u0165"  // LATIN SMALL LETTER T WITH CARON
  "\u00fc"  // LATIN SMALL LETTER U WITH DIAERESIS
  "\u017a"  // LATIN SMALL LETTER Z WITH ACUTE
  "\u017c"  // LATIN SMALL LETTER Z WITH DOT ABOVE
  "\u017e"  // LATIN SMALL LETTER Z WITH CARON
  "\u0491"  // CYRILLIC SMALL LETTER GHE WITH UPTURN
  "\ufb01"  // LATIN SMALL LIGATURE FI
  "\ufb02"  // LATIN SMALL LIGATURE FL
};

static std::string_view UPPER_COMMON = {
  "\u00de"  // LATIN CAPITAL LETTER THORN
  // GREEK CAPITAL LETTER ALPHA - GREEK CAPITAL LETTER OMEGA
  "\u0391-\u03a9"
  // not included under 'accented' because these can commonly
  // occur at ends of words, in positions where they'd be detected
  // as mojibake
  "\u0386"  // GREEK CAPITAL LETTER ALPHA WITH TONOS
  "\u0388"  // GREEK CAPITAL LETTER EPSILON WITH TONOS
  "\u0389"  // GREEK CAPITAL LETTER ETA WITH TONOS
  "\u038a"  // GREEK CAPITAL LETTER IOTA WITH TONOS
  "\u038c"  // GREEK CAPITAL LETTER OMICRON WITH TONOS
  "\u038e"  // GREEK CAPITAL LETTER UPSILON WITH TONOS
  "\u038f"  // GREEK CAPITAL LETTER OMEGA WITH TONOS
  "\u03aa"  // GREEK CAPITAL LETTER IOTA WITH DIALYTIKA
  "\u03ab"  // GREEK CAPITAL LETTER UPSILON WITH DIALYTIKA
  // CYRILLIC CAPITAL LETTER IO - CYRILLIC CAPITAL LETTER YA
  "\u0401-\u042f"
};

static std::string_view LOWER_COMMON = {
  // lowercase thorn does not appear in mojibake
  // GREEK SMALL LETTER ALPHA - GREEK SMALL LETTER OMEGA
  "\u03b1-\u03c9"
  "\u03ac"  // GREEK SMALL LETTER ALPHA WITH TONOS
  "\u03ad"  // GREEK SMALL LETTER EPSILON WITH TONOS
  "\u03ae"  // GREEK SMALL LETTER ETA WITH TONOS
  "\u03af"  // GREEK SMALL LETTER IOTA WITH TONOS
  "\u03b0"  // GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND TONOS
  // CYRILLIC SMALL LETTER A - CYRILLIC SMALL LETTER DZHE
  "\u0430-\u045f"
};

static std::string_view BOX = {
  // omit the single horizontal line, might be used in kaomoji
  "│┌┐┘├┤┬┼"
  // BOX DRAWINGS DOUBLE HORIZONTAL - BOX DRAWINGS DOUBLE VERTICAL
  //   AND HORIZONTAL
  "\u2550-\u256c"
  "▀▄█▌▐░▒▓"
};

// We can now build a regular expression that detects unlikely juxtapositions
// of characters, mostly based on their categories.
//
// Another regular expression, which detects sequences that look more specifically
// like UTF-8 mojibake, appears in chardata.py.
//
// This is a verbose regular expression, with whitespace added for somewhat more
// readability. Remember that the only spaces that count as literal spaces in this
// expression are ones inside character classes (square brackets).

template<class ...Ts>
static void AppendStrs(std::string *out, Ts &&...ts) {
  ( (out->append(std::format("{}", std::forward<Ts>(ts)))), ... );
}

static std::string MakeBadnessRE() {
  std::string regex;
  auto Bar = [&regex](){ regex.push_back('|'); };

  AppendStrs(&regex, "[", C1, "]");
  Bar();
  AppendStrs(&regex, "[",
             BAD, LOWER_ACCENTED, UPPER_ACCENTED, BOX, START_PUNCTUATION,
             END_PUNCTUATION, CURRENCY, NUMERIC, LAW, "][", BAD, "]");
  Bar();
  AppendStrs(&regex, "[a-zA-Z][", LOWER_COMMON, UPPER_COMMON, "][", BAD, "]");
  Bar();
  AppendStrs(&regex, "[", BAD, "][", LOWER_ACCENTED, UPPER_ACCENTED, BOX,
             START_PUNCTUATION, END_PUNCTUATION, CURRENCY, NUMERIC, LAW, "]");
  Bar();

  AppendStrs(&regex, "[", LOWER_ACCENTED, LOWER_COMMON, BOX, END_PUNCTUATION,
             CURRENCY, NUMERIC, "][", UPPER_ACCENTED, "]");
  Bar();
  AppendStrs(&regex, "[", BOX, END_PUNCTUATION, CURRENCY, NUMERIC, "][",
             LOWER_ACCENTED, "]");
  Bar();

  AppendStrs(&regex, "[", LOWER_ACCENTED, BOX, END_PUNCTUATION, "][", CURRENCY, "]");
  Bar();
  AppendStrs(&regex, "\\s[", UPPER_ACCENTED, "][", CURRENCY, "]");
  Bar();

  AppendStrs(&regex, "[", UPPER_ACCENTED, BOX, "][", NUMERIC, LAW, "]");
  Bar();

  AppendStrs(&regex, "[", LOWER_ACCENTED, UPPER_ACCENTED, BOX, CURRENCY,
             END_PUNCTUATION, "][", START_PUNCTUATION, "][", NUMERIC, "]");
  Bar();

  AppendStrs(&regex, "[", LOWER_ACCENTED, UPPER_ACCENTED, CURRENCY, NUMERIC, BOX,
             LAW, "][", END_PUNCTUATION, "][", START_PUNCTUATION, "]");
  Bar();

  AppendStrs(&regex, "[", CURRENCY, NUMERIC, BOX, "][", START_PUNCTUATION, "]");
  Bar();

  AppendStrs(&regex, "[a-z][", UPPER_ACCENTED, "][", START_PUNCTUATION, CURRENCY, "]");
  Bar();

  AppendStrs(&regex, "[", BOX, "][", KAOMOJI, "]");
  Bar();

  AppendStrs(&regex, "[", LOWER_ACCENTED, UPPER_ACCENTED, CURRENCY, NUMERIC,
             START_PUNCTUATION, END_PUNCTUATION, LAW, "][", BOX, "]");
  Bar();

  AppendStrs(&regex, "[", BOX, "][", END_PUNCTUATION, "]");
  Bar();

  AppendStrs(&regex, "[", LOWER_ACCENTED, UPPER_ACCENTED, "][", START_PUNCTUATION,
             END_PUNCTUATION, "]\\w");
  Bar();

  // The ligature œ when not followed by an unaccented Latin letter
  AppendStrs(&regex, "[Œœ][^A-Za-z]");
  Bar();

  // Degree signs after capital letters
  AppendStrs(&regex, "[", UPPER_ACCENTED, "]°");
  Bar();

  // Common Windows-1252 2-character mojibake that isn't covered above.
  AppendStrs(&regex, "[ÂÃÎÐ][€œŠš¢£Ÿž\u00a0\u00ad®©°·»", START_PUNCTUATION,
             END_PUNCTUATION, "–—´]");
  Bar();
  AppendStrs(&regex, "×[²³]");
  Bar();

  // Windows-1252 mojibake of Arabic words needs to include 'common' characters.
  // To compensate, require four characters to be matched.
  AppendStrs(&regex,
             "[ØÙ][", COMMON, CURRENCY, BAD, NUMERIC, START_PUNCTUATION,
             "ŸŠ®°µ»]"
             "[ØÙ][", COMMON, CURRENCY, BAD, NUMERIC, START_PUNCTUATION,
             "ŸŠ®°µ»]");
  Bar();

  // Windows-1252 mojibake that starts 3-char sequences for some South Asian alphabets.
  AppendStrs(&regex, "à[²µ¹¼½¾]");
  Bar();

  // MacRoman mojibake that isn't covered by the cases above.
  AppendStrs(&regex, "√[±∂†≠®™´≤≥¥µø]");
  Bar();
  AppendStrs(&regex, "≈[°¢]");
  Bar();
  AppendStrs(&regex, "‚Ä[ìîïòôúùû†°¢π]");
  Bar();
  AppendStrs(&regex, "‚[âó][àä°ê]");
  Bar();

  // Windows-1251 mojibake of characters in the U+2000 range.
  AppendStrs(&regex, "вЂ");
  Bar();

  // Windows-1251 mojibake of Latin-1 characters and/or the Cyrillic alphabet.
  // Require a 3-character sequence because the 2-char sequences can be common.
  AppendStrs(&regex, "[ВГРС][", C1, BAD, START_PUNCTUATION, END_PUNCTUATION,
             CURRENCY, "°µ][ВГРС]");
  Bar();

  // A distinctive five-character sequence of Cyrillic letters.
  // Require a Latin letter nearby.
  AppendStrs(&regex, "ГўВЂВ.[A-Za-z ]");
  Bar();

  // Windows-1252 encodings of 'à' and 'á', as well as NO-BREAK SPACE itself.
  AppendStrs(&regex, "Ã[\u00a0¡]");
  Bar();
  AppendStrs(&regex, "[a-z]\\s?[ÃÂ][ ]");
  Bar();
  AppendStrs(&regex, "^[ÃÂ][ ]");
  Bar();


  // Cases where Â precedes a character as an encoding of exactly that character.
  AppendStrs(&regex, "[a-z.,?!", END_PUNCTUATION, "]Â[ ", START_PUNCTUATION,
             END_PUNCTUATION, "]");
  Bar();

  // Windows-1253 (Greek) mojibake of characters in the U+2000 range.
  AppendStrs(&regex, "β€[™\u00a0Ά\u00ad®°]");
  Bar();

  // Windows-1253 mojibake of Latin-1 characters and/or the Greek alphabet.
  AppendStrs(&regex, "[ΒΓΞΟ][", C1, BAD, START_PUNCTUATION, END_PUNCTUATION,
             CURRENCY, "°][ΒΓΞΟ]");
  Bar();

  // Windows-1257 mojibake of characters in the U+2000 range
  AppendStrs(&regex, "ā€");

  return regex;
}


static const RE2 &BadnessRegex() {
  static RE2 *re = new RE2(MakeBadnessRE());
  return *re;
}

// Get the 'badness' of a sequence of text, counting the number of unlikely
// character sequences. A badness greater than 0 indicates that some of it
[[maybe_unused]]
// seems to be mojibake.
static int Badness(std::string_view s) {
  const RE2 &re = BadnessRegex();
  int count = 0;
  while (RE2::FindAndConsume(&s, re)) count++;
  return count;
}

static inline bool IsASCII(std::string_view text) {
  for (const uint8_t c : text) {
    if (c >= 0x80) return false;
  }
  return true;
}

std::optional<std::string> FixEncoding::DecodeVariantUTF8(std::string_view bytes) {
  std::string out;
  out.reserve(bytes.size());

  while (!bytes.empty()) {

    // Java null: \xC0\x80 -> \0
    if ((uint8_t)bytes[0] == 0xc0) {
      if (bytes.size() >= 2) {
        if ((uint8_t)bytes[1] == 0x80) {
          out.push_back('\0');
          bytes.remove_prefix(2);
          continue;
        }
      }
      // Otherwise this cannot be valid variant UTF-8.
      return std::nullopt;
    }

    uint32_t cp = UTF8::ConsumePrefix(&bytes);
    if (cp == UTF8::INVALID)
      return std::nullopt;

    // Handle CESU-8 surrogate pairs.
    // 0xED is the start byte for the range U+D000..U+DFFF.
    if (cp >= 0xD800 && cp <= 0xDBFF) {

      // Is it followed by a low surrogate?
      std::string_view lookahead = bytes;
      uint32_t next_cp = UTF8::ConsumePrefix(&lookahead);
      // For emphasis: INVALID would not be in range anyway, but we don't
      // want to immediately return because it could be the Java null sequence.
      if (next_cp != UTF8::INVALID &&
          next_cp >= 0xDC00 && next_cp <= 0xDFFF) {
        uint32_t full = 0x10000 + (cp - 0xD800) * 0x400 + (next_cp - 0xDC00);
        out.append(UTF8::Encode(full));
        bytes = lookahead;
        continue;
      }
    }

    // If we didn't find a matching low surrogate, we just put it back in the
    // string, following ftfy.
    out.append(UTF8::Encode(cp));
  }

  return {out};
}

// This regex matches C1 control characters, which occupy some of the positions
// in the Latin-1 character map that Windows assigns to other characters instead.
static LazyRE2 C1_CONTROL_RE = { "[\u0080-\u009f]" };

static inline bool HasC1Controls(std::string_view str) {
  return RE2::PartialMatch(str, *C1_CONTROL_RE);
}

// If text still contains C1 control characters, treat them as their
// Windows-1252 equivalents. This matches what Web browsers do.
std::string FixEncoding::FixC1Controls(std::string_view str) {
  auto C1Fixer = [](std::span<const std::string_view> match) {
      std::optional<std::string> lat = Latin1().Encode(match[0]);
      CHECK(lat.has_value()) << "Latin-1 should always succeed for codepoints "
        "in this range.";
      return Windows1252().DecodeSloppy(lat.value());
    };

  return RE2Util::MapReplacement(str, *C1_CONTROL_RE, C1Fixer);
}

enum class Encoding {
  LATIN1,
  WINDOWS1252,
  WINDOWS1251,
  WINDOWS1250,
  WINDOWS1253,
  WINDOWS1254,
  WINDOWS1257,
  ISO8859_2,
  MACROMAN,
  CP437,
};

// These are the encodings we will try to fix, in the
// order that they should be tried. Follows ftfy. If the
// boolean is true, the "sloppy" version of the codec should
// be used.
static std::vector<std::tuple<Encoding, bool, const TextCodec *>> ENCODINGS = {
  {Encoding::LATIN1, false, &Latin1()},
  {Encoding::WINDOWS1252, true, &Windows1252()},
  {Encoding::WINDOWS1251, true, &Windows1251()},
  {Encoding::WINDOWS1250, true, &Windows1250()},
  {Encoding::WINDOWS1253, true, &Windows1253()},
  {Encoding::WINDOWS1254, true, &Windows1254()},
  {Encoding::WINDOWS1257, true, &Windows1257()},
  {Encoding::ISO8859_2, false, &ISO8859_2()},
  {Encoding::MACROMAN, false, &MacRoman()},
  {Encoding::CP437, false, &CP437()},
};

static std::string FixOneStep(std::string_view text) {
  std::vector<Encoding> possible_1byte_encodings;
  // We iterate through common single-byte encodings to see if the text
  // decodes successfully from them.
  for (const auto &[name, sloppy, codec] : ENCODINGS) {
    std::optional<std::string> encoded =
      sloppy ? codec->EncodeSloppy(text) : codec->Encode(text);
    if (!encoded.has_value()) continue;

    possible_1byte_encodings.push_back(name);
    std::string bytes = std::move(encoded.value());

    // Restore 0xA0 (NBSP) if it was converted to space. We skip this
    // for Mac Roman because it produces false positives with en
    // dashes.
    if (name != Encoding::MACROMAN) {
      bytes = RestoreByteA0(bytes);
    }

    // For sloppy codecs, undo the 0x1A hack so that we have the
    // proper unicode replacement character again.
    if (sloppy) {
      bytes = ReplaceLossySequences(bytes);
    }

    // Check if we need the "utf-8-variants" decoder.
    // This handles CESU-8 and Java's "Modified UTF-8" (nulls).
    bool use_variants = false;
    for (uint8_t c : bytes) {
      if (c == 0xED || c == 0xC0) {
        use_variants = true;
        break;
      }
    }

    if (use_variants) {
      if (auto decoded = FixEncoding::DecodeVariantUTF8(bytes)) {
        return decoded.value();
      }
    } else {
      // If no variants are suspected and it is already valid UTF-8,
      // then we are done.
      if (UTF8::IsValid(bytes)) {
        return bytes;
      }
    }
  }

  // Look for inconsistent UTF-8 (e.g. "Ã " patterns) that couldn't be
  // solved by the single-byte method above.
  std::string fixed = DecodeInconsistentUTF8(text);
  if (fixed != text) {
    return fixed;
  }

  // Latin-1 vs Windows-1252 Heuristic.
  // If text encodes as Latin-1 but NOT Windows-1252, it implies C1
  // control characters. We assume those were meant to be
  // Windows-1252.
  bool can_be_latin1 = false;
  bool can_be_w1252 = false;
  for (Encoding name : possible_1byte_encodings) {
    if (name == Encoding::LATIN1) can_be_latin1 = true;
    if (name == Encoding::WINDOWS1252) can_be_w1252 = true;
  }

  if (can_be_latin1) {
    if (can_be_w1252) {
      // It fits both. It's probably legitimate text (e.g. just "é").
      return std::string(text);
    } else {
      // It has C1 controls. Transcode Latin-1 -> Windows-1252.
      // Note: Latin1::Encode always succeeds if the previous check passed.
      // Windows1252::DecodeSloppy always succeeds.
      if (auto bytes = Latin1().Encode(text)) {
        return Windows1252().DecodeSloppy(bytes.value());
      }
    }
  }

  // Last ditch: Fix C1 controls directly.
  if (HasC1Controls(text)) {
    return FixEncoding::FixC1Controls(text);
  }

  // No fixes found.
  return std::string(text);
}

static void RepeatedlyFix(std::string *text) {
  // PERF: Perhaps we shouldn't keep testing ascii, but just do
  // that as an initial step in Fix.
  while (!IsASCII(*text) && FixEncoding::IsBad(*text)) {
    std::string fixed = FixOneStep(*text);
    if (fixed == *text)
      return;

    *text = std::move(fixed);
  }
}

// Returns true iff the given text looks like it contains mojibake.
//
// This can be faster than Badness, because it returns when the first match
// is found to a regex instead of counting matches. Note that as strings get
// longer, they have a higher chance of returning true for IsBad().
bool FixEncoding::IsBad(std::string_view s) {
  return RE2::PartialMatch(s, BadnessRegex());
}

std::string FixEncoding::Fix(std::string_view s, uint64_t fixmask) {
  std::string text(s);

  static constexpr bool VERBOSE = false;

  for (;;) {
    std::string orig = text;
    RepeatedlyFix(&text);
    if (VERBOSE) Print("After repeatedly fixing:\n{}\n",
                       HexDump::Color(text));

    text = FixC1Controls(text);
    if (VERBOSE) Print("After FixC1Controls:\n{}\n",
                       HexDump::Color(text));

    text = FixLatinLigatures(text);
    if (VERBOSE) Print("After FixLatinLigatures:\n{}\n",
                       HexDump::Color(text));

    if (fixmask & UNCURL_QUOTES) {
      text = UncurlQuotes(text);
      if (VERBOSE) Print("After UncurlQuotes:\n{}\n",
                         HexDump::Color(text));
    }

    text = FixLineBreaks(text);
    if (VERBOSE) Print("After FixLineBreaks:\n{}\n",
                       HexDump::Color(text));

    text = FixSurrogates(text);
    if (VERBOSE) Print("After FixSurrogates:\n{}\n",
                       HexDump::Color(text));

    text = RemoveTerminalEscapes(text);
    if (VERBOSE) Print("After RemoveTerminalEscapes:\n{}\n",
                       HexDump::Color(text));

    text = RemoveControlChars(text);
    if (VERBOSE) Print("After RemoveControlChars:\n{}\n",
                       HexDump::Color(text));

    // Port note: ftfy would do unicode normalization, but that
    // requires ICU or some other source of unicode data. We just
    // leave it unnormalized.

    if (text == orig) return text;
  }
}

