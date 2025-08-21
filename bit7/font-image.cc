#include "font-image.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "image.h"
#include "utf8.h"
#include "util.h"

using namespace std;

static constexpr bool VERBOSE = true;

// No stability guarantees for these codepoints. We should
// consider not even outputting them in the TTFs. The purpose
// is to allow for some utility-style glyph pieces in the
// font images that are not deleted by normalization.
enum PrivateUse : uint32_t {
  PUA_START = 0xF7000,
  PUA_LC_SLASH1,
  PUA_SLASH_STEEP,
  PUA_SLASH,
};

// TODO: Need to add page for "old" DFX fonts.

Page Config::ParsePage(std::string_view p) {
  if (p == "bit7-classic") return Page::BIT7_CLASSIC;
  if (p == "bit7-latinabc") return Page::BIT7_LATINABC;
  if (p == "bit7-extended") return Page::BIT7_EXTENDED;
  if (p == "bit7-extended2") return Page::BIT7_EXTENDED2;
  if (p == "bit7-cyrillic") return Page::BIT7_CYRILLIC;
  if (p == "bit7-math") return Page::BIT7_MATH;
  LOG(FATAL) << "Unknown page " << p;
}

const char *Config::PageString(Page p) {
  switch (p) {
  case Page::BIT7_CLASSIC:
    return "bit7-classic";
  case Page::BIT7_LATINABC:
    return "bit7-latinabc";
  case Page::BIT7_EXTENDED:
    return "bit7-extended";
  case Page::BIT7_EXTENDED2:
    return "bit7-extended2";
  case Page::BIT7_CYRILLIC:
    return "bit7-cyrillic";
  case Page::BIT7_MATH:
    return "bit7-math";
  default:
    break;
  }
}

inline static bool PageUsesEmptyGlyphs(Page p) {
  return p == Page::BIT7_CLASSIC;
}

// Tips:
//  - To move glyphs between pages, first duplicate them, then
//    normalize, then set the sources to -1, then normalize again.

// Standard size is: 16x24
const std::vector<int> &PageBit7Classic() {
  static const std::vector<int> CODEPOINTS = {
    // First line
    // BLACK HEART SUIT
    0x2665,
    // BEAMED EIGHTH NOTES
    0x266B,
    // INFINITY
    0x221E,
    // SQUARE ROOT
    0x221A,
    // LESS THAN OR EQUAL TO
    0x2264,
    // GREATER THAN OR EQUAL TO
    0x2265,
    // APPROXIMATELY EQUAL (~ on ~)
    0x2248,
    // EURO SIGN
    0x20AC,
    // ARROWS: LEFT, UP, RIGHT, DOWN
    0x2190, 0x2191, 0x2192, 0x2193,

    // EN DASH, EM DASH
    0x2013, 0x2014,

    // LEFT SINGLE QUOTE, RIGHT SINGLE QUOTE
    0x2018, 0x2019,
    // Second line

    // LEFT DOUBLE QUOTE, RIGHT DOUBLE QUOTE
    0x201C, 0x201D,

    // BULLET
    0x2022,
    // HORIZONTAL ELLIPSIS
    0x2026,
    // SINGLE HIGH REVERSED 9 QUOTE
    0x201B,
    // DOUBLE HIGH REVERSED 9 QUOTE
    0x201F,
    // unclaimed
    -1,

    // dagger, double-dagger
    0x2020, 0x2021,

    // checkmark, heavy checkmark,
    0x2713, 0x2714,
    // ballot x, heavy ballot x,
    0x2717, 0x2718,

    // Trade Mark Sign
    0x2122,

    // Ideographic full stop (big japanese period)
    0x3002,
    // turnstile (a.k.a. right tack)
    0x22A2,


    // INTERROBANG
    0x203D,
    // INVERTED INTERROBANG
    0x2E18,
    // DOUBLE EXCLAMATION MARK
    0x203C,

    // Left and right single guillemets
    0x2039, 0x203A,

    // Unclaimed. Was once emoji, but I moved those to the extended
    // page.
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    // ASCII, in order
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E,
    -1,

    // white king, queen, rook, bishop, knight, pawn
    0x2654,
    0x2655,
    0x2656,
    0x2657,
    0x2658,
    0x2659,
    // black
    0x265A,
    0x265B,
    0x265C,
    0x265D,
    0x265E,
    0x265F,

    // Four free after chess
    -1, -1, -1, -1,

    // Blank line
    // "Black" (filled) suits (Spade, Heart, Club, Diamond)
    0x2660, 0x2663, 0x2665, 0x2666,
    // White (outlined) suits (Spade, Heart, Club, Diamond)
    0x2664, 0x2661, 0x2667, 0x2662,
    // (die faces?)
    -1, -1, -1, -1, -1, -1, -1, -1,

    // Unicode Latin-1 Supplement, mapped to itself.
    // See https://en.wikibooks.org/wiki/Unicode/Character_reference/0000-0FFF
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
    0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,

    // unclaimed
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    // Greek. We skip the characters that look the same in the Latin
    // alphabet: A B E Z H I K M N O P T Y X v u x.

    // Gamma, Delta, Theta, Lambda, Xi
    0x0393, 0x0394, 0x0398, 0x039B, 0x039E,
    // Pi, Sigma, Phi, Psi, Omega,
    0x03A0, 0x03A3, 0x03A6, 0x03A8, 0x03A9,
    // alpha, beta, gamma, delta, epsilon, zeta
    0x03B1, 0x03B2, 0x03B3, 0x03B4, 0x03B5, 0x03B6,

    // Line 2:
    // eta, theta, iota, kappa, lambda, mu, (no nu), xi, (no omicron)
    0x03B7, 0x03B8, 0x03B9, 0x03BA, 0x03BB, 0x03BC, 0x03BE,
    // pi, rho, (final) sigma, sigma, tau, (no upsilon), phi, (no chi), omega
    0x03C0, 0x03C1, 0x03C2, 0x03C3, 0x03C4, 0x03C6, 0x03C8, 0x03C9,
    // one unclaimed spot at the end of greek
    -1,

    // This was once some basic math symbols, but I moved them
    // to the proper unicode page.
    // Black circle, black square
    0x25CF,
    0x25A0,
    // geometric shapes and bullets, unclaimed
    -1,
    0x203B, // reference mark
    // cont'd
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    // <?> replacement char
    0xFFFD,

    // Block Elements, in unicode order
    0x2580, 0x2581, 0x2582, 0x2583, 0x2584, 0x2585, 0x2586, 0x2587,
    0x2588, 0x2589, 0x258A, 0x258B, 0x258C, 0x258D, 0x258E, 0x258F,
    0x2590, 0x2591, 0x2592, 0x2593, 0x2594, 0x2595, 0x2596, 0x2597,
    0x2598, 0x2599, 0x259A, 0x259B, 0x259C, 0x259D, 0x259E, 0x259F,
  };

  CHECK(CODEPOINTS.size() == 16 * 24);
  return CODEPOINTS;
}

