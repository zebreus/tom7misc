
// This file only: A rough port of "ftfy" ("fixes text for you")
// by Robyn Speer (see APACHE20.txt for license).

// Notes for people that don't know Python (like me):
//   Python strings are natively sequences of unicode codepoints.
//   Python has built-in encoders. Encoding is unicode codepoints -> bytes,
//   and decoding is the reverse.

// from chardata.py
// XXX: Nice to just make this one file if possible.


// XXX fix recursive
#include "fix-encoding.h"

#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "base/logging.h"
#include "html-entities.h"
#include "re2-util.h"
#include "re2/re2.h"
#include "text-codec.h"
#include "utf8.h"

// In RE2, setting Latin1 mode allows the input regex to have
// invalid UTF-8, which we need. Note that this affects the match, too.
static constexpr RE2::CannedOptions BINARY_REGEX = RE2::CannedOptions::Latin1;

static LazyRE2 SINGLE_QUOTE_RE = { "[\u02bc\u2018-\u201b]" };
static LazyRE2 DOUBLE_QUOTE_RE = { "[\u201c-\u201f]" };

#if 0
def _build_regexes() -> dict[str, re.Pattern[str]]:
    """
    ENCODING_REGEXES contain reasonably fast ways to detect if we
    could represent a given string in a given encoding. The simplest one is
    the 'ascii' detector, which of course just determines if all characters
    are between U+0000 and U+007F.
    """
    # Define a regex that matches ASCII text.
    encoding_regexes = {"ascii": re.compile("^[\x00-\x7f]*$")}

    for encoding in CHARMAP_ENCODINGS:
        // Make a sequence of characters that bytes \x80 to \xFF decode to
        // in each encoding, as well as byte \x1A, which is used to represent
        // the replacement character � in the sloppy-* encodings.
        byte_range = bytes([*range(0x80, 0x100), 0x1A])
        charlist = byte_range.decode(encoding)

        # The rest of the ASCII bytes -- bytes \x00 to \x19 and \x1B
        # to \x7F -- will decode as those ASCII characters in any encoding we
        # support, so we can just include them as ranges. This also lets us
        # not worry about escaping regex special characters, because all of
        # them are in the \x1B to \x7F range.
        regex = f"^[\x00-\x19\x1b-\x7f{charlist}]*$"
        encoding_regexes[encoding] = re.compile(regex)
    return encoding_regexes

ENCODING_REGEXES = _build_regexes()

def possible_encoding(text: str, encoding: str) -> bool:
    """
    Given text and a single-byte encoding, check whether that text could have
    been decoded from that single-byte encoding.

    In other words, check whether it can be encoded in that encoding, possibly
    sloppily.
    """
    return bool(ENCODING_REGEXES[encoding].match(text))
#endif


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


// Recognize UTF-8 sequences that would be valid if it weren't for a b'\xa0'
// that some Windows-1252 program converted to a plain space.
//
// The smaller values are included on a case-by-case basis, because we don't want
// to decode likely input sequences to unlikely characters. These are the ones
// that *do* form likely characters before 0xa0:
//
//   0xc2 -> U+A0 NO-BREAK SPACE
//   0xc3 -> U+E0 LATIN SMALL LETTER A WITH GRAVE
//   0xc5 -> U+160 LATIN CAPITAL LETTER S WITH CARON
//   0xce -> U+3A0 GREEK CAPITAL LETTER PI
//   0xd0 -> U+420 CYRILLIC CAPITAL LETTER ER
//   0xd9 -> U+660 ARABIC-INDIC DIGIT ZERO
//
// In three-character sequences, we exclude some lead bytes in some cases.
//
// When the lead byte is immediately followed by 0xA0, we shouldn't accept
// a space there, because it leads to some less-likely character ranges:
//
//   0xe0 -> Samaritan script
//   0xe1 -> Mongolian script (corresponds to Latin-1 'á' which is too common)
//
// We accept 0xe2 and 0xe3, which cover many scripts. Bytes 0xe4 and
// higher point mostly to CJK characters, which we generally don't want to
// decode near Latin lowercase letters.
//
// In four-character sequences, the lead byte must be F0, because that accounts
// for almost all of the usage of high-numbered codepoints (tag characters whose
// UTF-8 starts with the byte F3 are only used in some rare new emoji sequences).
//
// This is meant to be applied to encodings of text that tests true for `is_bad`.
// Any of these could represent characters that legitimately appear surrounded by
// spaces, particularly U+C5 (Å), which is a word in multiple languages!
//
// We should consider checking for b'\x85' being converted to ... in the future.
// I've seen it once, but the text still wasn't recoverable.
static LazyRE2 ALTERED_UTF8_RE = {
  "[\xc2\xc3\xc5\xce\xd0\xd9][ ]"
  "|[\xe2\xe3][ ][\x80-\x84\x86-\x9f\xa1-\xbf]"
  "|[\xe0-\xe3][\x80-\x84\x86-\x9f\xa1-\xbf][ ]"
  "|[\xf0][ ][\x80-\xbf][\x80-\xbf]"
  "|[\xf0][\x80-\xbf][ ][\x80-\xbf]"
  "|[\xf0][\x80-\xbf][\x80-\xbf][ ]",
  BINARY_REGEX
};

