// This file only: A rough port of "ftfy" ("fixes text for you") by
// Robyn Speer (see APACHE20.txt for license).

#include "fix-encoding.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "html-entities.h"
#include "re2-util.h"
#include "re2/re2.h"
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
// seems to be mojibake.
[[maybe_unused]]
static int Badness(std::string_view s) {
  const RE2 &re = BadnessRegex();
  int count = 0;
  while (RE2::FindAndConsume(&s, re)) count++;
  return count;
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

// -- stuff from chardata --

// In RE2, setting Latin1 mode allows the input regex to have
// invalid UTF-8, which we need. Note that this affects the match, too.
static constexpr RE2::CannedOptions BINARY_REGEX = RE2::CannedOptions::Latin1;

// HTML entities are in html-entities.h.
// Note that ftfy will also decode some nonstandard ones like
// "NTILDE" (should be Ntilde) to handle cases where the string
// was uppercased in ASCII. Not done here.
std::string UnescapeHTML(std::string_view text) {
  // {1,24} means from one to 24 repetitions, inclusive.
  static LazyRE2 HTML_ENTITY_RE = { "&(#?[0-9A-Za-z]{1,24});" };
  auto ReplaceEntity = [](std::span<const std::string_view> match) -> std::string {
      std::string_view ent = match[1];
      CHECK(!ent.empty());
      if (ent[0] == '#') {
        // TODO: Implement numeric entities.
        return std::string(match[0]);
      } else {
        if (std::optional<std::string> dec = HTMLEntities::GetEntity(ent)) {
          return dec.value();
        } else {
          return std::string(match[0]);
        }
      }
    };

  return RE2Util::MapReplacement(text, *HTML_ENTITY_RE, ReplaceEntity);
}

// Set of likely-unintended control characters.
static std::unordered_set<uint32_t> ControlChars() {
  std::unordered_set<uint32_t> ret;
  auto Range = [&ret](uint32_t lo, uint32_t hi) {
      for (uint32_t i = lo; i < hi; i++) {
        ret.insert(i);
      }
    };

  Range(0x0000, 0x0009);
  ret.insert(0x000B);
  Range(0x000E, 0x0020);
  ret.insert(0x007F);
  Range(0x206A, 0x2070);
  ret.insert(0xFEFF);
  Range(0xFFF9, 0xFFFD);

  return ret;
}

//  Remove various control characters that you probably didn't intend to be in
//  your text. Many of these characters appear in the table of "Characters not
//  suitable for use with markup" at
//  http://www.unicode.org/reports/tr20/tr20-9.html.
//
//  This includes:
//
//  - ASCII control characters, except for the important whitespace characters
//    (U+00 to U+08, U+0B, U+0E to U+1F, U+7F)
//  - Deprecated Arabic control characters (U+206A to U+206F)
//  - Interlinear annotation characters (U+FFF9 to U+FFFB)
//  - The Object Replacement Character (U+FFFC)
//  - The byte order mark (U+FEFF)
//
//  However, these similar characters are left alone:
//
//  - Control characters that produce whitespace (U+09, U+0A, U+0C, U+0D,
//    U+2028, and U+2029)
//  - C1 control characters (U+80 to U+9F) -- even though they are basically
//    never used intentionally, they are important clues about what mojibake
//    has happened
//  - Control characters that affect glyph rendering, such as joiners and
//    right-to-left marks (U+200C to U+200F, U+202A to U+202E)
//  - Musical notation control characters (U+1D173 to U+1D17A) because wow if
//    you're using those you probably have a good reason
//  - Tag characters, because they are now used in emoji sequences such as
//    "Flag of Wales"
std::string RemoveControlChars(std::string_view text) {
  static const std::unordered_set<uint32_t> cc = ControlChars();

  std::string ret;
  ret.reserve(text.size());

  for (uint32_t codepoint : UTF8::Decoder(text)) {
    if (!cc.contains(codepoint)) {
      // Encoding will always fit in SSO here, so there are no
      // allocations. But it might be good to have an EncodeTo?
      ret.append(UTF8::Encode(codepoint));
    }
  }

  return ret;
}

// Port note: I removed the width stuff. I don't see any reason to
// normalize away halfwidth/fullwidth forms. -tom7


// Character classes that help us pinpoint embedded mojibake. These can
// include common characters, because we'll also check them for 'badness'.

// Letters that decode to 0xC2 - 0xDF in a Latin-1-like encoding
static std::string_view UTF8_FIRST_OF_2 = {
  "\u0102"  // LATIN CAPITAL LETTER A WITH BREVE  # windows-1250:C3
  "\u00c2"  // LATIN CAPITAL LETTER A WITH CIRCUMFLEX  # latin-1:C2
  "\u00c4"  // LATIN CAPITAL LETTER A WITH DIAERESIS  # latin-1:C4
  "\u0100"  // LATIN CAPITAL LETTER A WITH MACRON  # windows-1257:C2
  "\u00c5"  // LATIN CAPITAL LETTER A WITH RING ABOVE  # latin-1:C5
  "\u00c3"  // LATIN CAPITAL LETTER A WITH TILDE  # latin-1:C3
  "\u00c6"  // LATIN CAPITAL LETTER AE  # latin-1:C6
  "\u0106"  // LATIN CAPITAL LETTER C WITH ACUTE  # windows-1250:C6
  "\u010c"  // LATIN CAPITAL LETTER C WITH CARON  # windows-1250:C8
  "\u00c7"  // LATIN CAPITAL LETTER C WITH CEDILLA  # latin-1:C7
  "\u010e"  // LATIN CAPITAL LETTER D WITH CARON  # windows-1250:CF
  "\u0110"  // LATIN CAPITAL LETTER D WITH STROKE  # windows-1250:D0
  "\u00c9"  // LATIN CAPITAL LETTER E WITH ACUTE  # latin-1:C9
  "\u011a"  // LATIN CAPITAL LETTER E WITH CARON  # windows-1250:CC
  "\u00ca"  // LATIN CAPITAL LETTER E WITH CIRCUMFLEX  # latin-1:CA
  "\u00cb"  // LATIN CAPITAL LETTER E WITH DIAERESIS  # latin-1:CB
  "\u0116"  // LATIN CAPITAL LETTER E WITH DOT ABOVE  # windows-1257:CB
  "\u00c8"  // LATIN CAPITAL LETTER E WITH GRAVE  # latin-1:C8
  "\u0112"  // LATIN CAPITAL LETTER E WITH MACRON  # windows-1257:C7
  "\u0118"  // LATIN CAPITAL LETTER E WITH OGONEK  # windows-1250:CA
  "\u00d0"  // LATIN CAPITAL LETTER ETH  # latin-1:D0
  "\u011e"  // LATIN CAPITAL LETTER G WITH BREVE  # windows-1254:D0
  "\u0122"  // LATIN CAPITAL LETTER G WITH CEDILLA  # windows-1257:CC
  "\u00cd"  // LATIN CAPITAL LETTER I WITH ACUTE  # latin-1:CD
  "\u00ce"  // LATIN CAPITAL LETTER I WITH CIRCUMFLEX  # latin-1:CE
  "\u00cf"  // LATIN CAPITAL LETTER I WITH DIAERESIS  # latin-1:CF
  "\u0130"  // LATIN CAPITAL LETTER I WITH DOT ABOVE  # windows-1254:DD
  "\u00cc"  // LATIN CAPITAL LETTER I WITH GRAVE  # latin-1:CC
  "\u012a"  // LATIN CAPITAL LETTER I WITH MACRON  # windows-1257:CE
  "\u0136"  // LATIN CAPITAL LETTER K WITH CEDILLA  # windows-1257:CD
  "\u0139"  // LATIN CAPITAL LETTER L WITH ACUTE  # windows-1250:C5
  "\u013b"  // LATIN CAPITAL LETTER L WITH CEDILLA  # windows-1257:CF
  "\u0141"  // LATIN CAPITAL LETTER L WITH STROKE  # windows-1257:D9
  "\u0143"  // LATIN CAPITAL LETTER N WITH ACUTE  # windows-1250:D1
  "\u0147"  // LATIN CAPITAL LETTER N WITH CARON  # windows-1250:D2
  "\u0145"  // LATIN CAPITAL LETTER N WITH CEDILLA  # windows-1257:D2
  "\u00d1"  // LATIN CAPITAL LETTER N WITH TILDE  # latin-1:D1
  "\u00d3"  // LATIN CAPITAL LETTER O WITH ACUTE  # latin-1:D3
  "\u00d4"  // LATIN CAPITAL LETTER O WITH CIRCUMFLEX  # latin-1:D4
  "\u00d6"  // LATIN CAPITAL LETTER O WITH DIAERESIS  # latin-1:D6
  "\u0150"  // LATIN CAPITAL LETTER O WITH DOUBLE ACUTE  # windows-1250:D5
  "\u00d2"  // LATIN CAPITAL LETTER O WITH GRAVE  # latin-1:D2
  "\u014c"  // LATIN CAPITAL LETTER O WITH MACRON  # windows-1257:D4
  "\u00d8"  // LATIN CAPITAL LETTER O WITH STROKE  # latin-1:D8
  "\u00d5"  // LATIN CAPITAL LETTER O WITH TILDE  # latin-1:D5
  "\u0158"  // LATIN CAPITAL LETTER R WITH CARON  # windows-1250:D8
  "\u015a"  // LATIN CAPITAL LETTER S WITH ACUTE  # windows-1257:DA
  "\u0160"  // LATIN CAPITAL LETTER S WITH CARON  # windows-1257:D0
  "\u015e"  // LATIN CAPITAL LETTER S WITH CEDILLA  # windows-1254:DE
  "\u0162"  // LATIN CAPITAL LETTER T WITH CEDILLA  # windows-1250:DE
  "\u00de"  // LATIN CAPITAL LETTER THORN  # latin-1:DE
  "\u00da"  // LATIN CAPITAL LETTER U WITH ACUTE  # latin-1:DA
  "\u00db"  // LATIN CAPITAL LETTER U WITH CIRCUMFLEX  # latin-1:DB
  "\u00dc"  // LATIN CAPITAL LETTER U WITH DIAERESIS  # latin-1:DC
  "\u0170"  // LATIN CAPITAL LETTER U WITH DOUBLE ACUTE  # windows-1250:DB
  "\u00d9"  // LATIN CAPITAL LETTER U WITH GRAVE  # latin-1:D9
  "\u016a"  // LATIN CAPITAL LETTER U WITH MACRON  # windows-1257:DB
  "\u0172"  // LATIN CAPITAL LETTER U WITH OGONEK  # windows-1257:D8
  "\u016e"  // LATIN CAPITAL LETTER U WITH RING ABOVE  # windows-1250:D9
  "\u00dd"  // LATIN CAPITAL LETTER Y WITH ACUTE  # latin-1:DD
  "\u0179"  // LATIN CAPITAL LETTER Z WITH ACUTE  # windows-1257:CA
  "\u017d"  // LATIN CAPITAL LETTER Z WITH CARON  # windows-1257:DE
  "\u017b"  // LATIN CAPITAL LETTER Z WITH DOT ABOVE  # windows-1257:DD
  "\u00df"  // LATIN SMALL LETTER SHARP S  # latin-1:DF
  "\u00d7"  // MULTIPLICATION SIGN  # latin-1:D7
  "\u0392"  // GREEK CAPITAL LETTER BETA  # windows-1253:C2
  "\u0393"  // GREEK CAPITAL LETTER GAMMA  # windows-1253:C3
  "\u0394"  // GREEK CAPITAL LETTER DELTA  # windows-1253:C4
  "\u0395"  // GREEK CAPITAL LETTER EPSILON  # windows-1253:C5
  "\u0396"  // GREEK CAPITAL LETTER ZETA  # windows-1253:C6
  "\u0397"  // GREEK CAPITAL LETTER ETA  # windows-1253:C7
  "\u0398"  // GREEK CAPITAL LETTER THETA  # windows-1253:C8
  "\u0399"  // GREEK CAPITAL LETTER IOTA  # windows-1253:C9
  "\u039a"  // GREEK CAPITAL LETTER KAPPA  # windows-1253:CA
  "\u039b"  // GREEK CAPITAL LETTER LAMDA  # windows-1253:CB
  "\u039c"  // GREEK CAPITAL LETTER MU  # windows-1253:CC
  "\u039d"  // GREEK CAPITAL LETTER NU  # windows-1253:CD
  "\u039e"  // GREEK CAPITAL LETTER XI  # windows-1253:CE
  "\u039f"  // GREEK CAPITAL LETTER OMICRON  # windows-1253:CF
  "\u03a0"  // GREEK CAPITAL LETTER PI  # windows-1253:D0
  "\u03a1"  // GREEK CAPITAL LETTER RHO  # windows-1253:D1
  "\u03a3"  // GREEK CAPITAL LETTER SIGMA  # windows-1253:D3
  "\u03a4"  // GREEK CAPITAL LETTER TAU  # windows-1253:D4
  "\u03a5"  // GREEK CAPITAL LETTER UPSILON  # windows-1253:D5
  "\u03a6"  // GREEK CAPITAL LETTER PHI  # windows-1253:D6
  "\u03a7"  // GREEK CAPITAL LETTER CHI  # windows-1253:D7
  "\u03a8"  // GREEK CAPITAL LETTER PSI  # windows-1253:D8
  "\u03a9"  // GREEK CAPITAL LETTER OMEGA  # windows-1253:D9
  "\u03aa"  // GREEK CAPITAL LETTER IOTA WITH DIALYTIKA  # windows-1253:DA
  "\u03ab"  // GREEK CAPITAL LETTER UPSILON WITH DIALYTIKA  # windows-1253:DB
  "\u03ac"  // GREEK SMALL LETTER ALPHA WITH TONOS  # windows-1253:DC
  "\u03ad"  // GREEK SMALL LETTER EPSILON WITH TONOS  # windows-1253:DD
  "\u03ae"  // GREEK SMALL LETTER ETA WITH TONOS  # windows-1253:DE
  "\u03af"  // GREEK SMALL LETTER IOTA WITH TONOS  # windows-1253:DF
  "\u0412"  // CYRILLIC CAPITAL LETTER VE  # windows-1251:C2
  "\u0413"  // CYRILLIC CAPITAL LETTER GHE  # windows-1251:C3
  "\u0414"  // CYRILLIC CAPITAL LETTER DE  # windows-1251:C4
  "\u0415"  // CYRILLIC CAPITAL LETTER IE  # windows-1251:C5
  "\u0416"  // CYRILLIC CAPITAL LETTER ZHE  # windows-1251:C6
  "\u0417"  // CYRILLIC CAPITAL LETTER ZE  # windows-1251:C7
  "\u0418"  // CYRILLIC CAPITAL LETTER I  # windows-1251:C8
  "\u0419"  // CYRILLIC CAPITAL LETTER SHORT I  # windows-1251:C9
  "\u041a"  // CYRILLIC CAPITAL LETTER KA  # windows-1251:CA
  "\u041b"  // CYRILLIC CAPITAL LETTER EL  # windows-1251:CB
  "\u041c"  // CYRILLIC CAPITAL LETTER EM  # windows-1251:CC
  "\u041d"  // CYRILLIC CAPITAL LETTER EN  # windows-1251:CD
  "\u041e"  // CYRILLIC CAPITAL LETTER O  # windows-1251:CE
  "\u041f"  // CYRILLIC CAPITAL LETTER PE  # windows-1251:CF
  "\u0420"  // CYRILLIC CAPITAL LETTER ER  # windows-1251:D0
  "\u0421"  // CYRILLIC CAPITAL LETTER ES  # windows-1251:D1
  "\u0422"  // CYRILLIC CAPITAL LETTER TE  # windows-1251:D2
  "\u0423"  // CYRILLIC CAPITAL LETTER U  # windows-1251:D3
  "\u0424"  // CYRILLIC CAPITAL LETTER EF  # windows-1251:D4
  "\u0425"  // CYRILLIC CAPITAL LETTER HA  # windows-1251:D5
  "\u0426"  // CYRILLIC CAPITAL LETTER TSE  # windows-1251:D6
  "\u0427"  // CYRILLIC CAPITAL LETTER CHE  # windows-1251:D7
  "\u0428"  // CYRILLIC CAPITAL LETTER SHA  # windows-1251:D8
  "\u0429"  // CYRILLIC CAPITAL LETTER SHCHA  # windows-1251:D9
  "\u042a"  // CYRILLIC CAPITAL LETTER HARD SIGN  # windows-1251:DA
  "\u042b"  // CYRILLIC CAPITAL LETTER YERU  # windows-1251:DB
  "\u042c"  // CYRILLIC CAPITAL LETTER SOFT SIGN  # windows-1251:DC
  "\u042d"  // CYRILLIC CAPITAL LETTER E  # windows-1251:DD
  "\u042e"  // CYRILLIC CAPITAL LETTER YU  # windows-1251:DE
  "\u042f"  // CYRILLIC CAPITAL LETTER YA  # windows-1251:DF
};

// Letters that decode to 0xE0 - 0xEF in a Latin-1-like encoding
static constexpr std::string_view UTF8_FIRST_OF_3 = {
  "\u00e1"  // LATIN SMALL LETTER A WITH ACUTE  # latin-1:E1
  "\u0103"  // LATIN SMALL LETTER A WITH BREVE  # windows-1250:E3
  "\u00e2"  // LATIN SMALL LETTER A WITH CIRCUMFLEX  # latin-1:E2
  "\u00e4"  // LATIN SMALL LETTER A WITH DIAERESIS  # latin-1:E4
  "\u00e0"  // LATIN SMALL LETTER A WITH GRAVE  # latin-1:E0
  "\u0101"  // LATIN SMALL LETTER A WITH MACRON  # windows-1257:E2
  "\u0105"  // LATIN SMALL LETTER A WITH OGONEK  # windows-1257:E0
  "\u00e5"  // LATIN SMALL LETTER A WITH RING ABOVE  # latin-1:E5
  "\u00e3"  // LATIN SMALL LETTER A WITH TILDE  # latin-1:E3
  "\u00e6"  // LATIN SMALL LETTER AE  # latin-1:E6
  "\u0107"  // LATIN SMALL LETTER C WITH ACUTE  # windows-1250:E6
  "\u010d"  // LATIN SMALL LETTER C WITH CARON  # windows-1250:E8
  "\u00e7"  // LATIN SMALL LETTER C WITH CEDILLA  # latin-1:E7
  "\u010f"  // LATIN SMALL LETTER D WITH CARON  # windows-1250:EF
  "\u00e9"  // LATIN SMALL LETTER E WITH ACUTE  # latin-1:E9
  "\u011b"  // LATIN SMALL LETTER E WITH CARON  # windows-1250:EC
  "\u00ea"  // LATIN SMALL LETTER E WITH CIRCUMFLEX  # latin-1:EA
  "\u00eb"  // LATIN SMALL LETTER E WITH DIAERESIS  # latin-1:EB
  "\u0117"  // LATIN SMALL LETTER E WITH DOT ABOVE  # windows-1257:EB
  "\u00e8"  // LATIN SMALL LETTER E WITH GRAVE  # latin-1:E8
  "\u0113"  // LATIN SMALL LETTER E WITH MACRON  # windows-1257:E7
  "\u0119"  // LATIN SMALL LETTER E WITH OGONEK  # windows-1250:EA
  "\u0119"  // LATIN SMALL LETTER E WITH OGONEK  # windows-1250:EA
  "\u0123"  // LATIN SMALL LETTER G WITH CEDILLA  # windows-1257:EC
  "\u00ed"  // LATIN SMALL LETTER I WITH ACUTE  # latin-1:ED
  "\u00ee"  // LATIN SMALL LETTER I WITH CIRCUMFLEX  # latin-1:EE
  "\u00ef"  // LATIN SMALL LETTER I WITH DIAERESIS  # latin-1:EF
  "\u00ec"  // LATIN SMALL LETTER I WITH GRAVE  # latin-1:EC
  "\u012b"  // LATIN SMALL LETTER I WITH MACRON  # windows-1257:EE
  "\u012f"  // LATIN SMALL LETTER I WITH OGONEK  # windows-1257:E1
  "\u0137"  // LATIN SMALL LETTER K WITH CEDILLA  # windows-1257:ED
  "\u013a"  // LATIN SMALL LETTER L WITH ACUTE  # windows-1250:E5
  "\u013c"  // LATIN SMALL LETTER L WITH CEDILLA  # windows-1257:EF
  "\u0155"  // LATIN SMALL LETTER R WITH ACUTE  # windows-1250:E0
  "\u017a"  // LATIN SMALL LETTER Z WITH ACUTE  # windows-1257:EA
  "\u03b0"  // GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND TONOS  # windows-1253:E0
  "\u03b1"  // GREEK SMALL LETTER ALPHA  # windows-1253:E1
  "\u03b2"  // GREEK SMALL LETTER BETA  # windows-1253:E2
  "\u03b3"  // GREEK SMALL LETTER GAMMA  # windows-1253:E3
  "\u03b4"  // GREEK SMALL LETTER DELTA  # windows-1253:E4
  "\u03b5"  // GREEK SMALL LETTER EPSILON  # windows-1253:E5
  "\u03b6"  // GREEK SMALL LETTER ZETA  # windows-1253:E6
  "\u03b7"  // GREEK SMALL LETTER ETA  # windows-1253:E7
  "\u03b8"  // GREEK SMALL LETTER THETA  # windows-1253:E8
  "\u03b9"  // GREEK SMALL LETTER IOTA  # windows-1253:E9
  "\u03ba"  // GREEK SMALL LETTER KAPPA  # windows-1253:EA
  "\u03bb"  // GREEK SMALL LETTER LAMDA  # windows-1253:EB
  "\u03bc"  // GREEK SMALL LETTER MU  # windows-1253:EC
  "\u03bd"  // GREEK SMALL LETTER NU  # windows-1253:ED
  "\u03be"  // GREEK SMALL LETTER XI  # windows-1253:EE
  "\u03bf"  // GREEK SMALL LETTER OMICRON  # windows-1253:EF
  "\u0430"  // CYRILLIC SMALL LETTER A  # windows-1251:E0
  "\u0431"  // CYRILLIC SMALL LETTER BE  # windows-1251:E1
  "\u0432"  // CYRILLIC SMALL LETTER VE  # windows-1251:E2
  "\u0433"  // CYRILLIC SMALL LETTER GHE  # windows-1251:E3
  "\u0434"  // CYRILLIC SMALL LETTER DE  # windows-1251:E4
  "\u0435"  // CYRILLIC SMALL LETTER IE  # windows-1251:E5
  "\u0436"  // CYRILLIC SMALL LETTER ZHE  # windows-1251:E6
  "\u0437"  // CYRILLIC SMALL LETTER ZE  # windows-1251:E7
  "\u0438"  // CYRILLIC SMALL LETTER I  # windows-1251:E8
  "\u0439"  // CYRILLIC SMALL LETTER SHORT I  # windows-1251:E9
  "\u043a"  // CYRILLIC SMALL LETTER KA  # windows-1251:EA
  "\u043b"  // CYRILLIC SMALL LETTER EL  # windows-1251:EB
  "\u043c"  // CYRILLIC SMALL LETTER EM  # windows-1251:EC
  "\u043d"  // CYRILLIC SMALL LETTER EN  # windows-1251:ED
  "\u043e"  // CYRILLIC SMALL LETTER O  # windows-1251:EE
  "\u043f"  // CYRILLIC SMALL LETTER PE  # windows-1251:EF
};

// Letters that decode to 0xF0 or 0xF3 in a Latin-1-like encoding.
// (Other leading bytes correspond only to unassigned codepoints)
static constexpr std::string_view UTF8_FIRST_OF_4 = {
  "\u0111"  // LATIN SMALL LETTER D WITH STROKE  # windows-1250:F0
  "\u00f0"  // LATIN SMALL LETTER ETH  # latin-1:F0
  "\u011f"  // LATIN SMALL LETTER G WITH BREVE  # windows-1254:F0
  "\u00f3"  // LATIN SMALL LETTER O WITH ACUTE  # latin-1:F3
  "\u0161"  // LATIN SMALL LETTER S WITH CARON  # windows-1257:F0
  "\u03c0"  // GREEK SMALL LETTER PI  # windows-1253:F0
  "\u03c3"  // GREEK SMALL LETTER SIGMA  # windows-1253:F3
  "\u0440"  // CYRILLIC SMALL LETTER ER  # windows-1251:F0
  "\u0443"  // CYRILLIC SMALL LETTER U  # windows-1251:F3
};

// Letters that decode to 0x80 - 0xBF in a Latin-1-like encoding,
// including a space standing in for 0xA0
static constexpr std::string_view UTF8_CONTINUATION = {
  // Port note: Original code had \x80-\xbf here, but since this
  // is not a binary string, that means U+0080 to U+00bf. This
  // is confusing but I think probably intentional, since we
  // are looking for bad decoding. -tom7
  "\u0080-\u00bf"
  "\u0020"  // SPACE  # modification of latin-1:A0, NO-BREAK SPACE
  "\u0104"  // LATIN CAPITAL LETTER A WITH OGONEK  # windows-1250:A5
  "\u00c6"  // LATIN CAPITAL LETTER AE  # windows-1257:AF
  "\u013d"  // LATIN CAPITAL LETTER L WITH CARON  # windows-1250:BC
  "\u0141"  // LATIN CAPITAL LETTER L WITH STROKE  # windows-1250:A3
  "\u00d8"  // LATIN CAPITAL LETTER O WITH STROKE  # windows-1257:A8
  "\u0156"  // LATIN CAPITAL LETTER R WITH CEDILLA  # windows-1257:AA
  "\u015a"  // LATIN CAPITAL LETTER S WITH ACUTE  # windows-1250:8C
  "\u0160"  // LATIN CAPITAL LETTER S WITH CARON  # windows-1252:8A
  "\u015e"  // LATIN CAPITAL LETTER S WITH CEDILLA  # windows-1250:AA
  "\u0164"  // LATIN CAPITAL LETTER T WITH CARON  # windows-1250:8D
  "\u0178"  // LATIN CAPITAL LETTER Y WITH DIAERESIS  # windows-1252:9F
  "\u0179"  // LATIN CAPITAL LETTER Z WITH ACUTE  # windows-1250:8F
  "\u017d"  // LATIN CAPITAL LETTER Z WITH CARON  # windows-1252:8E
  "\u017b"  // LATIN CAPITAL LETTER Z WITH DOT ABOVE  # windows-1250:AF
  "\u0152"  // LATIN CAPITAL LIGATURE OE  # windows-1252:8C
  "\u0105"  // LATIN SMALL LETTER A WITH OGONEK  # windows-1250:B9
  "\u00e6"  // LATIN SMALL LETTER AE  # windows-1257:BF
  "\u0192"  // LATIN SMALL LETTER F WITH HOOK  # windows-1252:83
  "\u013e"  // LATIN SMALL LETTER L WITH CARON  # windows-1250:BE
  "\u0142"  // LATIN SMALL LETTER L WITH STROKE  # windows-1250:B3
  "\u00f8"  // LATIN SMALL LETTER O WITH STROKE  # windows-1257:B8
  "\u0157"  // LATIN SMALL LETTER R WITH CEDILLA  # windows-1257:BA
  "\u015b"  // LATIN SMALL LETTER S WITH ACUTE  # windows-1250:9C
  "\u0161"  // LATIN SMALL LETTER S WITH CARON  # windows-1252:9A
  "\u015f"  // LATIN SMALL LETTER S WITH CEDILLA  # windows-1250:BA
  "\u0165"  // LATIN SMALL LETTER T WITH CARON  # windows-1250:9D
  "\u017a"  // LATIN SMALL LETTER Z WITH ACUTE  # windows-1250:9F
  "\u017e"  // LATIN SMALL LETTER Z WITH CARON  # windows-1252:9E
  "\u017c"  // LATIN SMALL LETTER Z WITH DOT ABOVE  # windows-1250:BF
  "\u0153"  // LATIN SMALL LIGATURE OE  # windows-1252:9C
  "\u02c6"  // MODIFIER LETTER CIRCUMFLEX ACCENT  # windows-1252:88
  "\u02c7"  // CARON  # windows-1250:A1
  "\u02d8"  // BREVE  # windows-1250:A2
  "\u02db"  // OGONEK  # windows-1250:B2
  "\u02dc"  // SMALL TILDE  # windows-1252:98
  "\u02dd"  // DOUBLE ACUTE ACCENT  # windows-1250:BD
  "\u0384"  // GREEK TONOS  # windows-1253:B4
  "\u0385"  // GREEK DIALYTIKA TONOS  # windows-1253:A1
  "\u0386"  // GREEK CAPITAL LETTER ALPHA WITH TONOS  # windows-1253:A2
  "\u0388"  // GREEK CAPITAL LETTER EPSILON WITH TONOS  # windows-1253:B8
  "\u0389"  // GREEK CAPITAL LETTER ETA WITH TONOS  # windows-1253:B9
  "\u038a"  // GREEK CAPITAL LETTER IOTA WITH TONOS  # windows-1253:BA
  "\u038c"  // GREEK CAPITAL LETTER OMICRON WITH TONOS  # windows-1253:BC
  "\u038e"  // GREEK CAPITAL LETTER UPSILON WITH TONOS  # windows-1253:BE
  "\u038f"  // GREEK CAPITAL LETTER OMEGA WITH TONOS  # windows-1253:BF
  "\u0401"  // CYRILLIC CAPITAL LETTER IO  # windows-1251:A8
  "\u0402"  // CYRILLIC CAPITAL LETTER DJE  # windows-1251:80
  "\u0403"  // CYRILLIC CAPITAL LETTER GJE  # windows-1251:81
  "\u0404"  // CYRILLIC CAPITAL LETTER UKRAINIAN IE  # windows-1251:AA
  "\u0405"  // CYRILLIC CAPITAL LETTER DZE  # windows-1251:BD
  "\u0406"  // CYRILLIC CAPITAL LETTER BYELORUSSIAN-UKRAINIAN I  # windows-1251:B2
  "\u0407"  // CYRILLIC CAPITAL LETTER YI  # windows-1251:AF
  "\u0408"  // CYRILLIC CAPITAL LETTER JE  # windows-1251:A3
  "\u0409"  // CYRILLIC CAPITAL LETTER LJE  # windows-1251:8A
  "\u040a"  // CYRILLIC CAPITAL LETTER NJE  # windows-1251:8C
  "\u040b"  // CYRILLIC CAPITAL LETTER TSHE  # windows-1251:8E
  "\u040c"  // CYRILLIC CAPITAL LETTER KJE  # windows-1251:8D
  "\u040e"  // CYRILLIC CAPITAL LETTER SHORT U  # windows-1251:A1
  "\u040f"  // CYRILLIC CAPITAL LETTER DZHE  # windows-1251:8F
  "\u0451"  // CYRILLIC SMALL LETTER IO  # windows-1251:B8
  "\u0452"  // CYRILLIC SMALL LETTER DJE  # windows-1251:90
  "\u0453"  // CYRILLIC SMALL LETTER GJE  # windows-1251:83
  "\u0454"  // CYRILLIC SMALL LETTER UKRAINIAN IE  # windows-1251:BA
  "\u0455"  // CYRILLIC SMALL LETTER DZE  # windows-1251:BE
  "\u0456"  // CYRILLIC SMALL LETTER BYELORUSSIAN-UKRAINIAN I  # windows-1251:B3
  "\u0457"  // CYRILLIC SMALL LETTER YI  # windows-1251:BF
  "\u0458"  // CYRILLIC SMALL LETTER JE  # windows-1251:BC
  "\u0459"  // CYRILLIC SMALL LETTER LJE  # windows-1251:9A
  "\u045a"  // CYRILLIC SMALL LETTER NJE  # windows-1251:9C
  "\u045b"  // CYRILLIC SMALL LETTER TSHE  # windows-1251:9E
  "\u045c"  // CYRILLIC SMALL LETTER KJE  # windows-1251:9D
  "\u045e"  // CYRILLIC SMALL LETTER SHORT U  # windows-1251:A2
  "\u045f"  // CYRILLIC SMALL LETTER DZHE  # windows-1251:9F
  "\u0490"  // CYRILLIC CAPITAL LETTER GHE WITH UPTURN  # windows-1251:A5
  "\u0491"  // CYRILLIC SMALL LETTER GHE WITH UPTURN  # windows-1251:B4
  "\u2013"  // EN DASH  # windows-1252:96
  "\u2014"  // EM DASH  # windows-1252:97
  "\u2015"  // HORIZONTAL BAR  # windows-1253:AF
  "\u2018"  // LEFT SINGLE QUOTATION MARK  # windows-1252:91
  "\u2019"  // RIGHT SINGLE QUOTATION MARK  # windows-1252:92
  "\u201a"  // SINGLE LOW-9 QUOTATION MARK  # windows-1252:82
  "\u201c"  // LEFT DOUBLE QUOTATION MARK  # windows-1252:93
  "\u201d"  // RIGHT DOUBLE QUOTATION MARK  # windows-1252:94
  "\u201e"  // DOUBLE LOW-9 QUOTATION MARK  # windows-1252:84
  "\u2020"  // DAGGER  # windows-1252:86
  "\u2021"  // DOUBLE DAGGER  # windows-1252:87
  "\u2022"  // BULLET  # windows-1252:95
  "\u2026"  // HORIZONTAL ELLIPSIS  # windows-1252:85
  "\u2030"  // PER MILLE SIGN  # windows-1252:89
  "\u2039"  // SINGLE LEFT-POINTING ANGLE QUOTATION MARK  # windows-1252:8B
  "\u203a"  // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK  # windows-1252:9B
  "\u20ac"  // EURO SIGN  # windows-1252:80
  "\u2116"  // NUMERO SIGN  # windows-1251:B9
  "\u2122" // TRADE MARK SIGN  # windows-1252:99
};

// Letters that decode to 0x80 - 0xBF in a Latin-1-like encoding,
// and don't usually stand for themselves when adjacent to mojibake.
// This excludes spaces, dashes, 'bullet', quotation marks, and ellipses.
static constexpr std::string_view UTF8_CONTINUATION_STRICT = {
  // Port note: Original code had \x80-\xbf here; see above.
  "\u0080-\u00bf"
  "\u0104"  // LATIN CAPITAL LETTER A WITH OGONEK  # windows-1250:A5
  "\u00c6"  // LATIN CAPITAL LETTER AE  # windows-1257:AF
  "\u013d"  // LATIN CAPITAL LETTER L WITH CARON  # windows-1250:BC
  "\u0141"  // LATIN CAPITAL LETTER L WITH STROKE  # windows-1250:A3
  "\u00d8"  // LATIN CAPITAL LETTER O WITH STROKE  # windows-1257:A8
  "\u0156"  // LATIN CAPITAL LETTER R WITH CEDILLA  # windows-1257:AA
  "\u015a"  // LATIN CAPITAL LETTER S WITH ACUTE  # windows-1250:8C
  "\u0160"  // LATIN CAPITAL LETTER S WITH CARON  # windows-1252:8A
  "\u015e"  // LATIN CAPITAL LETTER S WITH CEDILLA  # windows-1250:AA
  "\u0164"  // LATIN CAPITAL LETTER T WITH CARON  # windows-1250:8D
  "\u0178"  // LATIN CAPITAL LETTER Y WITH DIAERESIS  # windows-1252:9F
  "\u0179"  // LATIN CAPITAL LETTER Z WITH ACUTE  # windows-1250:8F
  "\u017d"  // LATIN CAPITAL LETTER Z WITH CARON  # windows-1252:8E
  "\u017b"  // LATIN CAPITAL LETTER Z WITH DOT ABOVE  # windows-1250:AF
  "\u0152"  // LATIN CAPITAL LIGATURE OE  # windows-1252:8C
  "\u0105"  // LATIN SMALL LETTER A WITH OGONEK  # windows-1250:B9
  "\u00e6"  // LATIN SMALL LETTER AE  # windows-1257:BF
  "\u0192"  // LATIN SMALL LETTER F WITH HOOK  # windows-1252:83
  "\u013e"  // LATIN SMALL LETTER L WITH CARON  # windows-1250:BE
  "\u0142"  // LATIN SMALL LETTER L WITH STROKE  # windows-1250:B3
  "\u00f8"  // LATIN SMALL LETTER O WITH STROKE  # windows-1257:B8
  "\u0157"  // LATIN SMALL LETTER R WITH CEDILLA  # windows-1257:BA
  "\u015b"  // LATIN SMALL LETTER S WITH ACUTE  # windows-1250:9C
  "\u0161"  // LATIN SMALL LETTER S WITH CARON  # windows-1252:9A
  "\u015f"  // LATIN SMALL LETTER S WITH CEDILLA  # windows-1250:BA
  "\u0165"  // LATIN SMALL LETTER T WITH CARON  # windows-1250:9D
  "\u017a"  // LATIN SMALL LETTER Z WITH ACUTE  # windows-1250:9F
  "\u017e"  // LATIN SMALL LETTER Z WITH CARON  # windows-1252:9E
  "\u017c"  // LATIN SMALL LETTER Z WITH DOT ABOVE  # windows-1250:BF
  "\u0153"  // LATIN SMALL LIGATURE OE  # windows-1252:9C
  "\u02c6"  // MODIFIER LETTER CIRCUMFLEX ACCENT  # windows-1252:88
  "\u02c7"  // CARON  # windows-1250:A1
  "\u02d8"  // BREVE  # windows-1250:A2
  "\u02db"  // OGONEK  # windows-1250:B2
  "\u02dc"  // SMALL TILDE  # windows-1252:98
  "\u02dd"  // DOUBLE ACUTE ACCENT  # windows-1250:BD
  "\u0384"  // GREEK TONOS  # windows-1253:B4
  "\u0385"  // GREEK DIALYTIKA TONOS  # windows-1253:A1
  "\u0386"  // GREEK CAPITAL LETTER ALPHA WITH TONOS  # windows-1253:A2
  "\u0388"  // GREEK CAPITAL LETTER EPSILON WITH TONOS  # windows-1253:B8
  "\u0389"  // GREEK CAPITAL LETTER ETA WITH TONOS  # windows-1253:B9
  "\u038a"  // GREEK CAPITAL LETTER IOTA WITH TONOS  # windows-1253:BA
  "\u038c"  // GREEK CAPITAL LETTER OMICRON WITH TONOS  # windows-1253:BC
  "\u038e"  // GREEK CAPITAL LETTER UPSILON WITH TONOS  # windows-1253:BE
  "\u038f"  // GREEK CAPITAL LETTER OMEGA WITH TONOS  # windows-1253:BF
  "\u0401"  // CYRILLIC CAPITAL LETTER IO  # windows-1251:A8
  "\u0402"  // CYRILLIC CAPITAL LETTER DJE  # windows-1251:80
  "\u0403"  // CYRILLIC CAPITAL LETTER GJE  # windows-1251:81
  "\u0404"  // CYRILLIC CAPITAL LETTER UKRAINIAN IE  # windows-1251:AA
  "\u0405"  // CYRILLIC CAPITAL LETTER DZE  # windows-1251:BD
  "\u0406"  // CYRILLIC CAPITAL LETTER BYELORUSSIAN-UKRAINIAN I  # windows-1251:B2
  "\u0407"  // CYRILLIC CAPITAL LETTER YI  # windows-1251:AF
  "\u0408"  // CYRILLIC CAPITAL LETTER JE  # windows-1251:A3
  "\u0409"  // CYRILLIC CAPITAL LETTER LJE  # windows-1251:8A
  "\u040a"  // CYRILLIC CAPITAL LETTER NJE  # windows-1251:8C
  "\u040b"  // CYRILLIC CAPITAL LETTER TSHE  # windows-1251:8E
  "\u040c"  // CYRILLIC CAPITAL LETTER KJE  # windows-1251:8D
  "\u040e"  // CYRILLIC CAPITAL LETTER SHORT U  # windows-1251:A1
  "\u040f"  // CYRILLIC CAPITAL LETTER DZHE  # windows-1251:8F
  "\u0451"  // CYRILLIC SMALL LETTER IO  # windows-1251:B8
  "\u0452"  // CYRILLIC SMALL LETTER DJE  # windows-1251:90
  "\u0453"  // CYRILLIC SMALL LETTER GJE  # windows-1251:83
  "\u0454"  // CYRILLIC SMALL LETTER UKRAINIAN IE  # windows-1251:BA
  "\u0455"  // CYRILLIC SMALL LETTER DZE  # windows-1251:BE
  "\u0456"  // CYRILLIC SMALL LETTER BYELORUSSIAN-UKRAINIAN I  # windows-1251:B3
  "\u0457"  // CYRILLIC SMALL LETTER YI  # windows-1251:BF
  "\u0458"  // CYRILLIC SMALL LETTER JE  # windows-1251:BC
  "\u0459"  // CYRILLIC SMALL LETTER LJE  # windows-1251:9A
  "\u045a"  // CYRILLIC SMALL LETTER NJE  # windows-1251:9C
  "\u045b"  // CYRILLIC SMALL LETTER TSHE  # windows-1251:9E
  "\u045c"  // CYRILLIC SMALL LETTER KJE  # windows-1251:9D
  "\u045e"  // CYRILLIC SMALL LETTER SHORT U  # windows-1251:A2
  "\u045f"  // CYRILLIC SMALL LETTER DZHE  # windows-1251:9F
  "\u0490"  // CYRILLIC CAPITAL LETTER GHE WITH UPTURN  # windows-1251:A5
  "\u0491"  // CYRILLIC SMALL LETTER GHE WITH UPTURN  # windows-1251:B4
  "\u2020"  // DAGGER  # windows-1252:86
  "\u2021"  // DOUBLE DAGGER  # windows-1252:87
  "\u2030"  // PER MILLE SIGN  # windows-1252:89
  "\u2039"  // SINGLE LEFT-POINTING ANGLE QUOTATION MARK  # windows-1252:8B
  "\u203a"  // SINGLE RIGHT-POINTING ANGLE QUOTATION MARK  # windows-1252:9B
  "\u20ac"  // EURO SIGN  # windows-1252:80
  "\u2116"  // NUMERO SIGN  # windows-1251:B9
  "\u2122"  // TRADE MARK SIGN  # windows-1252:99
};

template<class ...Ts>
static constexpr std::string StrCat(Ts &&...ts) {
  std::string out;
  ( (out.append(std::format("{}", std::forward<Ts>(ts)))), ... );
  return out;
}


std::string FixEncoding::DecodeInconsistentUTF8(std::string_view text) {
  // This regex uses UTF8_CLUES to find sequences of likely mojibake.
  // It matches them with + so that several adjacent UTF-8-looking
  // sequences get coalesced into one, allowing them to be fixed more
  // efficiently and not requiring every individual subsequence to be
  // detected as 'badness'.
  //
  // We accept spaces in place of "utf8_continuation", because spaces
  // might have been intended to be U+A0 NO-BREAK SPACE.

  // Port note: This originally used "negative lookbehind", which is
  // not supported in RE2. Instead I match a character or the
  // beginning of the string, which needs to then be replaced. This
  // works OK because for one thing, the second group allows repeated
  // matches.
  //
  // Unfortunately, the matches to this regular expression won't show
  // their surrounding context, and including context would make the
  // expression much less efficient. The 'badness' rules that require
  // context, such as a preceding lowercase letter, will prevent some
  // cases of inconsistent UTF-8 from being fixed when they don't see
  // it.
  static RE2 utf8_detector_re = {
    StrCat(
        // the negative lookbehind, but capture this so we can replace it,
        "([^", UTF8_CONTINUATION_STRICT, "]|^)",
        // The actual mojibake sequence.
        "("
        // non-capturing group for the +
        "(?:[", UTF8_FIRST_OF_2, "][", UTF8_CONTINUATION, "]|"
        "[", UTF8_FIRST_OF_3, "][", UTF8_CONTINUATION, "]{2}|"
        "[", UTF8_FIRST_OF_4, "][", UTF8_CONTINUATION, "]{3})+"
        ")")
  };

  // Sometimes, text from one encoding ends up embedded within text from a
  // different one. This is common enough that we need to be able to fix it.

  auto FixEmbeddedMojibake = [text](std::span<const std::string_view> match) {
      // group 0 is whole match
      // 1 is the "negative lookbehind" simulator
      // 2 is the target, which ftfy calls 'substr'
      std::string_view substr = match[2];
      // Require the match to be shorter, so that this doesn't recurse infinitely
      if (substr.size() < text.size()) {
        return std::format("{}{}", match[1], FixEncoding::Fix(substr));
      } else {
        return std::string(match[0]);
      }
    };

  return RE2Util::MapReplacement(text, utf8_detector_re, FixEmbeddedMojibake);
}

// Strip out "ANSI" terminal escape sequences, such as those that produce
// colored text on Unix.
std::string FixEncoding::RemoveTerminalEscapes(std::string_view text) {
  static LazyRE2 ANSI_RE = { "\033\\[((?:\\d|;)*)([a-zA-Z])" };
  std::string ret(text);
  RE2::GlobalReplace(&ret, *ANSI_RE, "");
  return ret;
}


// Replace curly; quotation marks with straight equivalents.
std::string FixEncoding::UncurlQuotes(std::string_view text) {
  static LazyRE2 SINGLE_QUOTE_RE = { "[\u02bc\u2018-\u201b]" };
  static LazyRE2 DOUBLE_QUOTE_RE = { "[\u201c-\u201f]" };
  std::string ret(text);
  RE2::GlobalReplace(&ret, *SINGLE_QUOTE_RE, "'");
  RE2::GlobalReplace(&ret, *DOUBLE_QUOTE_RE, "\"");
  return ret;
}

// Replace single-character ligatures of Latin letters, such as 'ﬁ',
// with the characters that they contain, as in 'fi'. Latin ligatures
// are usually not intended in text strings (though they're lovely in
// *rendered* text). If you have such a ligature in your string, it is
// probably a result of a copy-and-paste glitch.
//
// We leave ligatures in other scripts alone to be safe. They may be
// intended, and removing them may lose information. If you want to
// take apart nearly all ligatures, use NFKC normalization.
std::string FixEncoding::FixLatinLigatures(std::string_view text) {
  // A mapping that breaks ligatures made of Latin letters. While
  // ligatures may be important to the representation of other
  // languages, in Latin letters they tend to represent a copy/paste
  // error. It omits ligatures such as æ that are frequently used
  // intentionally.
  //
  // This list additionally includes some Latin digraphs that
  // represent two characters for legacy encoding reasons, not for
  // typographical reasons.
  //
  // Ligatures and digraphs may also be separated by NFKC
  // normalization, but that is sometimes more normalization than you
  // want.
  static std::unordered_map<uint32_t, std::string_view> LIGATURES = [](){
      return std::unordered_map<uint32_t, std::string_view> {
        { UTF8::Codepoint("Ĳ"), "IJ" },
        { UTF8::Codepoint("ĳ"), "ij" },
        { UTF8::Codepoint("ŉ"), "ʼn" },
        { UTF8::Codepoint("Ǳ"), "DZ" },
        { UTF8::Codepoint("ǲ"), "Dz" },
        { UTF8::Codepoint("ǳ"), "dz" },
        { UTF8::Codepoint("Ǆ"), "DŽ" },
        { UTF8::Codepoint("ǅ"), "Dž" },
        { UTF8::Codepoint("ǆ"), "dž" },
        { UTF8::Codepoint("Ǉ"), "LJ" },
        { UTF8::Codepoint("ǈ"), "Lj" },
        { UTF8::Codepoint("ǉ"), "lj" },
        { UTF8::Codepoint("Ǌ"), "NJ" },
        { UTF8::Codepoint("ǋ"), "Nj" },
        { UTF8::Codepoint("ǌ"), "nj" },
        { UTF8::Codepoint("ﬀ"), "ff" },
        { UTF8::Codepoint("ﬁ"), "fi" },
        { UTF8::Codepoint("ﬂ"), "fl" },
        { UTF8::Codepoint("ﬃ"), "ffi" },
        { UTF8::Codepoint("ﬄ"), "ffl" },
        { UTF8::Codepoint("ﬅ"), "ſt" },
        { UTF8::Codepoint("ﬆ"), "st" },
      };
    }();

  std::string ret;
  ret.reserve(text.size());

  for (uint32_t codepoint : UTF8::Decoder(text)) {
    auto it = LIGATURES.find(codepoint);
    if (it == LIGATURES.end()) {
      ret.append(UTF8::Encode(codepoint));
    } else {
      ret.append(it->second);
    }
  }

  return ret;
}

// Convert all line breaks to Unix style.
//
// This will convert the following sequences into the standard \\n
// line break:
//
// - CRLF (\\r\\n), used on Windows and in some communication protocols
// - CR (\\r), once used on Mac OS Classic, and now kept alive by misguided
//   software such as Microsoft Office for Mac
// - LINE SEPARATOR (\\u2028) and PARAGRAPH SEPARATOR (\\u2029), defined by
//   Unicode and used to sow confusion and discord
// - NEXT LINE (\\x85), a C1 control character that is certainly not what you
//   meant
//
// The NEXT LINE character is a bit of an odd case, because it
// usually won't show up if `fix_encoding` is also being run.
// \\x85 is very common mojibake for \\u2026, HORIZONTAL ELLIPSIS.
std::string FixEncoding::FixLineBreaks(std::string_view text) {
  static LazyRE2 CRLF_RE = { "\r\n" };
  static LazyRE2 WEIRD_LINE_END_RE = { "[\r\u2028\u2029\u0085]" };

  std::string s(text);
  // First replace \r\n so that we don't get double newlines.
  RE2::GlobalReplace(&s, *CRLF_RE, "\n");
  RE2::GlobalReplace(&s, *WEIRD_LINE_END_RE, "\n");
  return s;
}

std::string FixEncoding::FixSurrogates(std::string_view text) {
  // Port note: ftfy uses literals like \ud800 here, but those are not
  // valid entities in C++.

  std::string out;
  out.reserve(text.size());

  auto dec = UTF8::Decoder(text);
  for (auto it = dec.begin(); it != dec.end(); ++it) {
    uint32_t cp = *it;

    if (cp >= 0xd800 && cp <= 0xdfff) {
      // Valid?
      auto next = it;
      ++next;
      if (cp >= 0xd800 && cp <= 0xdbff &&
          next != dec.end() &&
          *next >= 0xdc00 && *next <= 0xdfff) {
        uint32_t full = 0x10000 + (cp - 0xD800) * 0x400 + (*next - 0xDC00);
        out.append(UTF8::Encode(full));
        // Consume the second.
        ++it;
      } else {
        // Just replace the first, following ftfy.
        // If we have hi/lo/hi, then the second pair is
        // decoded.
        out.append(UTF8::Encode(UTF8::REPLACEMENT_CODEPOINT));
      }
    } else {
      out.append(UTF8::Encode(cp));
    }
  }

  return out;
}

// Remove a byte-order mark that was accidentally decoded as if it were part
// of the text.
std::string RemoveBOM(std::string_view text) {
  while (!text.empty()) {
    const auto &[n, cp] = UTF8::ParsePrefix(text.data(), text.size());
    if (cp == 0xFEFF) {
      text.remove_prefix(n);
    } else {
      break;
    }
  }

  return std::string(text);
}

// Port note: No decode_escapes support; it's not run by default in ftfy
// and isn't really in scope IMO.

static std::string ReplaceByte(std::string_view s, uint8_t src, uint8_t dst) {
  std::string out(s);
  for (size_t i = 0; i < out.size(); i++) {
    if (out[i] == src) out[i] = dst;
  }
  return out;
}


// Some mojibake has been additionally altered by a process that said "hmm,
// byte A0, that's basically a space!" and replaced it with an ASCII space.
// When the A0 is part of a sequence that we intend to decode as UTF-8,
// changing byte A0 to 20 would make it fail to decode.
//
// This process finds sequences that would convincingly decode as UTF-8 if
// byte 20 were changed to A0, and puts back the A0. For the purpose of
// deciding whether this is a good idea, this step gets a cost of twice
// the number of bytes that are changed.
std::string FixEncoding::RestoreByteA0(std::string_view text) {
  // Recognize UTF-8 sequences that would be valid if it weren't for a
  // b'\xa0' that some Windows-1252 program converted to a plain
  // space.
  //
  // The smaller values are included on a case-by-case basis, because
  // we don't want to decode likely input sequences to unlikely
  // characters. These are the ones that *do* form likely characters
  // before 0xa0:
  //
  //   0xc2 -> U+A0 NO-BREAK SPACE
  //   0xc3 -> U+E0 LATIN SMALL LETTER A WITH GRAVE
  //   0xc5 -> U+160 LATIN CAPITAL LETTER S WITH CARON
  //   0xce -> U+3A0 GREEK CAPITAL LETTER PI
  //   0xd0 -> U+420 CYRILLIC CAPITAL LETTER ER
  //   0xd9 -> U+660 ARABIC-INDIC DIGIT ZERO
  //
  // In three-character sequences, we exclude some lead bytes in some
  // cases.
  //
  // When the lead byte is immediately followed by 0xA0, we shouldn't
  // accept a space there, because it leads to some less-likely
  // character ranges:
  //
  //   0xe0 -> Samaritan script
  //   0xe1 -> Mongolian script (corresponds to Latin-1 'á' which is too common)
  //
  // We accept 0xe2 and 0xe3, which cover many scripts. Bytes 0xe4 and
  // higher point mostly to CJK characters, which we generally don't
  // want to decode near Latin lowercase letters.
  //
  // In four-character sequences, the lead byte must be F0, because
  // that accounts for almost all of the usage of high-numbered
  // codepoints (tag characters whose UTF-8 starts with the byte F3
  // are only used in some rare new emoji sequences).
  //
  // This is meant to be applied to encodings of text that tests true
  // for `is_bad`. Any of these could represent characters that
  // legitimately appear surrounded by spaces, particularly U+C5 (Å),
  // which is a word in multiple languages!
  //
  // We should consider checking for b'\x85' being converted to ... in
  // the future. I've seen it once, but the text still wasn't
  // recoverable.
  static LazyRE2 ALTERED_UTF8_RE = {
    "[\\xc2\\xc3\\xc5\\xce\\xd0\\xd9][ ]"
    "|[\\xe2\\xe3][ ][\\x80-\\x84\\x86-\\x9f\\xa1-\\xbf]"
    "|[\\xe0-\\xe3][\\x80-\\x84\\x86-\\x9f\\xa1-\\xbf][ ]"
    "|[\\xf0][ ][\\x80-\\xbf][\\x80-\\xbf]"
    "|[\\xf0][\\x80-\\xbf][ ][\\x80-\\xbf]"
    "|[\\xf0][\\x80-\\xbf][\\x80-\\xbf][ ]",
    BINARY_REGEX
  };

  // Port note: ftfy uses negative lookahead, not supported by RE2.
  // Since the match must start with literally "\xc3 " I'm just doing
  // this as a simple find loop for the prefix, and rejecting if it
  // matches this pattern with a (positive)
  static LazyRE2 A_GRAVE_WORD_RE = {
    "^\xc3 (?: |quele|quela|quilo|s )", BINARY_REGEX
  };

  std::string out;
  out.reserve(text.size());

  for (;;) {
    auto pos = text.find("\xc3 ");

    if (pos == std::string_view::npos) {
      out.append(text);
      break;
    }

    // Output everything up to the match, and remove
    // it from the text.
    out.append(text.substr(0, pos));
    text.remove_prefix(pos);

    if (RE2::StartMatch(text, *A_GRAVE_WORD_RE)) {
      // Suppressed. Preserve the original bytes.
      out.append("\xc3 ");
    } else {
      // Here \xc3\xa0 is UTF-8 for à, and we *also* preserve the
      // space (following ftfy), with the idea that it was probably
      // coalesced with the \xa0 (non-breaking space). The
      // A_GRAVE_WORD_RE prevented us from doing this for some
      // common words starting with à.
      out.append("\xc3\xa0 ");
    }

    text.remove_prefix(2);
  }

  auto Replacement = [](std::span<const std::string_view> match) {
      return ReplaceByte(match[0], 0x20, 0xa0);
    };

  return RE2Util::MapReplacement(out, *ALTERED_UTF8_RE, Replacement);
}


// Identifies sequences where information has been lost in
// a "sloppy" codec, indicated by byte 1A, and if they would otherwise look
// like a UTF-8 sequence, it replaces them with the UTF-8 sequence for U+FFFD.
std::string ReplaceLossySequences(std::string_view str) {
  // This expression matches UTF-8 and CESU-8 sequences where some of
  // the continuation bytes have been lost. The byte 0x1a (sometimes
  // written as ^Z) is used within ftfy to represent a byte that
  // produced the replacement character \ufffd. We don't know which
  // byte it was, but we can at least decode the UTF-8 sequence as
  // \ufffd instead of failing to re-decode it at all.
  //
  // In some cases, we allow the ASCII '?' in place of \ufffd, but at
  // most once per sequence.
  static LazyRE2 LOSSY_UTF8_RE = {
    "[\xc2-\xdf][\x1a]"
    "|[\xc2-\xc3][?]"
    "|\xed[\xa0-\xaf][\x1a?]\xed[\xb0-\xbf][\x1a?\x80-\xbf]"
    "|\xed[\xa0-\xaf][\x1a?\x80-\xbf]\xed[\xb0-\xbf][\x1a?]"
    "|[\xe0-\xef][\x1a?][\x1a\x80-\xbf]"
    "|[\xe0-\xef][\x1a\x80-\xbf][\x1a?]"
    "|[\xf0-\xf4][\x1a?][\x1a\x80-\xbf][\x1a\x80-\xbf]"
    "|[\xf0-\xf4][\x1a\x80-\xbf][\x1a?][\x1a\x80-\xbf]"
    "|[\xf0-\xf4][\x1a\x80-\xbf][\x1a\x80-\xbf][\x1a?]"
    "|\x1a",
    BINARY_REGEX
  };

  return RE2Util::GlobalReplace(str, *LOSSY_UTF8_RE,
                                UTF8::Encode(UTF8::REPLACEMENT_CODEPOINT));
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

// from sloppy-codecs and similar:

static constexpr uint32_t UN_DEF = 0xFFFFFFFF;
inline constexpr uint32_t SloppyCodepoint(const std::array<uint32_t, 256> &DATA,
                                          uint8_t byte) {
  const uint32_t codepoint = DATA[byte];
  return (codepoint == UN_DEF) ? (uint32_t)byte : codepoint;
}

constexpr std::array<uint32_t, 256> table_windows_1252 = {
  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
  0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
  0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
  0x20AC, UN_DEF, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, UN_DEF, 0x017D, UN_DEF,
  UN_DEF, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, UN_DEF, 0x017E, 0x0178,
  0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
  0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
  0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
  0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
  0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
  0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
  0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
  0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
  0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
  0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
  0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
  0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

constexpr std::array<uint32_t, 256> table_windows_1251 = {
  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
  0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
  0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
  0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
  0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
  0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  UN_DEF, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
  0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
  0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
  0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
  0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
  0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
  0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
  0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
  0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
  0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
  0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
  0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
  0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

constexpr std::array<uint32_t, 256> table_windows_1250 = {
  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
  0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
  0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
  0x20AC, UN_DEF, 0x201A, UN_DEF, 0x201E, 0x2026, 0x2020, 0x2021,
  UN_DEF, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
  UN_DEF, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  UN_DEF, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
  0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7,
  0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
  0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
  0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
  0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
  0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
  0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
  0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
  0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
  0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
  0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
  0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

constexpr std::array<uint32_t, 256> table_windows_1253 = {
  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
  0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
  0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
  0x20AC, UN_DEF, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
  UN_DEF, 0x2030, UN_DEF, 0x2039, UN_DEF, UN_DEF, UN_DEF, UN_DEF,
  UN_DEF, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  UN_DEF, 0x2122, UN_DEF, 0x203A, UN_DEF, UN_DEF, UN_DEF, UN_DEF,
  0x00A0, 0x0385, 0x0386, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
  0x00A8, 0x00A9, UN_DEF, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x2015,
  0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x0384, 0x00B5, 0x00B6, 0x00B7,
  0x0388, 0x0389, 0x038A, 0x00BB, 0x038C, 0x00BD, 0x038E, 0x038F,
  0x0390, 0x0391, 0x0392, 0x0393, 0x0394, 0x0395, 0x0396, 0x0397,
  0x0398, 0x0399, 0x039A, 0x039B, 0x039C, 0x039D, 0x039E, 0x039F,
  0x03A0, 0x03A1, UN_DEF, 0x03A3, 0x03A4, 0x03A5, 0x03A6, 0x03A7,
  0x03A8, 0x03A9, 0x03AA, 0x03AB, 0x03AC, 0x03AD, 0x03AE, 0x03AF,
  0x03B0, 0x03B1, 0x03B2, 0x03B3, 0x03B4, 0x03B5, 0x03B6, 0x03B7,
  0x03B8, 0x03B9, 0x03BA, 0x03BB, 0x03BC, 0x03BD, 0x03BE, 0x03BF,
  0x03C0, 0x03C1, 0x03C2, 0x03C3, 0x03C4, 0x03C5, 0x03C6, 0x03C7,
  0x03C8, 0x03C9, 0x03CA, 0x03CB, 0x03CC, 0x03CD, 0x03CE, UN_DEF,
};

constexpr std::array<uint32_t, 256> table_windows_1254 = {
  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
  0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
  0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
  0x20AC, UN_DEF, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, UN_DEF, UN_DEF, UN_DEF,
  UN_DEF, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, UN_DEF, UN_DEF, 0x0178,
  0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
  0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
  0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
  0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
  0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
  0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
  0x011E, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
  0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x0130, 0x015E, 0x00DF,
  0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
  0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
  0x011F, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
  0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x0131, 0x015F, 0x00FF,
};

constexpr std::array<uint32_t, 256> table_windows_1257 = {
  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
  0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
  0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
  0x20AC, UN_DEF, 0x201A, UN_DEF, 0x201E, 0x2026, 0x2020, 0x2021,
  UN_DEF, 0x2030, UN_DEF, 0x2039, UN_DEF, 0x00A8, 0x02C7, 0x00B8,
  UN_DEF, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
  UN_DEF, 0x2122, UN_DEF, 0x203A, UN_DEF, 0x00AF, 0x02DB, UN_DEF,
  0x00A0, UN_DEF, 0x00A2, 0x00A3, 0x00A4, UN_DEF, 0x00A6, 0x00A7,
  0x00D8, 0x00A9, 0x0156, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00C6,
  0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
  0x00F8, 0x00B9, 0x0157, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00E6,
  0x0104, 0x012E, 0x0100, 0x0106, 0x00C4, 0x00C5, 0x0118, 0x0112,
  0x010C, 0x00C9, 0x0179, 0x0116, 0x0122, 0x0136, 0x012A, 0x013B,
  0x0160, 0x0143, 0x0145, 0x00D3, 0x014C, 0x00D5, 0x00D6, 0x00D7,
  0x0172, 0x0141, 0x015A, 0x016A, 0x00DC, 0x017B, 0x017D, 0x00DF,
  0x0105, 0x012F, 0x0101, 0x0107, 0x00E4, 0x00E5, 0x0119, 0x0113,
  0x010D, 0x00E9, 0x017A, 0x0117, 0x0123, 0x0137, 0x012B, 0x013C,
  0x0161, 0x0144, 0x0146, 0x00F3, 0x014D, 0x00F5, 0x00F6, 0x00F7,
  0x0173, 0x0142, 0x015B, 0x016B, 0x00FC, 0x017C, 0x017E, 0x02D9,
};

constexpr std::array<uint32_t, 256> table_iso_8859_2 = {
  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
  0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
  0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
  0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
  0x0088, 0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F,
  0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
  0x0098, 0x0099, 0x009A, 0x009B, 0x009C, 0x009D, 0x009E, 0x009F,
  0x00A0, 0x0104, 0x02D8, 0x0141, 0x00A4, 0x013D, 0x015A, 0x00A7,
  0x00A8, 0x0160, 0x015E, 0x0164, 0x0179, 0x00AD, 0x017D, 0x017B,
  0x00B0, 0x0105, 0x02DB, 0x0142, 0x00B4, 0x013E, 0x015B, 0x02C7,
  0x00B8, 0x0161, 0x015F, 0x0165, 0x017A, 0x02DD, 0x017E, 0x017C,
  0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7,
  0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
  0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7,
  0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
  0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7,
  0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
  0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7,
  0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9,
};

constexpr std::array<uint32_t, 256> table_macroman = {
  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
  0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
  0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
  0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
  0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
  0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
  0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
  0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
  0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
  0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
  0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
  0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
  0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
  0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
  0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
  0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
  0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
  0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
  0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

constexpr std::array<uint32_t, 256> table_cp437 = {
  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
  0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
  0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F,
  0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
  0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
  0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
  0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
  0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
  0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
  0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
  0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
  0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
  0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
  0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
  0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
  0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
  0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
  0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
  0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

// We need to be able to invert the tables above to encode.
// The table is stored as an array of bytes which are sorted by
// decode_table[b].
template<const std::array<uint32_t, 256> &DECODE>
consteval std::array<uint8_t, 256> MakeEncodeTable() {
  std::array<uint8_t, 256> indices;
  for (int i = 0; i < 256; i++) indices[i] = (uint8_t)i;

  // Note: This requires c++20. Might want to have a constexpr
  // shim for older compilers.
  std::sort(indices.begin(), indices.end(), [](uint8_t a, uint8_t b) {
    return SloppyCodepoint(DECODE, a) < SloppyCodepoint(DECODE, b);
  });

  return indices;
}

constexpr std::array<uint8_t, 256> inv_table_windows_1252 =
  MakeEncodeTable<table_windows_1252>();
constexpr std::array<uint8_t, 256> inv_table_windows_1251 =
  MakeEncodeTable<table_windows_1251>();
constexpr std::array<uint8_t, 256> inv_table_windows_1250 =
  MakeEncodeTable<table_windows_1250>();
constexpr std::array<uint8_t, 256> inv_table_windows_1253 =
  MakeEncodeTable<table_windows_1253>();
constexpr std::array<uint8_t, 256> inv_table_windows_1254 =
  MakeEncodeTable<table_windows_1254>();
constexpr std::array<uint8_t, 256> inv_table_windows_1257 =
  MakeEncodeTable<table_windows_1257>();
constexpr std::array<uint8_t, 256> inv_table_iso_8859_2 =
  MakeEncodeTable<table_iso_8859_2>();
constexpr std::array<uint8_t, 256> inv_table_macroman =
  MakeEncodeTable<table_macroman>();
constexpr std::array<uint8_t, 256> inv_table_cp437 =
  MakeEncodeTable<table_cp437>();

template<const std::array<uint32_t, 256> &DATA>
std::optional<std::string> DecodeTable(std::string_view bytes) {
  std::string out;
  out.reserve(bytes.size());
  for (uint8_t byte : bytes) {
    const uint32_t codepoint = DATA[byte];
    if (codepoint == UN_DEF) return std::nullopt;
    out.append(UTF8::Encode(codepoint));
  }
  return out;
}

template<const std::array<uint32_t, 256> &DATA>
std::string SloppyDecodeTable(std::string_view bytes) {
  std::string out;
  out.reserve(bytes.size());
  for (uint8_t byte : bytes) {
    // ftfy uses 0x1A, the "substitute" control code, for
    // UTF replacement char.
    uint32_t cp = byte == 0x1A ? UTF8::REPLACEMENT_CODEPOINT :
      SloppyCodepoint(DATA, byte);
    out.append(UTF8::Encode(cp));
  }
  return out;
}

template<const std::array<uint32_t, 256> &DECODE,
         const std::array<uint8_t, 256> &ENCODE>
static inline std::optional<uint8_t> EncodeOne(uint32_t codepoint) {
  auto it = std::lower_bound(ENCODE.begin(), ENCODE.end(), codepoint,
                             [&](uint8_t index, uint32_t val) {
                               return SloppyCodepoint(DECODE, index) < val;
                             });

  // No byte assigned.
  if (it == ENCODE.end())
    return std::nullopt;

  uint8_t byte = *it;
  if (SloppyCodepoint(DECODE, byte) != codepoint)
    return std::nullopt;

  return {byte};
}


template<const std::array<uint32_t, 256> &DECODE,
         const std::array<uint8_t, 256> &ENCODE>
std::optional<std::string> SloppyEncodeTable(std::string_view str) {
  std::string out;
  out.reserve(str.size());
  for (uint32_t codepoint : UTF8::Decoder(str)) {
    if (codepoint == UTF8::REPLACEMENT_CODEPOINT) {
      out.push_back(0x1A);
    } else {
      auto bo = EncodeOne<DECODE, ENCODE>(codepoint);
      if (!bo.has_value())
        return std::nullopt;

      out.push_back(bo.value());
    }
  }
  return out;
}

template<const std::array<uint32_t, 256> &DECODE,
         const std::array<uint8_t, 256> &ENCODE>
std::optional<std::string> EncodeTable(std::string_view str) {
  std::string out;
  out.reserve(str.size());
  for (uint32_t codepoint : UTF8::Decoder(str)) {
    auto bo = EncodeOne<DECODE, ENCODE>(codepoint);
    if (!bo.has_value())
      return std::nullopt;

    uint32_t cp = DECODE[bo.value()];
    // If the table entry is undefined, then this entry only existed
    // because of the sloppy logic. Reject it when doing strict
    // encoding.
    if (cp == UN_DEF)
      return std::nullopt;

    out.push_back(bo.value());
  }
  return out;
}

struct Latin1Codec : public FixEncoding::TextCodec {
  std::optional<std::string> Encode(std::string_view str) const final {
    std::string out;
    out.reserve(str.size());
    for (uint32_t codepoint : UTF8::Decoder(str)) {
      if (codepoint > 0xFF) return std::nullopt;
      out.push_back((uint8_t)codepoint);
    }
    return out;
  }

  virtual std::optional<std::string> EncodeSloppy(
      std::string_view str) const final {
    return Encode(str);
  }

  virtual std::optional<std::string> Decode(
      std::string_view bytes) const final {
    // Note that decoding Latin-1 always succeeds.
    return {DecodeSloppy(bytes)};
  }

  virtual std::string DecodeSloppy(std::string_view bytes) const final {
    std::string out;
    out.reserve(bytes.size());
    // In Latin-1, byte YY means codepoint U+00YY.
    for (uint8_t codepoint : bytes) {
      out.append(UTF8::Encode((uint32_t)codepoint));
    }
    return out;
  }
};


#define TABLED_CODEC(ClassName, tab)                                    \
  struct ClassName ## Codec : public FixEncoding::TextCodec {           \
    std::optional<std::string> Encode(                                  \
        std::string_view str) const final {                             \
      return EncodeTable<table_ ## tab, inv_table_ ## tab>(str);        \
    }                                                                   \
                                                                        \
    std::optional<std::string>                                          \
    EncodeSloppy(std::string_view bytes) const final {                  \
      return SloppyEncodeTable<table_ ## tab, inv_table_ ## tab>(       \
          bytes);                                                       \
    }                                                                   \
                                                                        \
    std::optional<std::string> Decode(                                  \
        std::string_view bytes) const final {                           \
      return DecodeTable<table_ ## tab>(bytes);                         \
    }                                                                   \
    std::string DecodeSloppy(std::string_view bytes) const final {      \
      return SloppyDecodeTable<table_ ## tab>(bytes);                   \
    }                                                                   \
  };                                                                    \
  const FixEncoding::TextCodec & FixEncoding::ClassName () {            \
    static ClassName ## Codec *codec = new ClassName ## Codec ();       \
    return *codec;                                                      \
  }

TABLED_CODEC(Windows1252, windows_1252);
TABLED_CODEC(Windows1251, windows_1251);
TABLED_CODEC(Windows1250, windows_1250);
TABLED_CODEC(Windows1253, windows_1253);
TABLED_CODEC(Windows1254, windows_1254);
TABLED_CODEC(Windows1257, windows_1257);
TABLED_CODEC(ISO8859_2, iso_8859_2);
TABLED_CODEC(MacRoman, macroman);
TABLED_CODEC(CP437, cp437);

const FixEncoding::TextCodec &FixEncoding::Latin1() {
  static Latin1Codec *codec = new Latin1Codec();
  return *codec;
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

using TextCodec = FixEncoding::TextCodec;

// These are the encodings we will try to fix, in the
// order that they should be tried. Follows ftfy. If the
// boolean is true, the "sloppy" version of the codec should
// be used.
static std::vector<std::tuple<Encoding, bool, const TextCodec *>> ENCODINGS = {
  {Encoding::LATIN1, false, &FixEncoding::Latin1()},
  {Encoding::WINDOWS1252, true, &FixEncoding::Windows1252()},
  {Encoding::WINDOWS1251, true, &FixEncoding::Windows1251()},
  {Encoding::WINDOWS1250, true, &FixEncoding::Windows1250()},
  {Encoding::WINDOWS1253, true, &FixEncoding::Windows1253()},
  {Encoding::WINDOWS1254, true, &FixEncoding::Windows1254()},
  {Encoding::WINDOWS1257, true, &FixEncoding::Windows1257()},
  {Encoding::ISO8859_2, false, &FixEncoding::ISO8859_2()},
  {Encoding::MACROMAN, false, &FixEncoding::MacRoman()},
  {Encoding::CP437, false, &FixEncoding::CP437()},
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
      bytes = FixEncoding::RestoreByteA0(bytes);
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
  std::string fixed = FixEncoding::DecodeInconsistentUTF8(text);
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
      if (auto bytes = FixEncoding::Latin1().Encode(text)) {
        return FixEncoding::Windows1252().DecodeSloppy(bytes.value());
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

static inline bool IsASCII(std::string_view text) {
  for (const uint8_t c : text) {
    if (c >= 0x80) return false;
  }
  return true;
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

  for (;;) {
    std::string orig = text;
    RepeatedlyFix(&text);

    text = FixC1Controls(text);

    if (fixmask & LATIN_LIGATURES) {
      text = FixLatinLigatures(text);
    }

    if (fixmask & UNCURL_QUOTES) {
      text = UncurlQuotes(text);
    }

    if (fixmask & NEWLINES) {
      text = FixLineBreaks(text);
    }

    text = FixSurrogates(text);

    text = RemoveTerminalEscapes(text);

    text = RemoveControlChars(text);

    // Port note: ftfy would do unicode normalization, but that
    // requires ICU or some other source of unicode data. We just
    // leave it unnormalized.

    if (text == orig) return text;
  }
}