// Standard size is: 16x24
const std::vector<int> &PageBit7LatinABC() {
  static const std::vector<int> CODEPOINTS = {
    // U+0100 through U+017F: Latin Extended-A
    0x0100,  // (Ā) Latin Capital letter A with macron
    0x0101,  // (ā) Latin Small letter A with macron
    0x0102,  // (Ă) Latin Capital letter A with breve
    0x0103,  // (ă) Latin Small letter A with breve
    0x0104,  // (Ą) Latin Capital letter A with ogonek
    0x0105,  // (ą) Latin Small letter A with ogonek
    0x0106,  // (Ć) Latin Capital letter C with acute
    0x0107,  // (ć) Latin Small letter C with acute
    0x0108,  // (Ĉ) Latin Capital letter C with circumflex
    0x0109,  // (ĉ) Latin Small letter C with circumflex
    0x010A,  // (Ċ) Latin Capital letter C with dot above
    0x010B,  // (ċ) Latin Small letter C with dot above
    0x010C,  // (Č) Latin Capital letter C with caron
    0x010D,  // (č) Latin Small letter C with caron
    0x010E,  // (Ď) Latin Capital letter D with caron
    0x010F,  // (ď) Latin Small letter D with caron
    0x0110,  // (Đ) Latin Capital letter D with stroke
    0x0111,  // (đ) Latin Small letter D with stroke
    0x0112,  // (Ē) Latin Capital letter E with macron
    0x0113,  // (ē) Latin Small letter E with macron
    0x0114,  // (Ĕ) Latin Capital letter E with breve
    0x0115,  // (ĕ) Latin Small letter E with breve
    0x0116,  // (Ė) Latin Capital letter E with dot above
    0x0117,  // (ė) Latin Small letter E with dot above
    0x0118,  // (Ę) Latin Capital letter E with ogonek
    0x0119,  // (ę) Latin Small letter E with ogonek
    0x011A,  // (Ě) Latin Capital letter E with caron
    0x011B,  // (ě) Latin Small letter E with caron
    0x011C,  // (Ĝ) Latin Capital letter G with circumflex
    0x011D,  // (ĝ) Latin Small letter G with circumflex
    0x011E,  // (Ğ) Latin Capital letter G with breve
    0x011F,  // (ğ) Latin Small letter G with breve
    0x0120,  // (Ġ) Latin Capital letter G with dot above
    0x0121,  // (ġ) Latin Small letter G with dot above
    0x0122,  // (Ģ) Latin Capital letter G with cedilla
    0x0123,  // (ģ) Latin Small letter G with cedilla
    0x0124,  // (Ĥ) Latin Capital letter H with circumflex
    0x0125,  // (ĥ) Latin Small letter H with circumflex
    0x0126,  // (Ħ) Latin Capital letter H with stroke
    0x0127,  // (ħ) Latin Small letter H with stroke
    0x0128,  // (Ĩ) Latin Capital letter I with tilde
    0x0129,  // (ĩ) Latin Small letter I with tilde
    0x012A,  // (Ī) Latin Capital letter I with macron
    0x012B,  // (ī) Latin Small letter I with macron
    0x012C,  // (Ĭ) Latin Capital letter I with breve
    0x012D,  // (ĭ) Latin Small letter I with breve
    0x012E,  // (Į) Latin Capital letter I with ogonek
    0x012F,  // (į) Latin Small letter I with ogonek
    0x0130,  // (İ) Latin Capital letter I with dot above
    0x0131,  // (ı) Latin Small letter dotless I
    0x0132,  // (Ĳ) Latin Capital Ligature IJ
    0x0133,  // (ĳ) Latin Small Ligature IJ
    0x0134,  // (Ĵ) Latin Capital letter J with circumflex
    0x0135,  // (ĵ) Latin Small letter J with circumflex
    0x0136,  // (Ķ) Latin Capital letter K with cedilla
    0x0137,  // (ķ) Latin Small letter K with cedilla
    0x0138,  // (ĸ) Latin Small letter Kra
    0x0139,  // (Ĺ) Latin Capital letter L with acute
    0x013A,  // (ĺ) Latin Small letter L with acute
    0x013B,  // (Ļ) Latin Capital letter L with cedilla
    0x013C,  // (ļ) Latin Small letter L with cedilla
    0x013D,  // (Ľ) Latin Capital letter L with caron
    0x013E,  // (ľ) Latin Small letter L with caron
    0x013F,  // (Ŀ) Latin Capital letter L with middle dot
    0x0140,  // (ŀ) Latin Small letter L with middle dot
    0x0141,  // (Ł) Latin Capital letter L with stroke
    0x0142,  // (ł) Latin Small letter L with stroke
    0x0143,  // (Ń) Latin Capital letter N with acute
    0x0144,  // (ń) Latin Small letter N with acute
    0x0145,  // (Ņ) Latin Capital letter N with cedilla
    0x0146,  // (ņ) Latin Small letter N with cedilla
    0x0147,  // (Ň) Latin Capital letter N with caron
    0x0148,  // (ň) Latin Small letter N with caron
    0x0149,  // (ŉ) Latin Small letter N preceded by apostrophe (deprecated!)
    0x014A,  // (Ŋ) Latin Capital letter Eng
    0x014B,  // (ŋ) Latin Small letter Eng
    0x014C,  // (Ō) Latin Capital letter O with macron
    0x014D,  // (ō) Latin Small letter O with macron
    0x014E,  // (Ŏ) Latin Capital letter O with breve
    0x014F,  // (ŏ) Latin Small letter O with breve
    0x0150,  // (Ő) Latin Capital Letter O with double acute
    0x0151,  // (ő) Latin Small Letter O with double acute
    0x0152,  // (Œ) Latin Capital Ligature OE
    0x0153,  // (œ) Latin Small Ligature OE
    0x0154,  // (Ŕ) Latin Capital letter R with acute
    0x0155,  // (ŕ) Latin Small letter R with acute
    0x0156,  // (Ŗ) Latin Capital letter R with cedilla
    0x0157,  // (ŗ) Latin Small letter R with cedilla
    0x0158,  // (Ř) Latin Capital letter R with caron
    0x0159,  // (ř) Latin Small letter R with caron
    0x015A,  // (Ś) Latin Capital letter S with acute
    0x015B,  // (ś) Latin Small letter S with acute
    0x015C,  // (Ŝ) Latin Capital letter S with circumflex
    0x015D,  // (ŝ) Latin Small letter S with circumflex
    0x015E,  // (Ş) Latin Capital letter S with cedilla
    0x015F,  // (ş) Latin Small letter S with cedilla
    0x0160,  // (Š) Latin Capital letter S with caron
    0x0161,  // (š) Latin Small letter S with caron
    0x0162,  // (Ţ) Latin Capital letter T with cedilla
    0x0163,  // (ţ) Latin Small letter T with cedilla
    0x0164,  // (Ť) Latin Capital letter T with caron
    0x0165,  // (ť) Latin Small letter T with caron
    0x0166,  // (Ŧ) Latin Capital letter T with stroke
    0x0167,  // (ŧ) Latin Small letter T with stroke
    0x0168,  // (Ũ) Latin Capital letter U with tilde
    0x0169,  // (ũ) Latin Small letter U with tilde
    0x016A,  // (Ū) Latin Capital letter U with macron
    0x016B,  // (ū) Latin Small letter U with macron
    0x016C,  // (Ŭ) Latin Capital letter U with breve
    0x016D,  // (ŭ) Latin Small letter U with breve
    0x016E,  // (Ů) Latin Capital letter U with ring above
    0x016F,  // (ů) Latin Small letter U with ring above
    0x0170,  // (Ű) Latin Capital Letter U with double acute
    0x0171,  // (ű) Latin Small Letter U with double acute
    0x0172,  // (Ų) Latin Capital letter U with ogonek
    0x0173,  // (ų) Latin Small letter U with ogonek
    0x0174,  // (Ŵ) Latin Capital letter W with circumflex
    0x0175,  // (ŵ) Latin Small letter W with circumflex
    0x0176,  // (Ŷ) Latin Capital letter Y with circumflex
    0x0177,  // (ŷ) Latin Small letter Y with circumflex
    0x0178,  // (Ÿ) Latin Capital letter Y with diaeresis
    0x0179,  // (Ź) Latin Capital letter Z with acute
    0x017A,  // (ź) Latin Small letter Z with acute
    0x017B,  // (Ż) Latin Capital letter Z with dot above
    0x017C,  // (ż) Latin Small letter Z with dot above
    0x017D,  // (Ž) Latin Capital letter Z with caron
    0x017E,  // (ž) Latin Small letter Z with caron
    0x017F,  // (ſ) Latin Small letter long S

    // Latin Extended-B
    0x0180,  // (ƀ) Latin Small Letter B with Stroke
    0x0181,  // (Ɓ) Latin Capital Letter B with Hook
    0x0182,  // (Ƃ) Latin Capital Letter B with Top Bar
    0x0183,  // (ƃ) Latin Small Letter B with Top Bar
    0x0184,  // (Ƅ) Latin Capital Letter Tone Six
    0x0185,  // (ƅ) Latin Small Letter Tone Six
    0x0186,  // (Ɔ) Latin Capital Letter Open O
    0x0187,  // (Ƈ) Latin Capital Letter C with Hook
    0x0188,  // (ƈ) Latin Small Letter C with Hook
    0x0189,  // (Ɖ) Latin Capital Letter African D
    0x018A,  // (Ɗ) Latin Capital Letter D with Hook
    0x018B,  // (Ƌ) Latin Capital Letter D with Top Bar
    0x018C,  // (ƌ) Latin Small Letter D with Top Bar
    0x018D,  // (ƍ) Latin Small Letter Turned Delta
    0x018E,  // (Ǝ) Latin Capital Letter Reversed E
    0x018F,  // (Ə) Latin Capital Letter Schwa
    0x0190,  // (Ɛ) Latin Capital Letter Open E (= Latin Capital Letter Epsilon)
    0x0191,  // (Ƒ) Latin Capital Letter F with Hook
    0x0192,  // (ƒ) Latin Small Letter F with Hook
    0x0193,  // (Ɠ) Latin Capital Letter G with Hook
    0x0194,  // (Ɣ) Latin Capital Letter Gamma
    0x0195,  // (ƕ) Latin Small Letter HV
    0x0196,  // (Ɩ) Latin Capital Letter Iota
    0x0197,  // (Ɨ) Latin Capital Letter I with Stroke
    0x0198,  // (Ƙ) Latin Capital Letter K with Hook
    0x0199,  // (ƙ) Latin Small Letter K with Hook
    0x019A,  // (ƚ) Latin Small Letter L with Bar
    0x019B,  // (ƛ) Latin Small Letter Lambda with Stroke
    0x019C,  // (Ɯ) Latin Capital Letter Turned M
    0x019D,  // (Ɲ) Latin Capital Letter N with Left Hook
    0x019E,  // (ƞ) Latin Small Letter N with Long Right Leg
    0x019F,  // (Ɵ) Latin Capital Letter O with Middle Tilde
    0x01A0,  // (Ơ) Latin Capital Letter O with Horn
    0x01A1,  // (ơ) Latin Small Letter O with Horn
    0x01A2,  // (Ƣ) Latin Capital Letter OI (= Latin Capital Letter Gha)
    0x01A3,  // (ƣ) Latin Small Letter OI (= Latin Small Letter Gha)
    0x01A4,  // (Ƥ) Latin Capital Letter P with Hook
    0x01A5,  // (ƥ) Latin Small Letter P with Hook
    0x01A6,  // (Ʀ) Latin Letter YR
    0x01A7,  // (Ƨ) Latin Capital Letter Tone Two
    0x01A8,  // (ƨ) Latin Small Letter Tone Two
    0x01A9,  // (Ʃ) Latin Capital Letter Esh
    0x01AA,  // (ƪ) Latin Letter Reversed Esh Loop
    0x01AB,  // (ƫ) Latin Small Letter T with Palatal Hook
    0x01AC,  // (Ƭ) Latin Capital Letter T with Hook
    0x01AD,  // (ƭ) Latin Small Letter T with Hook
    0x01AE,  // (Ʈ) Latin Capital Letter T with Retroflex Hook
    0x01AF,  // (Ư) Latin Capital Letter U with Horn
    0x01B0,  // (ư) Latin Small Letter U with Horn
    0x01B1,  // (Ʊ) Latin Capital Letter Upsilon
    0x01B2,  // (Ʋ) Latin Capital Letter V with Hook
    0x01B3,  // (Ƴ) Latin Capital Letter Y with Hook
    0x01B4,  // (ƴ) Latin Small Letter Y with Hook
    0x01B5,  // (Ƶ) Latin Capital Letter Z with Stroke
    0x01B6,  // (ƶ) Latin Small Letter Z with Stroke
    0x01B7,  // (Ʒ) Latin Capital Letter Ezh
    0x01B8,  // (Ƹ) Latin Capital Letter Ezh Reversed
    0x01B9,  // (ƹ) Latin Small Letter Ezh Reversed
    0x01BA,  // (ƺ) Latin Small Letter Ezh with Tail
    0x01BB,  // (ƻ) Latin Letter Two with Stroke
    0x01BC,  // (Ƽ) Latin Capital Letter Tone Five
    0x01BD,  // (ƽ) Latin Small Letter Tone Five
    0x01BE,  // (ƾ) Latin Letter Inverted Glottal Stop with Stroke
    0x01BF,  // (ƿ) Latin Letter Wynn
    // African letters for clicks
    0x01C0,  // (ǀ) Latin Letter Dental Click
    0x01C1,  // (ǁ) Latin Letter Lateral Click
    0x01C2,  // (ǂ) Latin Letter Alveolar Click
    0x01C3,  // (ǃ) Latin Letter Retroflex Click
    // Croatian digraphs matching Serbian Cyrillic letters
    0x01C4,  // (Ǆ) Latin Capital Letter DZ with Caron
    0x01C5,  // (ǅ) Latin Capital Letter D with Small Letter Z with Caron
    0x01C6,  // (ǆ) Latin Small Letter DZ with Caron
    0x01C7,  // (Ǉ) Latin Capital Letter LJ
    0x01C8,  // (ǈ) Latin Capital Letter L with Small Letter J
    0x01C9,  // (ǉ) Latin Small Letter LJ
    0x01CA,  // (Ǌ) Latin Capital Letter NJ
    0x01CB,  // (ǋ) Latin Capital Letter N with Small Letter J
    0x01CC,  // (ǌ) Latin Small Letter NJ
    // Pinyin diacritic-vowel combinations
    0x01CD,  // (Ǎ) Latin Capital Letter A with Caron
    0x01CE,  // (ǎ) Latin Small Letter A with Caron
    0x01CF,  // (Ǐ) Latin Capital Letter I with Caron
    0x01D0,  // (ǐ) Latin Small Letter I with Caron
    0x01D1,  // (Ǒ) Latin Capital Letter O with Caron
    0x01D2,  // (ǒ) Latin Small Letter O with Caron
    0x01D3,  // (Ǔ) Latin Capital Letter U with Caron
    0x01D4,  // (ǔ) Latin Small Letter U with Caron
    0x01D5,  // (Ǖ) Latin Capital Letter U with Diaeresis and Macron
    0x01D6,  // (ǖ) Latin Small Letter U with Diaeresis and Macron
    0x01D7,  // (Ǘ) Latin Capital Letter U with Diaeresis and Acute
    0x01D8,  // (ǘ) Latin Small Letter U with Diaeresis and Acute
    0x01D9,  // (Ǚ) Latin Capital Letter U with Diaeresis and Caron
    0x01DA,  // (ǚ) Latin Small Letter U with Diaeresis and Caron
    0x01DB,  // (Ǜ) Latin Capital Letter U with Diaeresis and Grave
    0x01DC,  // (ǜ) Latin Small Letter U with Diaeresis and Grave
    // Phonetic and historic letters
    0x01DD,  // (ǝ) Latin Small Letter Turned E
    0x01DE,  // (Ǟ) Latin Capital Letter A with Diaeresis and Macron
    0x01DF,  // (ǟ) Latin Small Letter A with Diaeresis and Macron
    0x01E0,  // (Ǡ) Latin Capital Letter A with Dot Above and Macron
    0x01E1,  // (ǡ) Latin Small Letter A with Dot Above and Macron
    0x01E2,  // (Ǣ) Latin Capital Letter AE with Macron
    0x01E3,  // (ǣ) Latin Small Letter AE with Macron
    0x01E4,  // (Ǥ) Latin Capital Letter G with Stroke
    0x01E5,  // (ǥ) Latin Small Letter G with Stroke
    0x01E6,  // (Ǧ) Latin Capital Letter G with Caron
    0x01E7,  // (ǧ) Latin Small Letter G with Caron
    0x01E8,  // (Ǩ) Latin Capital Letter K with Caron
    0x01E9,  // (ǩ) Latin Small Letter K with Caron
    0x01EA,  // (Ǫ) Latin Capital Letter O with Ogonek
    0x01EB,  // (ǫ) Latin Small Letter O with Ogonek
    0x01EC,  // (Ǭ) Latin Capital Letter O with Ogonek and Macron
    //          (=Latin Capital Letter O with Macron and Ogonek)
    0x01ED,  // (ǭ) Latin Small Letter O with Ogonek and Macron
    //          (=Latin Small Letter O with Macron and Ogonek)
    0x01EE,  // (Ǯ) Latin Capital Letter Ezh with Caron
    0x01EF,  // (ǯ) Latin Small Letter Ezh with Caron
    0x01F0,  // (ǰ) Latin Small Letter J with Caron
    0x01F1,  // (Ǳ) Latin Capital Letter DZ
    0x01F2,  // (ǲ) Latin Capital Letter D with Small Letter Z
    0x01F3,  // (ǳ) Latin Small Letter DZ
    0x01F4,  // (Ǵ) Latin Capital Letter G with Acute
    0x01F5,  // (ǵ) Latin Small Letter G with Acute
    0x01F6,  // (Ƕ) Latin Capital Letter Hwair
    0x01F7,  // (Ƿ) Latin Capital Letter Wynn
    0x01F8,  // (Ǹ) Latin Capital Letter N with Grave
    0x01F9,  // (ǹ) Latin Small Letter N with Grave
    0x01FA,  // (Ǻ) Latin Capital Letter A with Ring Above and Acute
    0x01FB,  // (ǻ) Latin Small Letter A with Ring Above and Acute
    0x01FC,  // (Ǽ) Latin Capital Letter AE with Acute
    0x01FD,  // (ǽ) Latin Small Letter AE with Acute
    0x01FE,  // (Ǿ) Latin Capital Letter O with Stroke and Acute
    0x01FF,  // (ǿ) Latin Small Letter O with Stroke and Acute
    // Additions for Slovenian and Croatian
    0x0200,  // (Ȁ) Latin Capital Letter A with Double Grave
    0x0201,  // (ȁ) Latin Small Letter A with Double Grave
    0x0202,  // (Ȃ) Latin Capital Letter A with Inverted Breve
    0x0203,  // (ȃ) Latin Small Letter A with Inverted Breve
    0x0204,  // (Ȅ) Latin Capital Letter E with Double Grave
    0x0205,  // (ȅ) Latin Small Letter E with Double Grave
    0x0206,  // (Ȇ) Latin Capital Letter E with Inverted Breve
    0x0207,  // (ȇ) Latin Small Letter E with Inverted Breve
    0x0208,  // (Ȉ) Latin Capital Letter I with Double Grave
    0x0209,  // (ȉ) Latin Small Letter I with Double Grave
    0x020A,  // (Ȋ) Latin Capital Letter I with Inverted Breve
    0x020B,  // (ȋ) Latin Small Letter I with Inverted Breve
    0x020C,  // (Ȍ) Latin Capital Letter O with Double Grave
    0x020D,  // (ȍ) Latin Small Letter O with Double Grave
    0x020E,  // (Ȏ) Latin Capital Letter O with Inverted Breve
    0x020F,  // (ȏ) Latin Small Letter O with Inverted Breve
    0x0210,  // (Ȑ) Latin Capital Letter R with Double Grave
    0x0211,  // (ȑ) Latin Small Letter R with Double Grave
    0x0212,  // (Ȓ) Latin Capital Letter R with Inverted Breve
    0x0213,  // (ȓ) Latin Small Letter R with Inverted Breve
    0x0214,  // (Ȕ) Latin Capital Letter U with Double Grave
    0x0215,  // (ȕ) Latin Small Letter U with Double Grave
    0x0216,  // (Ȗ) Latin Capital Letter U with Inverted Breve
    0x0217,  // (ȗ) Latin Small Letter U with Inverted Breve
    // Additions for Romanian
    0x0218,  // (Ș) Latin Capital Letter S with Comma Below
    0x0219,  // (ș) Latin Small Letter S with Comma Below
    0x021A,  // (Ț) Latin Capital Letter T with Comma Below
    0x021B,  // (ț) Latin Small Letter T with Comma Below
    // Miscellaneous additions
    0x021C,  // (Ȝ) Latin Capital Letter Yogh
    0x021D,  // (ȝ) Latin Small Letter Yogh
    0x021E,  // (Ȟ) Latin Capital Letter H with Caron
    0x021F,  // (ȟ) Latin Small Letter H with Caron
    0x0220,  // (Ƞ) Latin Capital Letter N with Long Right Leg
    0x0221,  // (ȡ) Latin Small Letter D with Curl
    0x0222,  // (Ȣ) Latin Capital Letter OU
    0x0223,  // (ȣ) Latin Small Letter OU
    0x0224,  // (Ȥ) Latin Capital Letter Z with Hook
    0x0225,  // (ȥ) Latin Small Letter Z with Hook
    0x0226,  // (Ȧ) Latin Capital Letter A with Dot Above
    0x0227,  // (ȧ) Latin Small Letter A with Dot Above
    0x0228,  // (Ȩ) Latin Capital Letter E with Cedilla
    0x0229,  // (ȩ) Latin Small Letter E with Cedilla
    // Additions for Livonian
    0x022A,  // (Ȫ) Latin Capital Letter O with Diaeresis and Macron
    0x022B,  // (ȫ) Latin Small Letter O with Diaeresis and Macron
    0x022C,  // (Ȭ) Latin Capital Letter O with Tilde and Macron
    0x022D,  // (ȭ) Latin Small Letter O with Tilde and Macron
    0x022E,  // (Ȯ) Latin Capital Letter O with Dot Above
    0x022F,  // (ȯ) Latin Small Letter O with Dot Above
    0x0230,  // (Ȱ) Latin Capital Letter O with Dot Above and Macron
    0x0231,  // (ȱ) Latin Small Letter O with Dot Above and Macron
    0x0232,  // (Ȳ) Latin Capital Letter Y with Macron
    0x0233,  // (ȳ) Latin Small Letter Y with Macron
    // Additions for Sinology
    0x0234,  // (ȴ) Latin Small Letter L with Curl
    0x0235,  // (ȵ) Latin Small Letter N with Curl
    0x0236,  // (ȶ) Latin Small Letter T with Curl
    // Miscellaneous addition
    0x0237,  // (ȷ) Latin Small Letter Dotless J
    // Additions for Africanist linguistics
    0x0238,  // (ȸ) Latin Small Letter DB Digraph
    0x0239,  // (ȹ) Latin Small Letter QP Digraph
    // Additions for Sencoten
    0x023A,  // (Ⱥ) Latin Capital Letter A with Stroke
    0x023B,  // (Ȼ) Latin Capital Letter C with Stroke
    0x023C,  // (ȼ) Latin Small Letter C with Stroke
    0x023D,  // (Ƚ) Latin Capital Letter L with Bar
    0x023E,  // (Ⱦ) Latin Capital Letter T with Diagonal Stroke
    // Additions for Africanist linguistics
    0x023F,  // (ȿ) Latin Small Letter S with Swash Tail
    0x0240,  // (ɀ) Latin Small Letter Z with Swash Tail
    // Miscellaneous additions
    0x0241,  // (Ɂ) Latin Capital Letter Glottal Stop
    0x0242,  // (ɂ) Latin Small Letter Glottal Stop
    0x0243,  // (Ƀ) Latin Capital Letter B with Stroke
    0x0244,  // (Ʉ) Latin Capital Letter U Bar
    0x0245,  // (Ʌ) Latin Capital Letter Turned V
    0x0246,  // (Ɇ) Latin Capital Letter E with Stroke
    0x0247,  // (ɇ) Latin Small Letter E with Stroke
    0x0248,  // (Ɉ) Latin Capital Letter J with Stroke
    0x0249,  // (ɉ) Latin Small Letter J with Stroke
    0x024A,  // (Ɋ) Latin Capital Letter Q with Hook Tail
    0x024B,  // (ɋ) Latin Small Letter Q with Hook Tail
    0x024C,  // (Ɍ) Latin Capital Letter R with Stroke
    0x024D,  // (ɍ) Latin Small Letter R with Stroke
    0x024E,  // (Ɏ) Latin Capital Letter Y with Stroke
    0x024F,  // (ɏ) Latin Small Letter Y with Stroke

    // Latin Extended-C
    0x2C60,  // (Ⱡ) LATIN CAPITAL LETTER L WITH DOUBLE BAR
    0x2C61,  // (ⱡ) LATIN SMALL LETTER L WITH DOUBLE BAR
    0x2C62,  // (Ɫ) LATIN CAPITAL LETTER L WITH MIDDLE TILDE
    0x2C63,  // (Ᵽ) LATIN CAPITAL LETTER P WITH STROKE
    0x2C64,  // (Ɽ) LATIN CAPITAL LETTER R WITH TAIL
    0x2C65,  // (ⱥ) LATIN SMALL LETTER A WITH STROKE
    0x2C66,  // (ⱦ) LATIN SMALL LETTER T WITH DIAGONAL
    0x2C67,  // (Ⱨ) LATIN CAPITAL LETTER H WITH DESCENDER
    0x2C68,  // (ⱨ) LATIN SMALL LETTER H WITH DESCENDER
    0x2C69,  // (Ⱪ) LATIN CAPITAL LETTER K WITH DESCENDER
    0x2C6A,  // (ⱪ) LATIN SMALL LETTER K WITH DESCENDER
    0x2C6B,  // (Ⱬ) LATIN CAPITAL LETTER Z WITH DESCENDER
    0x2C6C,  // (ⱬ) LATIN SMALL LETTER Z WITH DESCENDER
    0x2C6D,  // (Ɑ) LATIN CAPITAL LETTER ALPHA
    0x2C6E,  // (Ɱ) LATIN CAPITAL LETTER M WITH HOOK
    0x2C6F,  // (Ɐ) LATIN CAPITAL LETTER TURNED A

    0x2C70,  // (Ɒ) LATIN CAPITAL LETTER TURNED ALPHA
    0x2C71,  // (ⱱ) LATIN SMALL LETTER V WITH RIGHT HOOK
    0x2C72,  // (Ⱳ) LATIN CAPITAL LETTER W WITH HOOK
    0x2C73,  // (ⱳ) LATIN SMALL LETTER W WITH HOOK
    0x2C74,  // (ⱴ) LATIN SMALL LETTER V WITH CURL
    0x2C75,  // (Ⱶ) LATIN CAPITAL LETTER HALF H
    0x2C76,  // (ⱶ) LATIN SMALL LETTER HALF H
    0x2C77,  // (ⱷ) LATIN SMALL LETTER TAILLESS PHI
    0x2C78,  // (ⱸ) LATIN SMALL LETTER E WITH NOTCH
    0x2C79,  // (ⱹ) LATIN SMALL LETTER TURNED R WITH TAIL
    0x2C7A,  // (ⱺ) LATIN SMALL LETTER O WITH LOW RING INSIDE
    0x2C7B,  // (ⱻ) LATIN LETTER SMALL CAPITAL TURNED E
    0x2C7C,  // (ⱼ) LATIN SUBSCRIPT SMALL LETTER J
    0x2C7D,  // (ⱽ) MODIFIER LETTER CAPITAL V
    0x2C7E,  // (Ȿ) LATIN CAPITAL LETTER S WITH SWASH TAIL
    0x2C7F,  // (Ɀ) LATIN CAPITAL LETTER Z WITH SWASH TAIL

    // Private area for utility glyph pieces (slashes)
    PUA_LC_SLASH1,
    PUA_SLASH_STEEP,
    PUA_SLASH,

    // 16 unused slots
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  };

  CHECK(CODEPOINTS.size() == 16 * 24) << CODEPOINTS.size();
  return CODEPOINTS;
}