// This expression matches UTF-8 and CESU-8 sequences where some of the
// continuation bytes have been lost. The byte 0x1a (sometimes written as ^Z) is
// used within ftfy to represent a byte that produced the replacement character
// \ufffd. We don't know which byte it was, but we can at least decode the UTF-8
// sequence as \ufffd instead of failing to re-decode it at all.
//
// In some cases, we allow the ASCII '?' in place of \ufffd, but at most once per
// sequence.
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
static void AppendStrs(std::string *out, Ts &&...ts) {
  ( (out->append(std::format("{}", std::forward<Ts>(ts)))), ... );
}

template<class ...Ts>
static std::string StrCat(Ts &&...ts) {
  std::string out;
  ( (out.append(std::format("{}", std::forward<Ts>(ts)))), ... );
  return out;
}


std::string DecodeInconsistentUTF8(std::string_view text) {
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
std::string RemoveTerminalEscapes(std::string_view text) {
  static LazyRE2 ANSI_RE = { "\033\\[((?:\\d|;)*)([a-zA-Z])" };
  std::string ret(text);
  RE2::GlobalReplace(&ret, *ANSI_RE, "");
  return ret;
}


// Replace curly; quotation marks with straight equivalents.
std::string UncurlQuotes(std::string_view text) {
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
std::string FixLatinLigatures(std::string_view text) {
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
std::string FixLineBreaks(std::string_view text) {
  static LazyRE2 CRLF_RE = { "\r\n" };
  static LazyRE2 WEIRD_LINE_END_RE = { "[\r\u2028\u2029\u0085]" };

  std::string s(text);
  // First replace \r\n so that we don't get double newlines.
  RE2::GlobalReplace(&s, *CRLF_RE, "\n");
  RE2::GlobalReplace(&s, *WEIRD_LINE_END_RE, "\n");
  return s;
}

std::string FixSurrogates(std::string_view text) {
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
std::string RestoreByteA0(std::string_view text) {
  // Port note: ftfy uses negative lookahead, not supported by RE2.
  // Since the match must start with literally "\xc3 " I'm just doing
  // this as a simple find loop for the prefix, and rejecting if it matches
  // this pattern with a (positive)
  static LazyRE2 A_GRAVE_WORD_RE = {
    "^\xc3 (?: |quele|quela|quilo|s )", BINARY_REGEX
  };

  std::string out;
  out.reserve(text.size());

  for (;;) {
    auto pos = text.find("\xc3 ");

    if (pos == std::string_view::npos) {
      out.append(text);
      return out;
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
  return RE2Util::GlobalReplace(str, *LOSSY_UTF8_RE,
                                UTF8::Encode(UTF8::REPLACEMENT_CODEPOINT));
}

// If text still contains C1 control characters, treat them as their
// Windows-1252 equivalents. This matches what Web browsers do.
std::string FixC1Controls(std::string_view str) {
  // This regex matches C1 control characters, which occupy some of the positions
  // in the Latin-1 character map that Windows assigns to other characters instead.
  static LazyRE2 C1_CONTROL_RE = { "[\x80-\x9f]", BINARY_REGEX };

  auto C1Fixer = [](std::span<const std::string_view> match) {
      std::optional<std::string> lat = Latin1().Encode(match[0]);
      CHECK(lat.has_value()) << "Latin-1 should always succeed.";
      return Windows1252().DecodeSloppy(lat.value());
    };

  return RE2Util::MapReplacement(str, *C1_CONTROL_RE, C1Fixer);
}