// TODO: Pages could also define sections, which just get colored
// differently in generated images.

// Standard size is: 16x24
const std::vector<int> &PageBit7Extended() {
  static const std::vector<int> CODEPOINTS = {
    // space for emoji
    // EMOJI: LIGHT BULB
    0x1F4A1,
    // EMOJI: BEER MUG
    0x1F37A,
    // EMOJI: WASTEBASKET
    0x1F5D1,
    // EMOJI: MOAI HEAD
    0x1F5FF,
    // EMOJI: HIGH VOLTAGE
    0x26A1,
    // EMOJI: MAGNET
    0x1F9F2,
    // EMOJI: SKULL
    0x1F480,
    // EMOJI: SKULL AND CROSSBONES
    0x2620,
    // EMOJI: DROPLET
    0x1F4A7,
    // EMOJI: HUNDRED POINTS
    0x1F4AF,
    // EMOJI: ANGER SYMBOL
    0x1F4A2,
    // EMOJI: ZZZ
    0x1F4A4,
    // EMOJI: PAGE FACING UP
    0x1F4C4,
    // EMOJI: BOMB
    0x1F4A3,
    // EMOJI: GLOBE WITH MERIDIANS
    0x1F310,
    // EMOJI: EYES
    0x1F440,

    // Emoji line 2.

    // EMOJI: TOOTHBRUSH
    0x1FAA5,
    // EMOJI: HEADSTONE
    0x1FAA6,
    // EMOJI: PLACARD (Signpost)
    0x1FAA7,
    // EMOJI: ROCK
    0x1FAA8,
    // EMJOI: FLY
    0x1FAB0,

    // EMOJI: MAGIC WAND
    0x1FA84,
    // EMOJI: COIN
    0x1FA99,
    // EMOJI: LADDER
    0x1FA9C,

    // EMOJI: HOT PEPPER
    0x1F336,

    // EMOJI: GHOST
    0x1F47B,

    // EMOJI: KEY
    0x1F511,

    // EMOJI: LOCK (LOCKED)
    0x1F512,
    // EMOJI: OPEN LOCK
    0x1F513,

    // EMOJI: HEAVY DOLLAR SIGN
    0x1F4B2,

    // EMOJI: FIRE
    0x1F525,

    // EMOJI: BONE
    0x1F9B4,

    // EMOJI: CLOUD
    0x2601,
    // EMOJI: ROCKET
    0x1F680,
    // EMOJI: NO ENTRY (horizontal)
    0x26D4,
    // No Entry Sign (diagonal slash)
    0x1F6AB,
    // Hourglass
    0x231B,
    // Pile of poo
    0x1F4A9,

    // Film strip.
    // I think there's also an emoji presentation of this
    // when followed by U+FE0F.
    0x1F39E,

    // "Place of interest" (mac "open apple" key)
    0x2318,

    -1, -1, -1, -1, -1, -1, -1, -1,

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    // Unicode Katakana
    // 96 characters in unicode order, U+30A0 to U+30FF.

    0x30A0, // ゠ Katakana-Hiragana Double Hyphen
    0x30A1, // ァ Katakana Letter Small A
    0x30A2, // ア Katakana Letter A
    0x30A3, // ィ Katakana Letter Small I
    0x30A4, // イ Katakana Letter I
    0x30A5, // ゥ Katakana Letter Small U
    0x30A6, // ウ Katakana Letter U
    0x30A7, // ェ Katakana Letter Small E
    0x30A8, // エ Katakana Letter E
    0x30A9, // ォ Katakana Letter Small O
    0x30AA, // オ Katakana Letter O
    0x30AB, // カ Katakana Letter Ka
    0x30AC, // ガ Katakana Letter Ga
    0x30AD, // キ Katakana Letter Ki
    0x30AE, // ギ Katakana Letter Gi
    0x30AF, // ク Katakana Letter Ku
    0x30B0, // グ Katakana Letter Gu
    0x30B1, // ケ Katakana Letter Ke
    0x30B2, // ゲ Katakana Letter Ge
    0x30B3, // コ Katakana Letter Ko
    0x30B4, // ゴ Katakana Letter Go
    0x30B5, // サ Katakana Letter Sa
    0x30B6, // ザ Katakana Letter Za
    0x30B7, // シ Katakana Letter Si
    0x30B8, // ジ Katakana Letter Zi
    0x30B9, // ス Katakana Letter Su
    0x30BA, // ズ Katakana Letter Zu
    0x30BB, // セ Katakana Letter Se
    0x30BC, // ゼ Katakana Letter Ze
    0x30BD, // ソ Katakana Letter So
    0x30BE, // ゾ Katakana Letter Zo
    0x30BF, // タ Katakana Letter Ta
    0x30C0, // ダ Katakana Letter Da
    0x30C1, // チ Katakana Letter Ti
    0x30C2, // ヂ Katakana Letter Di
    0x30C3, // ッ Katakana Letter Small Tu
    0x30C4, // ツ Katakana Letter Tu
    0x30C5, // ヅ Katakana Letter Du
    0x30C6, // テ Katakana Letter Te
    0x30C7, // デ Katakana Letter De
    0x30C8, // ト Katakana Letter To
    0x30C9, // ド Katakana Letter Do
    0x30CA, // ナ Katakana Letter Na
    0x30CB, // ニ Katakana Letter Ni
    0x30CC, // ヌ Katakana Letter Nu
    0x30CD, // ネ Katakana Letter Ne
    0x30CE, // ノ Katakana Letter No
    0x30CF, // ハ Katakana Letter Ha
    0x30D0, // バ Katakana Letter Ba
    0x30D1, // パ Katakana Letter Pa
    0x30D2, // ヒ Katakana Letter Hi
    0x30D3, // ビ Katakana Letter Bi
    0x30D4, // ピ Katakana Letter Pi
    0x30D5, // フ Katakana Letter Hu
    0x30D6, // ブ Katakana Letter Bu
    0x30D7, // プ Katakana Letter Pu
    0x30D8, // ヘ Katakana Letter He
    0x30D9, // ベ Katakana Letter Be
    0x30DA, // ペ Katakana Letter Pe
    0x30DB, // ホ Katakana Letter Ho
    0x30DC, // ボ Katakana Letter Bo
    0x30DD, // ポ Katakana Letter Po
    0x30DE, // マ Katakana Letter Ma
    0x30DF, // ミ Katakana Letter Mi
    0x30E0, // ム Katakana Letter Mu
    0x30E1, // メ Katakana Letter Me
    0x30E2, // モ Katakana Letter Mo
    0x30E3, // ャ Katakana Letter Small Ya
    0x30E4, // ヤ Katakana Letter Ya
    0x30E5, // ュ Katakana Letter Small Yu
    0x30E6, // ユ Katakana Letter Yu
    0x30E7, // ョ Katakana Letter Small Yo
    0x30E8, // ヨ Katakana Letter Yo
    0x30E9, // ラ Katakana Letter Ra
    0x30EA, // リ Katakana Letter Ri
    0x30EB, // ル Katakana Letter Ru
    0x30EC, // レ Katakana Letter Re
    0x30ED, // ロ Katakana Letter Ro
    0x30EE, // ヮ Katakana Letter Small Wa
    0x30EF, // ワ Katakana Letter Wa
    0x30F0, // ヰ Katakana Letter Wi
    0x30F1, // ヱ Katakana Letter We
    0x30F2, // ヲ Katakana Letter Wo
    0x30F3, // ン Katakana Letter N
    0x30F4, // ヴ Katakana Letter Vu
    0x30F5, // ヵ Katakana Letter Small Ka
    0x30F6, // ヶ Katakana Letter Small Ke
    0x30F7, // ヷ Katakana Letter Va
    0x30F8, // ヸ Katakana Letter Vi
    0x30F9, // ヹ Katakana Letter Ve
    0x30FA, // ヺ Katakana Letter Vo
    0x30FB, // ・ Katakana Middle Dot
    0x30FC, // ー Katakana-Hiragana Prolonged Sound Mark
    0x30FD, // ヽ Katakana Iteration Mark
    0x30FE, // ヾ Katakana Voiced Iteration Mark
    0x30FF, // ヿ Katakana Digraph Koto
  };

  return CODEPOINTS;
}


// Standard size is: 16x24
const std::vector<int> &PageBit7Extended2() {
  static const std::vector<int> CODEPOINTS = {
    // Unicode superscripts and subscripts. Note that
    // there are some codepoints unassigned, and some
    // of these symbols appear in other blocks.
    0x2070, // (⁰) Superscript Zero
    0x2071, // (ⁱ) Superscript Latin Small Letter I
    0x2074, // (⁴) Superscript Four
    0x2075, // (⁵) Superscript Five
    0x2076, // (⁶) Superscript Six
    0x2077, // (⁷) Superscript Seven
    0x2078, // (⁸) Superscript Eight
    0x2079, // (⁹) Superscript Nine
    0x207A, // (⁺) Superscript Plus Sign
    0x207B, // (⁻) Superscript Minus
    0x207C, // (⁼) Superscript Equals Sign
    0x207D, // (⁽) Superscript Left Parenthesis
    0x207E, // (⁾) Superscript Right Parenthesis
    0x207F, // (ⁿ) Superscript Latin Small Letter N
    0x2080, // (₀) Subscript Zero
    0x2081, // (₁) Subscript One

    0x2082, // (₂) Subscript Two
    0x2083, // (₃) Subscript Three
    0x2084, // (₄) Subscript Four
    0x2085, // (₅) Subscript Five
    0x2086, // (₆) Subscript Six
    0x2087, // (₇) Subscript Seven
    0x2088, // (₈) Subscript Eight
    0x2089, // (₉) Subscript Nine
    0x208A, // (₊) Subscript Plus Sign
    0x208B, // (₋) Subscript Minus
    0x208C, // (₌) Subscript Equals Sign
    0x208D, // (₍) Subscript Left Parenthesis
    0x208E, // (₎) Subscript Right Parenthesis
    0x2090, // (ₐ) Latin Subscript Small Letter A
    0x2091, // (ₑ) Latin Subscript Small Letter E
    0x2092, // (ₒ) Latin Subscript Small Letter O

    0x2093, // (ₓ) Latin Subscript Small Letter X
    0x2094, // (ₔ) Latin Subscript Small Letter Schwa
    0x2095, // (ₕ) Latin Subscript Small Letter H
    0x2096, // (ₖ) Latin Subscript Small Letter K
    0x2097, // (ₗ) Latin Subscript Small Letter L
    0x2098, // (ₘ) Latin Subscript Small Letter M
    0x2099, // (ₙ) Latin Subscript Small Letter N
    0x209A, // (ₚ) Latin Subscript Small Letter P
    0x209B, // (ₛ) Latin Subscript Small Letter S
    0x209C, // (ₜ) Latin Subscript Small Letter T

    // Unused
    -1, -1, -1, -1, -1, -1,

    // Spacing Modifier Letters
    0x02B0, // (ʰ) Modifier Letter Small H
    0x02B1, // (ʱ) Modifier Letter Small H with hook
    0x02B2, // (ʲ) Modifier Letter Small J
    0x02B3, // (ʳ) Modifier Letter Small R
    0x02B4, // (ʴ) Modifier Letter Small Turned R
    0x02B5, // (ʵ) Modifier Letter Small Turned R with hook
    0x02B6, // (ʶ) Modifier Letter Small Capital Inverted R
    0x02B7, // (ʷ) Modifier Letter Small W
    0x02B8, // (ʸ) Modifier Letter Small Y
    0x02B9, // (ʹ) Modifier Letter Prime
    0x02BA, // (ʺ) Modifier Letter Double Prime
    0x02BB, // (ʻ) Modifier Letter Turned Comma
    0x02BC, // (ʼ) Modifier Letter Apostrophe
    0x02BD, // (ʽ) Modifier Letter Reversed Comma
    0x02BE, // (ʾ) Modifier Letter Right Half Ring
    0x02BF, // (ʿ) Modifier Letter Left Half Ring

    0x02C0, // (ˀ) Modifier Letter Glottal Stop
    0x02C1, // (ˁ) Modifier Letter Reversed Glottal Stop
    0x02C2, // (˂) Modifier Letter Left Arrowhead
    0x02C3, // (˃) Modifier Letter Right Arrowhead
    0x02C4, // (˄) Modifier Letter Up Arrowhead
    0x02C5, // (˅) Modifier Letter Down Arrowhead
    0x02C6, // (ˆ) Modifier Letter Circumflex Accent
    0x02C7, // (ˇ) Caron
    0x02C8, // (ˈ) Modifier Letter Vertical Line
    0x02C9, // (ˉ) Modifier Letter Macron
    0x02CA, // (ˊ) Modifier Letter Acute Accent
    0x02CB, // (ˋ) Modifier Letter Grave Accent
    0x02CC, // (ˌ) Modifier Letter Low Vertical Line
    0x02CD, // (ˍ) Modifier Letter Low Macron
    0x02CE, // (ˎ) Modifier Letter Low Grave Accent
    0x02CF, // (ˏ) Modifier Letter Low Acute Accent

    0x02D0, // (ː) Modifier Letter Triangular Colon
    0x02D1, // (ˑ) Modifier Letter Half Triangular Colon
    0x02D2, // (˒) Modifier Letter Centered Right Half Ring
    0x02D3, // (˓) Modifier Letter Centered Left Half Ring
    0x02D4, // (˔) Modifier Letter Up Tack
    0x02D5, // (˕) Modifier Letter Down Tack
    0x02D6, // (˖) Modifier Letter Plus Sign
    0x02D7, // (˗) Modifier Letter Minus Sign
    0x02D8, // (˘ &#728) Breve
    0x02D9, // (˙ &#729) Dot Above
    0x02DA, // (˚ &#730) Ring Above
    0x02DB, // (˛ &#731) Ogonek
    0x02DC, // (˜) Small Tilde
    0x02DD, // (˝) Double Acute Accent
    0x02DE, // (˞) Modifier Letter Rhotic Hook
    0x02DF, // (˟) Modifier Letter Cross Accent

    0x02E0, // (ˠ) Modifier Letter Small Gamma
    0x02E1, // (ˡ) Modifier Letter Small L
    0x02E2, // (ˢ) Modifier Letter Small S
    0x02E3, // (ˣ) Modifier Letter Small X
    0x02E4, // (ˤ) Modifier Letter Small Reversed Glottal Stop
    0x02E5, // (˥) Modifier Letter Extra-High Tone Bar
    0x02E6, // (˦) Modifier Letter High Tone Bar
    0x02E7, // (˧) Modifier Letter Mid Tone Bar
    0x02E8, // (˨) Modifier Letter Low Tone Bar
    0x02E9, // (˩) Modifier Letter Extra-Low Tone Bar
    0x02EA, // (˪) Extended Bopomofo Yin Departing
    0x02EB, // (˫) Extended Bopomofo Yang Departing
    0x02EC, // (ˬ) Modifier Letter Voicing
    0x02ED, // (˭) Modifier Letter Unaspirated
    0x02EE, // (ˮ) Modifier Letter Double Apostrophe
    0x02EF, // (˯) Modifier Letter Low Down Arrowhead

    0x02F0, // (˰) Modifier Letter Low Up Arrowhead
    0x02F1, // (˱) Modifier Letter Low Left Arrowhead
    0x02F2, // (˲) Modifier Letter Low Right Arrowhead
    0x02F3, // (˳) Modifier Letter Low Ring
    0x02F4, // (˴) Modifier Letter Middle Grave Accent
    0x02F5, // (˵) Modifier Letter Middle Double Grave Accent
    0x02F6, // (˴) Modifier Letter Middle Double Acute Accent
    0x02F7, // (˷) Modifier Letter Low Tilde
    0x02F8, // (˸) Modifier Letter Raised Colon
    0x02F9, // (˹) Modifier Letter Begin High Tone
    0x02FA, // (˺) Modifier Letter End High Tone
    0x02FB, // (˻) Modifier Letter Begin Low Tone
    0x02FC, // (˼) Modifier Letter End Low Tone
    0x02FD, // (˽) Modifier Letter Shelf
    0x02FE, // (˾) Modifier Letter Open Shelf
    0x02FF, // (˿) Modifier Letter Low Left Arrow

    // Free to a good home

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

  };

  CHECK(CODEPOINTS.size() == 16 * 24) << CODEPOINTS.size();
  return CODEPOINTS;
};

const std::vector<int> &PageBit7Cyrillic() {
  static const std::vector<int> CODEPOINTS = {
    // Cyrillic, exactly as Unicode U+0400 to U+04FF.
    0x0400, // (Ѐ) Cyrillic Capital Letter Ie with Grave
    0x0401, // (Ё) Cyrillic Capital Letter Io
    0x0402, // (Ђ) Cyrillic Capital Letter Dje
    0x0403, // (Ѓ) Cyrillic Capital Letter Gje
    0x0404, // (Є) Cyrillic Capital Letter Ukrainian Ie
    0x0405, // (Ѕ) Cyrillic Capital Letter Dze
    0x0406, // (І) Cyrillic Capital Letter Byelorussian-Ukrainian I
    0x0407, // (Ї) Cyrillic Capital Letter Yi
    0x0408, // (Ј) Cyrillic Capital Letter Je
    0x0409, // (Љ) Cyrillic Capital Letter Lje
    0x040A, // (Њ) Cyrillic Capital Letter Nje
    0x040B, // (Ћ) Cyrillic Capital Letter Tshe
    0x040C, // (Ќ) Cyrillic Capital Letter Kje
    0x040D, // (Ѝ) Cyrillic Capital Letter I with Grave
    0x040E, // (Ў) Cyrillic Capital Letter Short U
    0x040F, // (Џ) Cyrillic Capital Letter Dzhe
    0x0410, // (А) Cyrillic Capital Letter A
    0x0411, // (Б) Cyrillic Capital Letter Be
    0x0412, // (В) Cyrillic Capital Letter Ve
    0x0413, // (Г) Cyrillic Capital Letter Ghe
    0x0414, // (Д) Cyrillic Capital Letter De
    0x0415, // (Е) Cyrillic Capital Letter Ie
    0x0416, // (Ж) Cyrillic Capital Letter Zhe
    0x0417, // (З) Cyrillic Capital Letter Ze
    0x0418, // (И) Cyrillic Capital Letter I
    0x0419, // (Й) Cyrillic Capital Letter Short I
    0x041A, // (К) Cyrillic Capital Letter Ka
    0x041B, // (Л) Cyrillic Capital Letter El
    0x041C, // (М) Cyrillic Capital Letter Em
    0x041D, // (Н) Cyrillic Capital Letter En
    0x041E, // (О) Cyrillic Capital Letter O
    0x041F, // (П) Cyrillic Capital Letter Pe
    0x0420, // (Р) Cyrillic Capital Letter Er
    0x0421, // (С) Cyrillic Capital Letter Es
    0x0422, // (Т) Cyrillic Capital Letter Te
    0x0423, // (У) Cyrillic Capital Letter U
    0x0424, // (Ф) Cyrillic Capital Letter Ef
    0x0425, // (Х) Cyrillic Capital Letter Ha
    0x0426, // (Ц) Cyrillic Capital Letter Tse
    0x0427, // (Ч) Cyrillic Capital Letter Che
    0x0428, // (Ш) Cyrillic Capital Letter Sha
    0x0429, // (Щ) Cyrillic Capital Letter Shcha
    0x042A, // (Ъ) Cyrillic Capital Letter Hard Sign
    0x042B, // (Ы) Cyrillic Capital Letter Yeru
    0x042C, // (Ь) Cyrillic Capital Letter Soft Sign
    0x042D, // (Э) Cyrillic Capital Letter E
    0x042E, // (Ю) Cyrillic Capital Letter Yu
    0x042F, // (Я) Cyrillic Capital Letter Ya
    0x0430, // (а) Cyrillic Small Letter A
    0x0431, // (б) Cyrillic Small Letter Be
    0x0432, // (в) Cyrillic Small Letter Ve
    0x0433, // (г) Cyrillic Small Letter Ghe
    0x0434, // (д) Cyrillic Small Letter De
    0x0435, // (е) Cyrillic Small Letter Ie
    0x0436, // (ж) Cyrillic Small Letter Zhe
    0x0437, // (з) Cyrillic Small Letter Ze
    0x0438, // (и) Cyrillic Small Letter I
    0x0439, // (й) Cyrillic Small Letter Short I
    0x043A, // (к) Cyrillic Small Letter Ka
    0x043B, // (л) Cyrillic Small Letter El
    0x043C, // (м) Cyrillic Small Letter Em
    0x043D, // (н) Cyrillic Small Letter En
    0x043E, // (о) Cyrillic Small Letter O
    0x043F, // (п) Cyrillic Small Letter Pe
    0x0440, // (р) Cyrillic Small Letter Er
    0x0441, // (с) Cyrillic Small Letter Es
    0x0442, // (т) Cyrillic Small Letter Te
    0x0443, // (у) Cyrillic Small Letter U
    0x0444, // (ф) Cyrillic Small Letter Ef
    0x0445, // (х) Cyrillic Small Letter Ha
    0x0446, // (ц) Cyrillic Small Letter Tse
    0x0447, // (ч) Cyrillic Small Letter Che
    0x0448, // (ш) Cyrillic Small Letter Sha
    0x0449, // (щ) Cyrillic Small Letter Shcha
    0x044A, // (ъ) Cyrillic Small Letter Hard Sign
    0x044B, // (ы) Cyrillic Small Letter Yeru
    0x044C, // (ь) Cyrillic Small Letter Soft Sign
    0x044D, // (э) Cyrillic Small Letter E
    0x044E, // (ю) Cyrillic Small Letter Yu
    0x044F, // (я) Cyrillic Small Letter Ya
    0x0450, // (ѐ) Cyrillic Small Letter Ie with Grave
    0x0451, // (ё) Cyrillic Small Letter Io
    0x0452, // (ђ) Cyrillic Small Letter Dje
    0x0453, // (ѓ) Cyrillic Small Letter Gje
    0x0454, // (є) Cyrillic Small Letter Ukrainian Ie
    0x0455, // (ѕ) Cyrillic Small Letter Dze
    0x0456, // (і) Cyrillic Small Letter Byelorussian-Ukrainian I
    0x0457, // (ї) Cyrillic Small Letter Yi
    0x0458, // (ј) Cyrillic Small Letter Je
    0x0459, // (љ) Cyrillic Small Letter Lje
    0x045A, // (њ) Cyrillic Small Letter Nje
    0x045B, // (ћ) Cyrillic Small Letter Tshe
    0x045C, // (ќ) Cyrillic Small Letter Kje
    0x045D, // (ѝ) Cyrillic Small Letter I with Grave
    0x045E, // (ў) Cyrillic Small Letter Short U
    0x045F, // (џ) Cyrillic Small Letter Dzhe
    0x0460, // (Ѡ) Cyrillic Capital Letter Omega
    0x0461, // (ѡ) Cyrillic Small Letter Omega
    0x0462, // (Ѣ) Cyrillic Capital Letter Yat
    0x0463, // (ѣ) Cyrillic Small Letter Yat
    0x0464, // (Ѥ) Cyrillic Capital Letter Iotified E
    0x0465, // (ѥ) Cyrillic Small Letter Iotified E
    0x0466, // (Ѧ) Cyrillic Capital Letter Little Yus
    0x0467, // (ѧ) Cyrillic Small Letter Little Yus
    0x0468, // (Ѩ) Cyrillic Capital Letter Iotified Little Yus
    0x0469, // (ѩ) Cyrillic Small Letter Iotified Little Yus
    0x046A, // (Ѫ) Cyrillic Capital Letter Big Yus
    0x046B, // (ѫ) Cyrillic Small Letter Big Yus
    0x046C, // (Ѭ) Cyrillic Capital Letter Iotified Big Yus
    0x046D, // (ѭ) Cyrillic Small Letter Iotified Big Yus
    0x046E, // (Ѯ) Cyrillic Capital Letter Ksi
    0x046F, // (ѯ) Cyrillic Small Letter Ksi
    0x0470, // (Ѱ) Cyrillic Capital Letter Psi
    0x0471, // (ѱ) Cyrillic Small Letter Psi
    0x0472, // (Ѳ) Cyrillic Capital Letter Fita
    0x0473, // (ѳ) Cyrillic Small Letter Fita
    0x0474, // (Ѵ) Cyrillic Capital Letter Izhitsa
    0x0475, // (ѵ) Cyrillic Small Letter Izhitsa
    0x0476, // (Ѷ) Cyrillic Capital Letter Izhitsa with Double Grave Accent
    0x0477, // (ѷ) Cyrillic Small Letter Izhitsa with Double Grave Accent
    0x0478, // (Ѹ) Cyrillic Capital Letter Uk
    0x0479, // (ѹ) Cyrillic Small Letter Uk
    0x047A, // (Ѻ) Cyrillic Capital Letter Round Omega
    0x047B, // (ѻ) Cyrillic Small Letter Round Omega
    0x047C, // (Ѽ) Cyrillic Capital Letter Omega with Titlo
    0x047D, // (ѽ) Cyrillic Small Letter Omega with Titlo
    0x047E, // (Ѿ) Cyrillic Capital Letter Ot
    0x047F, // (ѿ) Cyrillic Small Letter Ot
    0x0480, // (Ҁ) Cyrillic Capital Letter Koppa
    0x0481, // (ҁ) Cyrillic Small Letter Koppa
    0x0482, // (҂) Cyrillic Thousands Sign
    0x0483, // (◌҃) Combining Cyrillic Titlo
    0x0484, // (◌҄) Combining Cyrillic Palatalization
    0x0485, // (◌҅) Combining Cyrillic Dasia Pneumata
    0x0486, // (◌҆) Combining Cyrillic Psili Pneumata
    0x0487, // (◌҇) Combining Cyrillic Pokrytie
    0x0488, // (҈) Combining Cyrillic Hundred Thousands Sign
    0x0489, // (҉) Combining Cyrillic Millions Sign
    0x048A, // (Ҋ) Cyrillic Capital Letter Short I with Tail
    0x048B, // (ҋ) Cyrillic Small Letter Short I with Tail
    0x048C, // (Ҍ) Cyrillic Capital Letter Semisoft Sign
    0x048D, // (ҍ) Cyrillic Small Letter Semisoft Sign
    0x048E, // (Ҏ) Cyrillic Capital Letter Er with Tick
    0x048F, // (ҏ) Cyrillic Small Letter Er with Tick
    0x0490, // (Ґ) Cyrillic Capital Letter Ghe with Upturn
    0x0491, // (ґ) Cyrillic Small Letter Ghe with Upturn
    0x0492, // (Ғ) Cyrillic Capital Letter Ghe with Stroke
    0x0493, // (ғ) Cyrillic Small Letter Ghe with Stroke
    0x0494, // (Ҕ) Cyrillic Capital Letter Ghe with Middle Hook
    0x0495, // (ҕ) Cyrillic Small Letter Ghe with Middle Hook
    0x0496, // (Җ) Cyrillic Capital Letter Zhe with Descender
    0x0497, // (җ) Cyrillic Small Letter Zhe with Descender
    0x0498, // (Ҙ) Cyrillic Capital Letter Ze with Descender
    0x0499, // (ҙ) Cyrillic Small Letter Ze with Descender
    0x049A, // (Қ) Cyrillic Capital Letter Ka with Descender
    0x049B, // (қ) Cyrillic Small Letter Ka with Descender
    0x049C, // (Ҝ) Cyrillic Capital Letter Ka with Vertical Stroke
    0x049D, // (ҝ) Cyrillic Small Letter Ka with Vertical Stroke
    0x049E, // (Ҟ) Cyrillic Capital Letter Ka with Stroke
    0x049F, // (ҟ) Cyrillic Small Letter Ka with Stroke
    0x04A0, // (Ҡ) Cyrillic Capital Letter Bashkir Ka
    0x04A1, // (ҡ) Cyrillic Small Letter Bashkir Ka
    0x04A2, // (Ң) Cyrillic Capital Letter En with Descender
    0x04A3, // (ң) Cyrillic Small Letter En with Descender
    0x04A4, // (Ҥ) Cyrillic Capital Ligature En Ghe
    0x04A5, // (ҥ) Cyrillic Small Ligature En Ghe
    0x04A6, // (Ҧ) Cyrillic Capital Letter Pe with Middle Hook
    0x04A7, // (ҧ) Cyrillic Small Letter Pe with Middle Hook
    0x04A8, // (Ҩ) Cyrillic Capital Letter Abkhasian Ha
    0x04A9, // (ҩ) Cyrillic Small Letter Abkhasian Ha
    0x04AA, // (Ҫ) Cyrillic Capital Letter Es with Descender
    0x04AB, // (ҫ) Cyrillic Small Letter Es with Descender
    0x04AC, // (Ҭ) Cyrillic Capital Letter Te with Descender
    0x04AD, // (ҭ) Cyrillic Small Letter Te with Descender
    0x04AE, // (Ү) Cyrillic Capital Letter Straight U
    0x04AF, // (ү) Cyrillic Small Letter Straight U
    0x04B0, // (Ұ) Cyrillic Capital Letter Straight U with Stroke
    0x04B1, // (ұ) Cyrillic Small Letter Straight U with Stroke
    0x04B2, // (Ҳ) Cyrillic Capital Letter Ha with Descender
    0x04B3, // (ҳ) Cyrillic Small Letter Ha with Descender
    0x04B4, // (Ҵ) Cyrillic Capital Ligature Te Tse
    0x04B5, // (ҵ) Cyrillic Small Ligature Te Tse
    0x04B6, // (Ҷ) Cyrillic Capital Letter Che with Descender
    0x04B7, // (ҷ) Cyrillic Small Letter Che with Descender
    0x04B8, // (Ҹ) Cyrillic Capital Letter Che with Vertical Stroke
    0x04B9, // (ҹ) Cyrillic Small Letter Che with Vertical Stroke
    0x04BA, // (Һ) Cyrillic Capital Letter Shha
    0x04BB, // (һ) Cyrillic Small Letter Shha
    0x04BC, // (Ҽ) Cyrillic Capital Letter Abkhasian Che
    0x04BD, // (ҽ) Cyrillic Small Letter Abkhasian Che
    0x04BE, // (Ҿ) Cyrillic Capital Letter Abkhasian Che with Descender
    0x04BF, // (ҿ) Cyrillic Small Letter Abkhasian Che with Descender
    0x04C0, // (Ӏ) Cyrillic Letter Palochka
    0x04C1, // (Ӂ) Cyrillic Capital Letter Zhe with Breve
    0x04C2, // (ӂ) Cyrillic Small Letter Zhe with Breve
    0x04C3, // (Ӄ) Cyrillic Capital Letter Ka with Hook
    0x04C4, // (ӄ) Cyrillic Small Letter Ka with Hook
    0x04C5, // (Ӆ) Cyrillic Capital Letter El with Tail
    0x04C6, // (ӆ) Cyrillic Small Letter El with Tail
    0x04C7, // (Ӈ) Cyrillic Capital Letter En with Hook
    0x04C8, // (ӈ) Cyrillic Small Letter En with Hook
    0x04C9, // (Ӊ) Cyrillic Capital Letter En with Tail
    0x04CA, // (ӊ) Cyrillic Small Letter En with Tail
    0x04CB, // (Ӌ) Cyrillic Capital Letter Khakassian Che
    0x04CC, // (ӌ) Cyrillic Small Letter Khakassian Che
    0x04CD, // (Ӎ) Cyrillic Capital Letter Em with Tail
    0x04CE, // (ӎ) Cyrillic Small Letter Em with Tail
    0x04CF, // (ӏ) Cyrillic Small Letter Palochka
    0x04D0, // (Ӑ) Cyrillic Capital Letter A with Breve
    0x04D1, // (ӑ) Cyrillic Small Letter A with Breve
    0x04D2, // (Ӓ) Cyrillic Capital Letter A with Diaeresis
    0x04D3, // (ӓ) Cyrillic Small Letter A with Diaeresis
    0x04D4, // (Ӕ) Cyrillic Capital Ligature A Ie
    0x04D5, // (ӕ) Cyrillic Small Ligature A Ie
    0x04D6, // (Ӗ) Cyrillic Capital Letter Ie with Breve
    0x04D7, // (ӗ) Cyrillic Small Letter Ie with Breve
    0x04D8, // (Ә) Cyrillic Capital Letter Schwa
    0x04D9, // (ә) Cyrillic Small Letter Schwa
    0x04DA, // (Ӛ) Cyrillic Capital Letter Schwa with Diaeresis
    0x04DB, // (ӛ) Cyrillic Small Letter Schwa with Diaeresis
    0x04DC, // (Ӝ) Cyrillic Capital Letter Zhe with Diaeresis
    0x04DD, // (ӝ) Cyrillic Small Letter Zhe with Diaeresis
    0x04DE, // (Ӟ) Cyrillic Capital Letter Ze with Diaeresis
    0x04DF, // (ӟ) Cyrillic Small Letter Ze with Diaeresis
    0x04E0, // (Ӡ) Cyrillic Capital Letter Abkhasian Dze
    0x04E1, // (ӡ) Cyrillic Small Letter Abkhasian Dze
    0x04E2, // (Ӣ) Cyrillic Capital Letter I with Macron
    0x04E3, // (ӣ) Cyrillic Small Letter I with Macron
    0x04E4, // (Ӥ) Cyrillic Capital Letter I with Diaeresis
    0x04E5, // (ӥ) Cyrillic Small Letter I with Diaeresis
    0x04E6, // (Ӧ) Cyrillic Capital Letter O with Diaeresis
    0x04E7, // (ӧ) Cyrillic Small Letter O with Diaeresis
    0x04E8, // (Ө) Cyrillic Capital Letter Barred O
    0x04E9, // (ө) Cyrillic Small Letter Barred O
    0x04EA, // (Ӫ) Cyrillic Capital Letter Barred O with Diaeresis
    0x04EB, // (ӫ) Cyrillic Small Letter Barred O with Diaeresis
    0x04EC, // (Ӭ) Cyrillic Capital Letter E with Diaeresis
    0x04ED, // (ӭ) Cyrillic Small Letter E with Diaeresis
    0x04EE, // (Ӯ) Cyrillic Capital Letter U with Macron
    0x04EF, // (ӯ) Cyrillic Small Letter U with Macron
    0x04F0, // (Ӱ) Cyrillic Capital Letter U with Diaeresis
    0x04F1, // (ӱ) Cyrillic Small Letter U with Diaeresis
    0x04F2, // (Ӳ) Cyrillic Capital Letter U with Double Acute
    0x04F3, // (ӳ) Cyrillic Small Letter U with Double Acute
    0x04F4, // (Ӵ) Cyrillic Capital Letter Che with Diaeresis
    0x04F5, // (ӵ) Cyrillic Small Letter Che with Diaeresis
    0x04F6, // (Ӷ) Cyrillic Capital Letter Ghe with Descender
    0x04F7, // (ӷ) Cyrillic Small Letter Ghe with Descender
    0x04F8, // (Ӹ) Cyrillic Capital Letter Yeru with Diaeresis
    0x04F9, // (ӹ) Cyrillic Small Letter Yeru with Diaeresis
    0x04FA, // (Ӻ) Cyrillic Capital Letter Ghe with Stroke and Hook
    0x04FB, // (ӻ) Cyrillic Small Letter Ghe with Stroke and Hook
    0x04FC, // (Ӽ) Cyrillic Capital Letter Ha with Hook
    0x04FD, // (ӽ) Cyrillic Small Letter Ha with Hook
    0x04FE, // (Ӿ) Cyrillic Capital Letter Ha with Stroke
    0x04FF, // (ӿ) Cyrillic Small Letter Ha with Stroke

    // "Cyrillic Supplement" exactly as unicode, U+0500 - U+052F
    0x0500, // (Ԁ) CYRILLIC CAPITAL LETTER KOMI DE
    0x0501, // (ԁ) CYRILLIC SMALL LETTER KOMI DE
    0x0502, // (Ԃ) CYRILLIC CAPITAL LETTER KOMI DJE
    0x0503, // (ԃ) CYRILLIC SMALL LETTER KOMI DJE
    0x0504, // (Ԅ) CYRILLIC CAPITAL LETTER KOMI ZJE
    0x0505, // (ԅ) CYRILLIC SMALL LETTER KOMI ZJE
    0x0506, // (Ԇ) CYRILLIC CAPITAL LETTER KOMI DZJE
    0x0507, // (ԇ) CYRILLIC SMALL LETTER KOMI DZJE
    0x0508, // (Ԉ) CYRILLIC CAPITAL LETTER KOMI LJE
    0x0509, // (ԉ) CYRILLIC SMALL LETTER KOMI LJE
    0x050A, // (Ԋ) CYRILLIC CAPITAL LETTER KOMI NJE
    0x050B, // (ԋ) CYRILLIC SMALL LETTER KOMI NJE
    0x050C, // (Ԍ) CYRILLIC CAPITAL LETTER KOMI SJE
    0x050D, // (ԍ) CYRILLIC SMALL LETTER KOMI SJE
    0x050E, // (Ԏ) CYRILLIC CAPITAL LETTER KOMI TJE
    0x050F, // (ԏ) CYRILLIC SMALL LETTER KOMI TJE
    // Khanty letters
    0x0510, // (Ԑ) CYRILLIC CAPITAL LETTER REVERSED ZE
    0x0511, // (ԑ) CYRILLIC SMALL LETTER REVERSED ZE
    // Chukchi letters
    0x0512, // (Ԓ) CYRILLIC CAPITAL LETTER EL WITH HOOK
    0x0513, // (ԓ) CYRILLIC SMALL LETTER EL WITH HOOK
    // Mordvin letters
    0x0514, // (Ԕ) CYRILLIC CAPITAL LETTER LHA
    0x0515, // (ԕ) CYRILLIC SMALL LETTER LHA
    0x0516, // (Ԗ) CYRILLIC CAPITAL LETTER RHA
    0x0517, // (ԗ) CYRILLIC SMALL LETTER RHA
    0x0518, // (Ԙ) CYRILLIC CAPITAL LETTER YAE
    0x0519, // (ԙ) CYRILLIC SMALL LETTER YAE
    // Kurdish letters
    0x051A, // (Ԛ) CYRILLIC CAPITAL LETTER QA
    0x051B, // (ԛ) CYRILLIC SMALL LETTER QA
    0x051C, // (Ԝ) CYRILLIC CAPITAL LETTER WE
    0x051D, // (ԝ) CYRILLIC SMALL LETTER WE
    // Aleut letters
    0x051E, // (Ԟ) CYRILLIC CAPITAL LETTER ALEUT KA
    0x051F, // (ԟ) CYRILLIC SMALL LETTER ALEUT KA
    // Chuvash letters
    0x0520, // (Ԡ) CYRILLIC CAPITAL LETTER EL WITH MIDDLE
    0x0521, // (ԡ) CYRILLIC SMALL LETTER EL WITH MIDDLE
    0x0522, // (Ԣ) CYRILLIC CAPITAL LETTER EN WITH MIDDLE
    0x0523, // (ԣ) CYRILLIC SMALL LETTER EN WITH MIDDLE

    // Abkhaz letters
    0x0524, // (Ԥ) CYRILLIC CAPITAL LETTER PE WITH
    0x0525, // (ԥ) CYRILLIC SMALL LETTER PE WITH DESCENDER
    // Azerbaijani letters
    0x0526, // (Ԧ) CYRILLIC CAPITAL LETTER SHHA WITH
    0x0527, // (ԧ) CYRILLIC SMALL LETTER SHHA WITH
    // Orok letters
    0x0528, // (Ԩ) CYRILLIC CAPITAL LETTER EN WITH LEFT HOOK
    0x0529, // (ԩ) CYRILLIC SMALL LETTER EN WITH LEFT HOOK
    // Komi letters
    0x052A, // (Ԫ) CYRILLIC CAPITAL LETTER DZZHE
    0x052B, // (ԫ) CYRILLIC SMALL LETTER DZZHE
    0x052C, // (Ԭ) CYRILLIC CAPITAL LETTER DCHE
    0x052D, // (ԭ) CYRILLIC SMALL LETTER DCHE
    // Khanty letters
    0x052E, // (Ԯ) CYRILLIC CAPITAL LETTER EL WITH DESCENDER
    0x052F, // (ԯ) CYRILLIC SMALL LETTER EL WITH DESCENDER

    // 80 unclaimed glyphs
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  };

  return CODEPOINTS;
}

const std::vector<int> &PageBit7Math() {
  static const std::vector<int> CODEPOINTS = {
    0x2200,  // (∀) FOR ALL
    0x2201,  // (∁) COMPLEMENT
    0x2202,  // (∂) PARTIAL DIFFERENTIAL
    0x2203,  // (∃) THERE EXISTS
    0x2204,  // (∄) THERE DOES NOT EXIST
    0x2205,  // (∅) EMPTY SET
    0x2206,  // (∆) INCREMENT
    0x2207,  // (∇) NABLA
    0x2208,  // (∈) ELEMENT OF
    0x2209,  // (∉) NOT AN ELEMENT OF
    0x220a,  // (∊) SMALL ELEMENT OF
    0x220b,  // (∋) CONTAINS AS MEMBER
    0x220c,  // (∌) DOES NOT CONTAIN AS MEMBER
    0x220d,  // (∍) SMALL CONTAINS AS MEMBER
    0x220e,  // (∎) END OF PROOF
    0x220f,  // (∏) N-ARY PRODUCT
    0x2210,  // (∐) N-ARY COPRODUCT
    0x2211,  // (∑) N-ARY SUMMATION
    0x2212,  // (−) MINUS SIGN
    0x2213,  // (∓) MINUS-OR-PLUS SIGN
    0x2214,  // (∔) DOT PLUS
    0x2215,  // (∕) DIVISION SLASH
    0x2216,  // (∖) SET MINUS
    0x2217,  // (∗) ASTERISK OPERATOR
    0x2218,  // (∘) RING OPERATOR
    0x2219,  // (∙) BULLET OPERATOR
    0x221a,  // (√) SQUARE ROOT
    0x221b,  // (∛) CUBE ROOT
    0x221c,  // (∜) FOURTH ROOT
    0x221d,  // (∝) PROPORTIONAL TO
    0x221e,  // (∞) INFINITY
    0x221f,  // (∟) RIGHT ANGLE
    0x2220,  // (∠) ANGLE
    0x2221,  // (∡) MEASURED ANGLE
    0x2222,  // (∢) SPHERICAL ANGLE
    0x2223,  // (∣) DIVIDES
    0x2224,  // (∤) DOES NOT DIVIDE
    0x2225,  // (∥) PARALLEL TO
    0x2226,  // (∦) NOT PARALLEL TO
    0x2227,  // (∧) LOGICAL AND
    0x2228,  // (∨) LOGICAL OR
    0x2229,  // (∩) INTERSECTION
    0x222a,  // (∪) UNION
    0x222b,  // (∫) INTEGRAL
    0x222c,  // (∬) DOUBLE INTEGRAL
    0x222d,  // (∭) TRIPLE INTEGRAL
    0x222e,  // (∮) CONTOUR INTEGRAL
    0x222f,  // (∯) SURFACE INTEGRAL
    0x2230,  // (∰) VOLUME INTEGRAL
    0x2231,  // (∱) CLOCKWISE INTEGRAL
    0x2232,  // (∲) CLOCKWISE CONTOUR INTEGRAL
    0x2233,  // (∳) ANTICLOCKWISE CONTOUR INTEGRAL
    0x2234,  // (∴) THEREFORE
    0x2235,  // (∵) BECAUSE
    0x2236,  // (∶) RATIO
    0x2237,  // (∷) PROPORTION
    0x2238,  // (∸) DOT MINUS
    0x2239,  // (∹) EXCESS
    0x223a,  // (∺) GEOMETRIC PROPORTION
    0x223b,  // (∻) HOMOTHETIC
    0x223c,  // (∼) TILDE OPERATOR
    0x223d,  // (∽) REVERSED TILDE
    0x223e,  // (∾) INVERTED LAZY S
    0x223f,  // (∿) SINE WAVE
    0x2240,  // (≀) WREATH PRODUCT
    0x2241,  // (≁) NOT TILDE
    0x2242,  // (≂) MINUS TILDE
    0x2243,  // (≃) ASYMPTOTICALLY EQUAL TO
    0x2244,  // (≄) NOT ASYMPTOTICALLY EQUAL TO
    0x2245,  // (≅) APPROXIMATELY EQUAL TO
    0x2246,  // (≆) APPROXIMATELY BUT NOT ACTUALLY EQUAL TO
    0x2247,  // (≇) NEITHER APPROXIMATELY NOR ACTUALLY EQUAL TO
    0x2248,  // (≈) ALMOST EQUAL TO
    0x2249,  // (≉) NOT ALMOST EQUAL TO
    0x224a,  // (≊) ALMOST EQUAL OR EQUAL TO
    0x224b,  // (≋) TRIPLE TILDE
    0x224c,  // (≌) ALL EQUAL TO
    0x224d,  // (≍) EQUIVALENT TO
    0x224e,  // (≎) GEOMETRICALLY EQUIVALENT TO
    0x224f,  // (≏) DIFFERENCE BETWEEN
    0x2250,  // (≐) APPROACHES THE LIMIT
    0x2251,  // (≑) GEOMETRICALLY EQUAL TO
    0x2252,  // (≒) APPROXIMATELY EQUAL TO OR THE IMAGE OF
    0x2253,  // (≓) IMAGE OF OR APPROXIMATELY EQUAL TO
    0x2254,  // (≔) COLON EQUALS
    0x2255,  // (≕) EQUALS COLON
    0x2256,  // (≖) RING IN EQUAL TO
    0x2257,  // (≗) RING EQUAL TO
    0x2258,  // (≘) CORRESPONDS TO
    0x2259,  // (≙) ESTIMATES
    0x225a,  // (≚) EQUIANGULAR TO
    0x225b,  // (≛) STAR EQUALS
    0x225c,  // (≜) DELTA EQUAL TO
    0x225d,  // (≝) EQUAL TO BY DEFINITION
    0x225e,  // (≞) MEASURED BY
    0x225f,  // (≟) QUESTIONED EQUAL TO
    0x2260,  // (≠) NOT EQUAL TO
    0x2261,  // (≡) IDENTICAL TO
    0x2262,  // (≢) NOT IDENTICAL TO
    0x2263,  // (≣) STRICTLY EQUIVALENT TO
    0x2264,  // (≤) LESS-THAN OR EQUAL TO
    0x2265,  // (≥) GREATER-THAN OR EQUAL TO
    0x2266,  // (≦) LESS-THAN OVER EQUAL TO
    0x2267,  // (≧) GREATER-THAN OVER EQUAL TO
    0x2268,  // (≨) LESS-THAN BUT NOT EQUAL TO
    0x2269,  // (≩) GREATER-THAN BUT NOT EQUAL TO
    0x226a,  // (≪) MUCH LESS-THAN
    0x226b,  // (≫) MUCH GREATER-THAN
    0x226c,  // (≬) BETWEEN
    0x226d,  // (≭) NOT EQUIVALENT TO
    0x226e,  // (≮) NOT LESS-THAN
    0x226f,  // (≯) NOT GREATER-THAN
    0x2270,  // (≰) NEITHER LESS-THAN NOR EQUAL TO
    0x2271,  // (≱) NEITHER GREATER-THAN NOR EQUAL TO
    0x2272,  // (≲) LESS-THAN OR EQUIVALENT TO
    0x2273,  // (≳) GREATER-THAN OR EQUIVALENT TO
    0x2274,  // (≴) NEITHER LESS-THAN NOR EQUIVALENT TO
    0x2275,  // (≵) NEITHER GREATER-THAN NOR EQUIVALENT TO
    0x2276,  // (≶) LESS-THAN OR GREATER-THAN
    0x2277,  // (≷) GREATER-THAN OR LESS-THAN
    0x2278,  // (≸) NEITHER LESS-THAN NOR GREATER-THAN
    0x2279,  // (≹) NEITHER GREATER-THAN NOR LESS-THAN
    0x227a,  // (≺) PRECEDES
    0x227b,  // (≻) SUCCEEDS
    0x227c,  // (≼) PRECEDES OR EQUAL TO
    0x227d,  // (≽) SUCCEEDS OR EQUAL TO
    0x227e,  // (≾) PRECEDES OR EQUIVALENT TO
    0x227f,  // (≿) SUCCEEDS OR EQUIVALENT TO
    0x2280,  // (⊀) DOES NOT PRECEDE
    0x2281,  // (⊁) DOES NOT SUCCEED
    0x2282,  // (⊂) SUBSET OF
    0x2283,  // (⊃) SUPERSET OF
    0x2284,  // (⊄) NOT A SUBSET OF
    0x2285,  // (⊅) NOT A SUPERSET OF
    0x2286,  // (⊆) SUBSET OF OR EQUAL TO
    0x2287,  // (⊇) SUPERSET OF OR EQUAL TO
    0x2288,  // (⊈) NEITHER A SUBSET OF NOR EQUAL TO
    0x2289,  // (⊉) NEITHER A SUPERSET OF NOR EQUAL TO
    0x228a,  // (⊊) SUBSET OF WITH NOT EQUAL TO
    0x228b,  // (⊋) SUPERSET OF WITH NOT EQUAL TO
    0x228c,  // (⊌) MULTISET
    0x228d,  // (⊍) MULTISET MULTIPLICATION
    0x228e,  // (⊎) MULTISET UNION
    0x228f,  // (⊏) SQUARE IMAGE OF
    0x2290,  // (⊐) SQUARE ORIGINAL OF
    0x2291,  // (⊑) SQUARE IMAGE OF OR EQUAL TO
    0x2292,  // (⊒) SQUARE ORIGINAL OF OR EQUAL TO
    0x2293,  // (⊓) SQUARE CAP
    0x2294,  // (⊔) SQUARE CUP
    0x2295,  // (⊕) CIRCLED PLUS
    0x2296,  // (⊖) CIRCLED MINUS
    0x2297,  // (⊗) CIRCLED TIMES
    0x2298,  // (⊘) CIRCLED DIVISION SLASH
    0x2299,  // (⊙) CIRCLED DOT OPERATOR
    0x229a,  // (⊚) CIRCLED RING OPERATOR
    0x229b,  // (⊛) CIRCLED ASTERISK OPERATOR
    0x229c,  // (⊜) CIRCLED EQUALS
    0x229d,  // (⊝) CIRCLED DASH
    0x229e,  // (⊞) SQUARED PLUS
    0x229f,  // (⊟) SQUARED MINUS
    0x22a0,  // (⊠) SQUARED TIMES
    0x22a1,  // (⊡) SQUARED DOT OPERATOR
    0x22a2,  // (⊢) RIGHT TACK
    0x22a3,  // (⊣) LEFT TACK
    0x22a4,  // (⊤) DOWN TACK
    0x22a5,  // (⊥) UP TACK
    0x22a6,  // (⊦) ASSERTION
    0x22a7,  // (⊧) MODELS
    0x22a8,  // (⊨) TRUE
    0x22a9,  // (⊩) FORCES
    0x22aa,  // (⊪) TRIPLE VERTICAL BAR RIGHT TURNSTILE
    0x22ab,  // (⊫) DOUBLE VERTICAL BAR DOUBLE RIGHT TURNSTILE
    0x22ac,  // (⊬) DOES NOT PROVE
    0x22ad,  // (⊭) NOT TRUE
    0x22ae,  // (⊮) DOES NOT FORCE
    0x22af,  // (⊯) NEGATED DOUBLE VERTICAL BAR DOUBLE RIGHT TURNSTILE
    0x22b0,  // (⊰) PRECEDES UNDER RELATION
    0x22b1,  // (⊱) SUCCEEDS UNDER RELATION
    0x22b2,  // (⊲) NORMAL SUBGROUP OF
    0x22b3,  // (⊳) CONTAINS AS NORMAL SUBGROUP
    0x22b4,  // (⊴) NORMAL SUBGROUP OF OR EQUAL TO
    0x22b5,  // (⊵) CONTAINS AS NORMAL SUBGROUP OR EQUAL TO
    0x22b6,  // (⊶) ORIGINAL OF
    0x22b7,  // (⊷) IMAGE OF
    0x22b8,  // (⊸) MULTIMAP
    0x22b9,  // (⊹) HERMITIAN CONJUGATE MATRIX
    0x22ba,  // (⊺) INTERCALATE
    0x22bb,  // (⊻) XOR
    0x22bc,  // (⊼) NAND
    0x22bd,  // (⊽) NOR
    0x22be,  // (⊾) RIGHT ANGLE WITH ARC
    0x22bf,  // (⊿) RIGHT TRIANGLE
    0x22c0,  // (⋀) N-ARY LOGICAL AND
    0x22c1,  // (⋁) N-ARY LOGICAL OR
    0x22c2,  // (⋂) N-ARY INTERSECTION
    0x22c3,  // (⋃) N-ARY UNION
    0x22c4,  // (⋄) DIAMOND OPERATOR
    0x22c5,  // (⋅) DOT OPERATOR
    0x22c6,  // (⋆) STAR OPERATOR
    0x22c7,  // (⋇) DIVISION TIMES
    0x22c8,  // (⋈) BOWTIE
    0x22c9,  // (⋉) LEFT NORMAL FACTOR SEMIDIRECT PRODUCT
    0x22ca,  // (⋊) RIGHT NORMAL FACTOR SEMIDIRECT PRODUCT
    0x22cb,  // (⋋) LEFT SEMIDIRECT PRODUCT
    0x22cc,  // (⋌) RIGHT SEMIDIRECT PRODUCT
    0x22cd,  // (⋍) REVERSED TILDE EQUALS
    0x22ce,  // (⋎) CURLY LOGICAL OR
    0x22cf,  // (⋏) CURLY LOGICAL AND
    0x22d0,  // (⋐) DOUBLE SUBSET
    0x22d1,  // (⋑) DOUBLE SUPERSET
    0x22d2,  // (⋒) DOUBLE INTERSECTION
    0x22d3,  // (⋓) DOUBLE UNION
    0x22d4,  // (⋔) PITCHFORK
    0x22d5,  // (⋕) EQUAL AND PARALLEL TO
    0x22d6,  // (⋖) LESS-THAN WITH DOT
    0x22d7,  // (⋗) GREATER-THAN WITH DOT
    0x22d8,  // (⋘) VERY MUCH LESS-THAN
    0x22d9,  // (⋙) VERY MUCH GREATER-THAN
    0x22da,  // (⋚) LESS-THAN EQUAL TO OR GREATER-THAN
    0x22db,  // (⋛) GREATER-THAN EQUAL TO OR LESS-THAN
    0x22dc,  // (⋜) EQUAL TO OR LESS-THAN
    0x22dd,  // (⋝) EQUAL TO OR GREATER-THAN
    0x22de,  // (⋞) EQUAL TO OR PRECEDES
    0x22df,  // (⋟) EQUAL TO OR SUCCEEDS
    0x22e0,  // (⋠) DOES NOT PRECEDE OR EQUAL
    0x22e1,  // (⋡) DOES NOT SUCCEED OR EQUAL
    0x22e2,  // (⋢) NOT SQUARE IMAGE OF OR EQUAL TO
    0x22e3,  // (⋣) NOT SQUARE ORIGINAL OF OR EQUAL TO
    0x22e4,  // (⋤) SQUARE IMAGE OF OR NOT EQUAL TO
    0x22e5,  // (⋥) SQUARE ORIGINAL OF OR NOT EQUAL TO
    0x22e6,  // (⋦) LESS-THAN BUT NOT EQUIVALENT TO
    0x22e7,  // (⋧) GREATER-THAN BUT NOT EQUIVALENT TO
    0x22e8,  // (⋨) PRECEDES BUT NOT EQUIVALENT TO
    0x22e9,  // (⋩) SUCCEEDS BUT NOT EQUIVALENT TO
    0x22ea,  // (⋪) NOT NORMAL SUBGROUP OF
    0x22eb,  // (⋫) DOES NOT CONTAIN AS NORMAL SUBGROUP
    0x22ec,  // (⋬) NOT NORMAL SUBGROUP OF OR EQUAL TO
    0x22ed,  // (⋭) DOES NOT CONTAIN AS NORMAL SUBGROUP OR EQUAL
    0x22ee,  // (⋮) VERTICAL ELLIPSIS
    0x22ef,  // (⋯) MIDLINE HORIZONTAL ELLIPSIS
    0x22f0,  // (⋰) UP RIGHT DIAGONAL ELLIPSIS
    0x22f1,  // (⋱) DOWN RIGHT DIAGONAL ELLIPSIS
    0x22f2,  // (⋲) ELEMENT OF WITH LONG HORIZONTAL STROKE
    0x22f3,  // (⋳) ELEMENT OF WITH VERTICAL BAR AT END OF HORIZONTAL STROKE
    0x22f4,  // (⋴) SMALL ELEMENT OF WITH VERTICAL BAR AT END OF HORIZONTAL STROKE
    0x22f5,  // (⋵) ELEMENT OF WITH DOT ABOVE
    0x22f6,  // (⋶) ELEMENT OF WITH OVERBAR
    0x22f7,  // (⋷) SMALL ELEMENT OF WITH OVERBAR
    0x22f8,  // (⋸) ELEMENT OF WITH UNDERBAR
    0x22f9,  // (⋹) ELEMENT OF WITH TWO HORIZONTAL STROKES
    0x22fa,  // (⋺) CONTAINS WITH LONG HORIZONTAL STROKE
    0x22fb,  // (⋻) CONTAINS WITH VERTICAL BAR AT END OF HORIZONTAL STROKE
    0x22fc,  // (⋼) SMALL CONTAINS WITH VERTICAL BAR AT END OF HORIZONTAL STROKE
    0x22fd,  // (⋽) CONTAINS WITH OVERBAR
    0x22fe,  // (⋾) SMALL CONTAINS WITH OVERBAR
    0x22ff,  // (⋿) Z NOTATION BAG MEMBERSHIP

    // Letter-like symbols
    // (I think these are not the official unicode names; they come
    // from Wikipedia)
    0x2100,  // (℀) ACCOUNT OF
    0x2101,  // (℁) ADDRESSED TO THE SUBJECT (I.E., CARE OF)
    0x2102,  // (ℂ) DOUBLE-STRUCK CAPITAL C
    0x2103,  // (℃) DEGREE CELSIUS
    0x2104,  // (℄) CENTER LINE SYMBOL
    0x2105,  // (℅) CARE OF
    0x2106,  // (℆) CADA UNA[4]
    0x2107,  // (ℇ) EULER CONSTANT[5]
    0x2108,  // (℈) SCRUPLE
    0x2109,  // (℉) DEGREE FAHRENHEIT
    0x210A,  // (ℊ) SCRIPT SMALL G
    0x210B,  // (ℋ) SCRIPT CAPITAL H
    0x210C,  // (ℌ) BLACK-LETTER CAPITAL H
    0x210D,  // (ℍ) DOUBLE-STRUCK CAPITAL H
    0x210E,  // (ℎ) PLANCK CONSTANT
    0x210F,  // (ℏ) REDUCED PLANCK CONSTANT (PLANCK CONSTANT OVER 2Π)
    0x2110,  // (ℐ) SCRIPT CAPITAL I
    0x2111,  // (ℑ) BLACK-LETTER CAPITAL I
    0x2112,  // (ℒ) SCRIPT CAPITAL L
    0x2113,  // (ℓ) SCRIPT SMALL L (LATEX: \ELL)
    0x2114,  // (℔) L B BAR SYMBOL
    0x2115,  // (ℕ) DOUBLE-STRUCK CAPITAL N
    0x2116,  // (№) NUMERO SIGN
    0x2117,  // (℗) SOUND RECORDING COPYRIGHT SYMBOL
    0x2118,  // (℘) SCRIPT CAPITAL aka WEIERSTRASS ELLIPTIC FUNCTION
    0x2119,  // (ℙ) DOUBLE-STRUCK CAPITAL P
    0x211A,  // (ℚ) DOUBLE-STRUCK CAPITAL Q
    0x211B,  // (ℛ) SCRIPT CAPITAL R
    0x211C,  // (ℜ) BLACK-LETTER CAPITAL R
    0x211D,  // (ℝ) DOUBLE-STRUCK CAPITAL R
    0x211E,  // (℞) PRESCRIPTION TAKE
    0x211F,  // (℟) RESPONSE
    0x2120,  // (℠) SERVICE MARK
    0x2121,  // (℡) TELEPHONE SIGN
    0x2122,  // (™) TRADEMARK SIGN
    0x2123,  // (℣) VERSICLE
    0x2124,  // (ℤ) DOUBLE-STRUCK CAPITAL Z
    0x2125,  // (℥) OUNCE SIGN
    0x2126,  // (Ω) OHM SIGN
    0x2127,  // (℧) INVERTED OHM SIGN
    0x2128,  // (ℨ) BLACK-LETTER CAPITAL Z
    0x2129,  // (℩) TURNED GREEK SMALL LETTER IOTA
    0x212A,  // (K) KELVIN SIGN
    0x212B,  // (Å) ÅNGSTRÖM SIGN
    0x212C,  // (ℬ) SCRIPT CAPITAL B
    0x212D,  // (ℭ) BLACK-LETTER CAPITAL C
    0x212E,  // (℮) ESTIMATED SYMBOL
    0x212F,  // (ℯ) SCRIPT SMALL E
    0x2130,  // (ℰ) SCRIPT CAPITAL E
    0x2131,  // (ℱ) SCRIPT CAPITAL F
    0x2132,  // (Ⅎ) TURNED CAPITAL F
    0x2133,  // (ℳ) SCRIPT CAPITAL M
    0x2134,  // (ℴ) SCRIPT SMALL O
    0x2135,  // (ℵ) ALEF SYMBOL
    0x2136,  // (ℶ) BET SYMBOL
    0x2137,  // (ℷ) GIMEL SYMBOL
    0x2138,  // (ℸ) DALET SYMBOL
    0x2139,  // (ℹ) INFORMATION SOURCE
    0x213A,  // (℺) ROTATED CAPITAL Q
    0x213B,  // (℻) FAX SIGN
    0x213C,  // (ℼ) DOUBLE-STRUCK SMALL PI
    0x213D,  // (ℽ) DOUBLE-STRUCK SMALL GAMMA
    0x213E,  // (ℾ) DOUBLE-STRUCK CAPITAL GAMMA
    0x213F,  // (ℿ) DOUBLE-STRUCK CAPITAL PI
    0x2140,  // (⅀) DOUBLE-STRUCK N-ARY SUMMATION
    0x2141,  // (⅁) TURNED SANS-SERIF CAPITAL G
    0x2142,  // (⅂) TURNED SANS-SERIF CAPITAL L
    0x2143,  // (⅃) REVERSED SANS-SERIF CAPITAL L
    0x2144,  // (⅄) TURNED SANS-SERIF CAPITAL Y
    0x2145,  // (ⅅ) DOUBLE-STRUCK ITALIC CAPITAL D
    0x2146,  // (ⅆ) DOUBLE-STRUCK ITALIC SMALL D
    0x2147,  // (ⅇ) DOUBLE-STRUCK ITALIC SMALL E
    0x2148,  // (ⅈ) DOUBLE-STRUCK ITALIC SMALL I
    0x2149,  // (ⅉ) DOUBLE-STRUCK ITALIC SMALL J
    0x214A,  // (⅊) PROPERTY LINE
    0x214B,  // (⅋) TURNED AMPERSAND
    0x214C,  // (⅌) PER SIGN
    0x214D,  // (⅍) AKTIESELSKAB
    0x214E,  // (ⅎ) TURNED SMALL F
    0x214F,  // (⅏) SYMBOL FOR SAMARITAN


    // Also from wikipedia.
    0x27C0,  // (⟀) THREE DIMENSIONAL ANGLE
    0x27C1,  // (⟁) WHITE TRIANGLE CONTAINING SMALL WHITE TRIANGLE
    0x27C2,  // (⟂) PERPENDICULAR
    0x27C3,  // (⟃) OPEN SUBSET
    0x27C4,  // (⟄) OPEN SUPERSET
    0x27C5,  // (⟅) LEFT S-SHAPED BAG DELIMITER
    0x27C6,  // (⟆) RIGHT S-SHAPED BAG DELIMITER
    0x27C7,  // (⟇) OR WITH DOT INSIDE
    0x27C8,  // (⟈) REVERSE SOLIDUS PRECEDING SUBSET
    0x27C9,  // (⟉) SUPERSET PRECEDING SOLIDUS
    0x27CA,  // (⟊) VERTICAL BAR WITH HORIZONTAL STROKE
    0x27CB,  // (⟋) MATHEMATICAL RISING DIAGONAL
    0x27CC,  // (⟌) LONG DIVISION
    0x27CD,  // (⟍) MATHEMATICAL FALLING DIAGONAL
    0x27CE,  // (⟎) SQUARED LOGICAL AND
    0x27CF,  // (⟏) SQUARED LOGICAL OR
    0x27D0,  // (⟐) WHITE DIAMOND WITH CENTERED DOT
    0x27D1,  // (⟑) AND WITH DOT
    0x27D2,  // (⟒) ELEMENT OF OPENING UPWARD
    0x27D3,  // (⟓) LOWER RIGHT CORNER WITH DOT
    0x27D4,  // (⟔) UPPER LEFT CORNER WITH DOT
    0x27D5,  // (⟕) LEFT OUTER JOIN
    0x27D6,  // (⟖) RIGHT OUTER JOIN
    0x27D7,  // (⟗) FULL OUTER JOIN
    0x27D8,  // (⟘) LARGE UP TACK
    0x27D9,  // (⟙) LARGE DOWN TACK
    0x27DA,  // (⟚) LEFT AND RIGHT DOUBLE TURNSTILE
    0x27DB,  // (⟛) LEFT AND RIGHT TACK
    0x27DC,  // (⟜) LEFT MULTIMAP
    0x27DD,  // (⟝) LONG RIGHT TACK
    0x27DE,  // (⟞) LONG LEFT TACK
    0x27DF,  // (⟟) UP TACK WITH CIRCLE ABOVE
    0x27E0,  // (⟠) LOZENGE DIVIDED BY HORIZONTAL RULE
    0x27E1,  // (⟡) WHITE CONCAVE-SIDED DIAMOND
    0x27E2,  // (⟢) WHITE CONCAVE-SIDED DIAMOND WITH LEFTWARD TICK
    0x27E3,  // (⟣) WHITE CONCAVE-SIDED DIAMOND WITH RIGHTWARD TICK
    0x27E4,  // (⟤) WHITE SQUARE WITH LEFTWARD TICK
    0x27E5,  // (⟥) WHITE SQUARE WITH RIGHTWARD TICK
    0x27E6,  // (⟦) MATHEMATICAL LEFT WHITE SQUARE BRACKET
    0x27E7,  // (⟧) MATHEMATICAL RIGHT WHITE SQUARE BRACKET
    0x27E8,  // (⟨) MATHEMATICAL LEFT ANGLE BRACKET
    0x27E9,  // (⟩) MATHEMATICAL RIGHT ANGLE BRACKET
    0x27EA,  // (⟪) MATHEMATICAL LEFT DOUBLE ANGLE BRACKET
    0x27EB,  // (⟫) MATHEMATICAL RIGHT DOUBLE ANGLE BRACKET
    0x27EC,  // (⟬) MATHEMATICAL LEFT WHITE TORTOISE SHELL BRACKET
    0x27ED,  // (⟭) MATHEMATICAL RIGHT WHITE TORTOISE SHELL BRACKET
    0x27EE,  // (⟮) MATHEMATICAL LEFT FLATTENED PARENTHESIS
    0x27EF,  // (⟯) MATHEMATICAL RIGHT FLATTENED PARENTHESIS
  };

  return CODEPOINTS;
};

static const std::vector<int> &GetCodepointsForPage(Page p) {
  switch (p) {
  case Page::BIT7_CLASSIC: return PageBit7Classic();
  case Page::BIT7_LATINABC: return PageBit7LatinABC();
  case Page::BIT7_EXTENDED: return PageBit7Extended();
  case Page::BIT7_EXTENDED2: return PageBit7Extended2();
  case Page::BIT7_CYRILLIC: return PageBit7Cyrillic();
  case Page::BIT7_MATH: return PageBit7Math();
  }
  LOG(FATAL) << "Unimplemented page!";
}

// e.g. use the glyph for hyphen (0x2D) to render U+2212 (minus).
// Both source and destination are Unicode codepoints.
static constexpr std::initializer_list<std::pair<int, int>>
REUSE_FOR = {
  // hyphen used as minus
  {0x002D, 0x2212},
  // Division slash
  {'/', 0x2215},
  {'\\', 0x2216},
  // ascii -> cyrillic
  {'S', 0x0405},
  {'J', 0x0408},
  {'A', 0x0410},
  {'B', 0x0412},
  {'E', 0x0415},
  {'M', 0x041C},
  {'H', 0x041D},
  {'O', 0x041E},
  {'P', 0x0420},
  {'C', 0x0421},
  {'I', 0x04C0},
  {'T', 0x0422},
  {'X', 0x0425},
  {'a', 0x0430},
  {'e', 0x0435},
  {'h', 0x04BB},
  {'o', 0x043E},
  {'p', 0x0440},
  {'c', 0x0441},
  {'x', 0x0445},
  {'s', 0x0455},
  {'i', 0x0456},
  {'j', 0x0458},
  {'d', 0x0501},
  {'Q', 0x051A},
  {'q', 0x051B},
  {'W', 0x051C},
  {'w', 0x051D},
  // TODO: More cyrillic can be copied from Latin-1, Greek.
  {0x00C6, 0x04D4}, // Æ -> cyrillic
  {0x00E6, 0x04D5}, // æ -> cyrillic
  {0x0393, 0x0413}, // Γ -> cyrillic
  {0x03A0, 0x041F}, // Π -> cyrillic
  {0x03A6, 0x0424}, // Φ -> cyrillic
  {0x00C8, 0x0400}, // È -> cyrillic
  {0x00CB, 0x0401}, // Ë -> cyrillic
  {0x00E8, 0x0450}, // è -> cyrillic
  {0x00EB, 0x0451}, // ë -> cyrillic
  {0x00C4, 0x04D2}, // Ä -> cyrillic
  {0x00E4, 0x04D3}, // ä -> cyrillic
  {0x00D6, 0x04E6}, // Ö -> cyrillic
  {0x00F6, 0x04E7}, // ö -> cyrillic

  // ascii -> greek
  {'J', 0x037F},
  {'A', 0x0391},
  {'B', 0x0392},
  {'E', 0x0395},
  {'Z', 0x0396},
  {'H', 0x0397},
  {'I', 0x0399},
  {'K', 0x039A},
  {'M', 0x039C},
  {'N', 0x039D},
  {'O', 0x039F},
  {'P', 0x03A1},
  {'T', 0x03A4},
  {'Y', 0x03A5},
  {'X', 0x03A7},
  {'v', 0x03BD},
  {'o', 0x03BF},
  {'u', 0x03C5},
  {'x', 0x03C7},

  // Coptic homoglyphs
  {'C', 0x2CA4},
  {'c', 0x2CA5},
  {'O', 0x2C9E},
  {'o', 0x2C9F},

  // Full-width comma
  {',', 0xFF0C},
  // Fullwidth parentheses
  // Actually we can just map these all from ascii?
  // en.wikipedia.org/wiki/Halfwidth_and_Fullwidth_Forms_(Unicode_block)
  {'(', 0xFF08},
  {')', 0xFF09},

  // "equal and parallel to" is like an octothorpe
  {'#', 0x22D5},

  // Greek -> Math
  {0x03A0, 0x220F},  // Pi -> Product
  {0x03A3, 0x2211},  // Sigma -> Sum

  // "Prohibited sign" is identical to "No entry sign" but
  // black instead of red.
  {0x1F6AB, 0x1F6C7},

  // Kelvin symbol
  {'K', 0x212A},
  // Ohm symbol from greek Omega
  {0x03A9, 0x2126},
  // "micro" symbol from greek mu
  {0x03BC, 0x00B5},

  // ISO Latin-1 Macron to overline
  {0x00AF, 0x203E},

  // bullet -> katakana middle dot
  {0x2022, 0x30FB},

  // Black circle -> black circle for record
  {0x25CF, 0x23FA},
  // Same for square
  {0x25A0, 0x23F9},

  // Various for IPA, Latin Extended B
  {0x03A3, 0x01A9}, // Greek Σ -> esh
  {'!', 0x01C3},
  {0x04D9, 0x0259}, // Cyrillic schwa -> IPA schwa
  {0x04D9, 0x01DD}, // Cyrillic schwa -> Phoenecian turned e
  {0x03A3, 0x01A9}, // Greek Sigma -> Capital Esh
  {0x0510, 0x0190}, // Cyrillic reversed Ze -> Open E
  {0x0511, 0x025B}, // Cyrillic reversed ze -> Open e

  {0x04E0, 0x01B7}, // Cyrillic Capital Abkhasian Dze -> Ezh
  {0x04E1, 0x0292}, // Lowercase dze -> ezh

  // Cyrillic to Extended C
  {0x04A2, 0x2C67}, // H with descender
  // Unicode docs suggest this, but I think Ka and K look different
  // {0x049A, 0x2C69}, // K with descender

  // Math black star to Symbol star
  {0x22c6, 0x2605},

  // Letter-like symbols that are REALLY letter-like.
  // Kelvin
  {'K', 0x212A},
  // A with circle -> Angstrom
  {0x00C5, 0x212B},
  // Omega -> Ohm sign
  {0x03A9, 0x2126},
  // Hebrew letters
  {0x05D0, 0x2135},  // Alef
  {0x05D1, 0x2136},  // Bet
  {0x05D2, 0x2137},  // Gimel
  {0x05D3, 0x2138},  // Dalet

  // Various spaces. Since the font is fixed-width,
  // we just render these the same as space. Most of these
  // are not mapped in the font image.
  {' ', 0x00A0},  // No-Break space
  {' ', 0x2000},  // En Quad
  {' ', 0x2001},  // Em Quad
  {' ', 0x2002},  // En Space
  {' ', 0x2003},  // Em Space
  {' ', 0x2004},  // Three-Per-Em Space
  {' ', 0x2005},  // Four-Per-Em Space
  {' ', 0x2006},  // Six-Per-Em Space
  {' ', 0x2007},  // Figure Space
  {' ', 0x2008},  // Punctuation Space
  {' ', 0x2009},  // Thin Space
  {' ', 0x200A},  // Hair Space
  {' ', 0x202F},  // Narrow No-Break Space
  {' ', 0x205F},  // Medium Mathematical Space
  {' ', 0x3000},  // Ideographic space
};

Config Config::ParseConfig(std::string_view cfgfile) {
  Config config;
  std::map<string, string> m = Util::ReadFileToMap(cfgfile);
  CHECK(!m.empty()) << "Couldn't read config file " << cfgfile;
  config.pngfile = m["pngfile"];
  config.name = m["name"];
  config.copyright = m["copyright"];
  config.charbox_width = atoi(m["charbox-width"].c_str());
  config.charbox_height = atoi(m["charbox-height"].c_str());
  config.descent = atoi(m["descent"].c_str());
  config.spacing = atoi(m["spacing"].c_str());

  if (m.find("chars-across") != m.end())
    config.chars_across = atoi(m["chars-across"].c_str());

  if (m.find("chars-down") != m.end())
    config.chars_down = atoi(m["chars-down"].c_str());

  if (m.find("extra-linespacing") != m.end())
    config.extra_linespacing = atoi(m["extra-linespacing"].c_str());

  if (m.find("no-lowercase") != m.end())
    config.no_lowercase = true;

  if (m.find("fixed-width") != m.end())
    config.fixed_width = true;

  std::vector<std::string> pp =
    Util::Tokens(m["pages"], [](char c) { return c == ' '; });

  if (m.find("vendor") != m.end()) {
    std::string v = m["vendor"];
    CHECK(v.size() == 4) << "Vendor must be exactly 4 bytes.";
    config.vendor[0] = v[0];
    config.vendor[1] = v[1];
    config.vendor[2] = v[2];
    config.vendor[3] = v[3];
  }

  for (const std::string &p : pp) {
    if (!p.empty()) {
      config.pages.push_back(ParsePage(p));
    }
  }

  return config;
}

string FontImage::GlyphString(const Glyph &glyph) {
  string out;
  for (int y = 0; y < glyph.pic.Height(); y++) {
    for (int x = 0; x < glyph.pic.Width(); x++) {
      char c = (glyph.pic.GetPixel(x, y) != 0) ? '#' : '.';
      out += c;
    }
    out += '\n';
  }
  return out;
}

bool FontImage::EmptyGlyph(const Glyph &g) {
  for (int y = 0; y < g.pic.Height(); y++)
    for (int x = 0; x < g.pic.Width(); x++)
      if (g.pic.GetPixel(x, y) != 0) return false;
  return true;
}

void FontImage::AddPage(const ImageRGBA &img, Page p) {

  const int chars_across = config.chars_across;
  const int chars_down = config.chars_down;

  std::unordered_map<int, int> pos_to_glyph;

  for (int cy = 0; cy < chars_down; cy++) {
    for (int cx = 0; cx < chars_across; cx++) {
      const int cidx = chars_across * cy + cx;

      // Get width, by searching for a column of all black.
      auto GetWidth = [&]() {
          // TODO: Check for pixels outside this region.
          if (config.fixed_width)
            return config.charbox_width - config.spacing;
          for (int x = 0; x < config.charbox_width; x++) {
            auto IsBlackColumn = [&]() {
                int sx = cx * config.charbox_width + x;
                for (int y = 0; y < config.charbox_height; y++) {
                  int sy = cy * config.charbox_height + y;
                  uint32_t color = img.GetPixel32(sx, sy);
                  if (color != 0x000000FF) return false;
                }
                return true;
              };
            if (IsBlackColumn()) {
              return x;
            }
          }
          return -1;
        };
      // -1 if not found. This is tolerated for totally empty characters.
      const int width = GetWidth();

      auto IsEmpty = [&]() {
          for (int y = 0; y < config.charbox_height; y++) {
            for (int x = 0; x < config.charbox_width; x++) {
              int sx = cx * config.charbox_width + x;
              int sy = cy * config.charbox_height + y;
              uint32_t color = img.GetPixel32(sx, sy);
              if (color == 0xFFFFFFFF) return false;
            }
          }
          return true;
        };

      if (width < 0) {
        if (!IsEmpty()) {
          printf("%s: "
                 "Character at cx=%d, cy=%d has no width (black column) but "
                 "has a glyph (white pixels).\n",
                 config.pngfile.c_str(), cx, cy);
          CHECK(false);
        }

        continue;
      } else if (width == 0) {
        printf("%s: Character at cx=%d, cy=%d has zero width; "
               "not supported!\n",
               config.pngfile.c_str(), cx, cy);
        CHECK(false);
      } else {
        // Glyph, but possibly an empty one...
        ImageA pic{width, config.charbox_height};
        pic.Clear(0x00);

        for (int y = 0; y < config.charbox_height; y++) {
          for (int x = 0; x < width; x++) {
            int sx = cx * config.charbox_width + x;
            int sy = cy * config.charbox_height + y;
            bool bit = img.GetPixel32(sx, sy) == 0xFFFFFFFF;
            if (bit) pic.SetPixel(x, y, 0xFF);
          }
        }

        // PERF: We could dedupe glyphs here. The main thing
        // would be empty glyphs, I think.

        // No way to set left edge from image yet...
        Glyph glyph{.left_edge = 0, .pic = std::move(pic)};
        int idx = (int)glyphs.size();
        glyphs.push_back(std::move(glyph));
        pos_to_glyph[cidx] = idx;
      }
    }
  }

  // Now map the glyphs (glyphs[gidx]) from their position in the
  // image (cidx) using the page's mapping to unicode codepoints.
  const std::vector<int> &codepoints = GetCodepointsForPage(p);

  for (const auto &[cidx, gidx] : pos_to_glyph) {
    CHECK(gidx >= 0 && gidx < (int)glyphs.size());
    const Glyph &glyph = glyphs[gidx];
    const bool is_empty = EmptyGlyph(glyph);
    const bool ok_missing = config.fixed_width && is_empty;

    if (cidx >= (int)codepoints.size()) {
      if (!ok_missing) {
        printf("Skipping glyph at %d,%d because it is outside the codepoint "
               "array for page %s! There are %d codepoints configured.\n",
               cidx % config.chars_across, cidx / config.chars_across,
               Config::PageString(p),
               (int)codepoints.size());
        printf("%s", GlyphString(glyph).c_str());
      }
      continue;
    }

    CHECK(cidx >= 0 && cidx < (int)codepoints.size());
    const int codepoint = codepoints[cidx];
    if (codepoint < 0) {
      if (!ok_missing) {
        printf("Skipping glyph at %d = %d,%d because the codepoint is not "
               "configured in page %s (it's %d)!\n",
               cidx,
               cidx % config.chars_across, cidx / config.chars_across,
               Config::PageString(p), codepoint);
        printf("%s", FontImage::GlyphString(glyph).c_str());
      }

    } else {
      // We use empty glyphs on the classic codepage, since it is sparsely
      // mapped, and this is where the space character lives. Otherwise,
      // we treat this as a missing glyph. Perhaps we should give some way
      // (like making it all black) to indicate a deliberate empty glyph,
      // though.
      bool write_empty_glyph =
        PageUsesEmptyGlyphs(p) &&
        !unicode_to_glyph.contains(codepoint);

      // A codepoint can appear multiple times in different pages. We take
      // the last one, but don't overwrite with an empty glyph.
      if (!is_empty || write_empty_glyph) {
        unicode_to_glyph[codepoint] = gidx;
      }
    }
  }
}

FontImage::FontImage(const Config &config) : config(config) {

  // For fixed-width fonts, the width is always the size of the charbox
  // minus the intra-character spacing (ignored pixels).

  // For proportional fonts, 'spacing' is presentational (used by
  // makegrid). We derive the width from the black line in each
  // character cell.

  std::unique_ptr<ImageRGBA> input(ImageRGBA::Load(config.pngfile));
  CHECK(input.get() != nullptr) << "Couldn't load: " << config.pngfile;

  const int page_width = config.chars_across * config.charbox_width;
  const int page_height = config.chars_down * config.charbox_height;
  const int spaced_page_width = page_width + config.page_spacing;
#if 0
  CHECK(page_width * config.pages.size() - config.page_spacing ==
        input->Width() &&
        page_height == input->Height()) <<
    "Image with configured charboxes " << config.charbox_width << "x"
                                       << config.charbox_height <<
    " should be " << (config.chars_across * config.charbox_width) << "x"
                  << (config.chars_down * config.charbox_height) << " but got "
                  << input->Width() << "x" << input->Height();
#endif

  // Currently, pages are always arranged horizontally.

  for (int page_num = 0; page_num < (int)config.pages.size(); page_num++) {
    const Page &p = config.pages[page_num];
    const int page_x = page_num * spaced_page_width;

    ImageRGBA page_img(page_width, page_height);

    page_img.CopyImageRect(0, 0, *input, page_x, 0,
                           page_width, page_height);
    AddPage(page_img, p);
  }

  // After every page is loaded, fill in any unused codepoints that
  // can be copied from existing ones.
  for (const auto &[src, dst] : REUSE_FOR) {
    // If we do have a non-blank source, but don't have the dest,
    // copy. (This includes if the dest is completely blank. It would
    // make logical sense to be able to somehow suppress the copy
    // over an empty glyph, but little practical sense.)
    auto sit = unicode_to_glyph.find(src);
    if (sit != unicode_to_glyph.end() &&
        !EmptyGlyph(glyphs[sit->second])) {
      auto dit = unicode_to_glyph.find(dst);
      if (dit == unicode_to_glyph.end() ||
          EmptyGlyph(glyphs[dit->second])) {
        if (VERBOSE) {
          printf("Copy %04x to %04x\n", src, dst);
        }
        unicode_to_glyph[dst] = unicode_to_glyph[src];
      }
    }
  }
}

ImageRGBA FontImage::ImagePage(Page p) {
  const int ww = config.charbox_width;
  const int hh = config.charbox_height;
  ImageRGBA out(config.chars_across * ww, config.chars_down * hh);
  out.Clear32(0xFF0000FF);

  const std::vector<int> &codepoints = GetCodepointsForPage(p);

  for (int y = 0; y < config.chars_down; y++) {
    for (int x = 0; x < config.chars_across; x++) {
      const int cidx = y * config.chars_across + x;
      const bool odd = !!((x + y) & 1);

      const uint32_t bgcolor = odd ? 0x594d96FF : 0x828a19FF;
      const uint32_t locolor = odd ? 0x453984FF : 0x636a0eFF;

      // Fill whole grid cell to start.
      int xs = x * ww;
      int ys = y * hh;
      out.BlendRect32(xs, ys, ww, hh, bgcolor);
      out.BlendRect32(xs, ys + hh - config.descent,
                      ww, config.descent, locolor);

      int codepoint = -1;
      if (cidx < (int)codepoints.size())
        codepoint = codepoints[cidx];

      // Blit the glyph.
      int glyph_width = 0;
      const auto git = unicode_to_glyph.find(codepoint);
      if (codepoint >= 0 && git != unicode_to_glyph.end()) {
        const int glyph_idx = git->second;
        const Glyph &glyph = glyphs[glyph_idx];
        for (int yy = 0; yy < glyph.pic.Height(); yy++) {
          for (int xx = 0; xx < glyph.pic.Width(); xx++) {
            if (glyph.pic.GetPixel(xx, yy) > 0) {
              out.SetPixel32(xs + xx, ys + yy, 0xFFFFFFFF);
            }
          }
        }
        glyph_width = glyph.pic.Width();
      } else {
        // for missing glyphs in proportional fonts, make
        // a blank full-width character so that the grid is
        // visible. Could use some "default width" from
        // config, if we had it.
        glyph_width = config.charbox_width - 1;
      }

      if (config.fixed_width)
        glyph_width = config.charbox_width - config.spacing;

      // Fill remaining horizontal with black.
      int sp = config.charbox_width - glyph_width;
      // printf("%d - %d - %d\n", config.charbox_width, glyph_width, sp);
      out.BlendRect32(xs + glyph_width, ys, sp, hh,
                      0x000000FF);
    }
  }
  return out;
}

void FontImage::SaveImage(std::string_view filename) {
  const int pages_across = (int)config.pages.size();
  const int page_width = config.chars_across * config.charbox_width;
  const int page_height = config.chars_down * config.charbox_height;
  const int spaced_page_width = page_width + config.page_spacing;

  ImageRGBA out(pages_across * spaced_page_width - config.page_spacing,
                page_height);
  out.Clear32(0x440044FF);

  for (int i = 0; i < (int)config.pages.size(); i++) {
    const Page p = config.pages[i];
    ImageRGBA pimg = ImagePage(p);
    out.CopyImageRect(i * spaced_page_width, 0, pimg,
                      0, 0, page_width, page_height);
  }

  out.Save(filename);
}

bool FontImage::MappedCodepoint(uint32_t codepoint) const {
  return unicode_to_glyph.contains(codepoint);
}

BitmapFont::BitmapFont(FontImage font_in) :
  font(std::move(font_in)) {

}

int BitmapFont::Height() const {
  return font.config.charbox_height + font.config.extra_linespacing;
}

int BitmapFont::Width(int cp) const {
  const auto it = font.unicode_to_glyph.find(cp);
  if (it == font.unicode_to_glyph.end())
    return 0;
  int glyph_idx = it->second;
  CHECK(glyph_idx >= 0 && glyph_idx < (int)font.glyphs.size());
  return font.glyphs[glyph_idx].pic.Width();
}

void BitmapFont::DrawText(ImageRGBA *img, int x, int y,
                           uint32_t color,
                          std::string_view s) const {
  std::vector<uint32_t> cps = UTF8::Codepoints(s);
  for (uint32_t cp : cps) {
    const auto it = font.unicode_to_glyph.find(cp);
    if (it == font.unicode_to_glyph.end()) {
      // Draw missing glyph char?
    } else {
      int glyph_idx = it->second;
      CHECK(glyph_idx >= 0 && glyph_idx < (int)font.glyphs.size());
      const FontImage::Glyph &glyph = font.glyphs[glyph_idx];

      // Draw it.
      for (int yy = 0; yy < glyph.pic.Height(); yy++) {
        for (int xx = 0; xx < glyph.pic.Width(); xx++) {
          uint8_t a = glyph.pic.GetPixel(xx, yy);
          if (a) {
            img->BlendPixel32(x + xx, y + yy, color);
          }
        }
      }

      // Advance horizontally.
      x += glyph.pic.Width();
    }
  }
}

std::unique_ptr<BitmapFont> BitmapFont::Load(std::string_view configfile) {
  // XXX interpret png in config relative to config path so that
  // this can load from other directories.
  Config cfg = Config::ParseConfig(configfile);
  FontImage font_image(cfg);
  return std::make_unique<BitmapFont>(std::move(font_image));
}
