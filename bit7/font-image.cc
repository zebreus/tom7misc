#include "font-image.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "image.h"
#include "utf8.h"
#include "util.h"

using namespace std;

static constexpr bool VERBOSE = true;

// No stability guarantees for these codepoints. We should
// consider not even outputting them in the TTFs. The purpose
// is to allow for some utility-style glyph pieces in the
// font images that are not deleted by normalization.
static constexpr uint32_t PUA_START = 0xF7000;
enum PrivateUse : uint32_t {
  PUA_LC_SLASH1 = PUA_START,
  PUA_SLASH_STEEP,
  PUA_SLASH,
  PUA_CENTER_TILDE,
  // lighter than tilde
  PUT_SWUNG_DASH,

  PUA_END,
};
constexpr int NUM_PUA = PUA_END - PUA_START;
static_assert(NUM_PUA <= 16);

static constexpr size_t NUM_COMBINING = 2072;
namespace { extern const std::array<uint32_t, NUM_COMBINING> COMBINING; }

struct PageInfo {
  // Codepoint or -1.
  std::vector<int> codepoints;
  // Optional. Pairs of start index, length.
  // Should not overlap. These are just used
  // to distinguish sections by color in the
  // images.
  std::vector<std::pair<int, int>> sections;
  // True if the nth glyph is a unicode combining glyph
  // (Mn or Me). These are distinguished in the normalized
  // image and output with negative offsets / zero width.
  std::vector<bool> combining;
};

// TODO: Need to add page for "old" DFX fonts.

Page Config::ParsePage(std::string_view p) {
  if (p == "bit7-classic") return Page::BIT7_CLASSIC;
  if (p == "bit7-latinabc") return Page::BIT7_LATINABC;
  if (p == "bit7-extended") return Page::BIT7_EXTENDED;
  if (p == "bit7-extended2") return Page::BIT7_EXTENDED2;
  if (p == "bit7-cyrillic") return Page::BIT7_CYRILLIC;
  if (p == "bit7-math") return Page::BIT7_MATH;
  if (p == "bit7-sym1") return Page::BIT7_SYM1;
  if (p == "bit7-mathfonts") return Page::BIT7_MATHFONTS;
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
  case Page::BIT7_SYM1:
    return "bit7-sym1";
  case Page::BIT7_MATHFONTS:
    return "bit7-mathfonts";
  default:
    break;
  }
}

inline static bool PageUsesEmptyGlyphs(Page p) {
  // Bit7 classic has the main space character.
  return p == Page::BIT7_CLASSIC;
}

// Tips:
//  - To move glyphs between pages, first duplicate them, then
//    normalize, then set the sources to -1, then normalize again.

// Standard size is: 16x24
static PageInfo PageBit7Classic() {
  PageInfo info;
  info.sections = {
    // ASCII.
    {4 * 16, 95},
    {12 * 16, 6 * 16},
  };

  info.codepoints = {
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

    0x2022,  // BULLET
    0x2026,  // HORIZONTAL ELLIPSIS
    0x201B,  // SINGLE HIGH REVERSED 9 QUOTE
    0x201F,  // DOUBLE HIGH REVERSED 9 QUOTE
    0x266A,  // (♪) EIGHTH NOTE

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

    0x2016,  // (‖) Double vertical line

    0x2B1D,  // (⬝) Black very small square
    0x2B1E,  // (⬞) White very small square

    0x20D7,  // (o⃗) Combining Right Arrow Above
    0x0300,  // (ò) Combining Grave Accent
    0x0301,  // (ó) Combining Acute Accent
    0x0302,  // (ô) Combining Circumflex Accent
    0x0303,  // (õ) Combining Tilde
    0x0304,  // (ō) Combining Macron
    0x0305,  // (o̅) Combining Overline
    0x0306,  // (ŏ) Combining Breve
    0x0307,  // (ȯ) Combining Dot Above
    0x0308,  // (ö) Combining Diaeresis
    0x0309,  // (ỏ) Combining Hook Above
    0x030A,  // (o̊) Combining Ring Above
    0x030B,  // (ő) Combining Double Acute Accent
    0x030C,  // (ǒ) Combining Caron
    0x030D,  // (o̍) Combining Vertical Line Above
    0x030E,  // (o̎) Combining Double Vertical Line Above
    0x030F,  // (ȍ) Combining Double Grave Accent

    // Unclaimed. Was once emoji, but I moved those to the extended
    // page.
    -1, -1, -1, -1,

    0x271D,  // (✝) Latin Cross
    0x271E,  // (✞) Shadowed White Latin Cross
    0x271F,  // (✟) Outlined Latin Cross

    // ASCII, in order
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E,

    // 0x7F is DELETE in unicode (no glyph) but in CP-437 this
    // renders as a "house" symbol. It's in the SYM1 page (misc technical).
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
    // "Black" (filled) suits (Spade, Club, Heart, Diamond)
    0x2660, 0x2663, 0x2665, 0x2666,
    // White (outlined) suits (Spade, Club, Heart, Diamond)
    0x2664, 0x2667, 0x2661, 0x2662,
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
    // pi, rho, (final) sigma, sigma, tau, (no upsilon), phi (the letter,
    // which is typically loopy), (no chi), omega
    0x03C0, 0x03C1, 0x03C2, 0x03C3, 0x03C4, 0x03C6, 0x03C8, 0x03C9,
    // mathematical phi symbol (closed)
    0x03D5,

    // TODO: More greeks
    -1, -1, -1, -1,
    // cont'd
    -1, -1, -1, -1, -1, -1, -1, -1,

    // reference mark,
    0x203B,
    // Black circle, black square
    0x25CF, 0x25A0,
    // <?> replacement char
    0xFFFD,

    // Block Elements, in unicode order
    0x2580, 0x2581, 0x2582, 0x2583, 0x2584, 0x2585, 0x2586, 0x2587,
    0x2588, 0x2589, 0x258A, 0x258B, 0x258C, 0x258D, 0x258E, 0x258F,
    0x2590, 0x2591, 0x2592, 0x2593, 0x2594, 0x2595, 0x2596, 0x2597,
    0x2598, 0x2599, 0x259A, 0x259B, 0x259C, 0x259D, 0x259E, 0x259F,
  };

  CHECK(info.codepoints.size() == 16 * 24);
  return info;
}

// Standard size is: 16x24
static PageInfo PageBit7LatinABC() {
  PageInfo info;

  info.sections = {
    {0, 8 * 16},
    {8 * 16, 13 * 16},
    {(8 + 13) * 16, 2 * 16},
    {(8 + 13 + 2) * 16, NUM_PUA},
  };

  info.codepoints = {
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
    0x01ED,  // (ǭ) Latin Small Letter O with Ogonek and Macron
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
  };

  // Private area for utility glyph pieces (e.g. slashes)
  for (int i = 0; i < 16; i++) {
    if (i < NUM_PUA) {
      info.codepoints.push_back(PUA_START + i);
    } else {
      // remainder of line unused
      info.codepoints.push_back(-1);
    }
  }

  CHECK(info.codepoints.size() == 16 * 24) << info.codepoints.size();
  return info;
}

// Standard size is: 16x24
static PageInfo PageBit7Extended() {
  PageInfo info;

  info.sections = {
    {0, 16 * 3},
    {16 * 12, 4 * 16 - 4},
    {16 * 16, 2 * 16 - 6},
    {18 * 16 - 3, 6 * 16 + 3},
  };

  info.codepoints = {
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
    // White heavy check mark emoji
    0x2705,
    // Pile of poo
    0x1F4A9,

    // Film strip.
    // I think there's also an emoji presentation of this
    // when followed by U+FE0F.
    0x1F39E,

    // EMOJI: PACKAGE
    0x1F4E6,

    // Warning Sign
    0x26A0,

    0x1f341,  // maple leaf
    0x1F340,  // four-leaf clover
    0x2618,   // shamrock (needs variant selector to show emoji)
    0x1F4BE,  // floppy disk

    0x1F9A5,  // sloth
    0x1f389,  // party popper

    -1,

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    // Remainder of CP437.
    0x263A,  // (☺︎)
    0x263B,  // (☻)
    // suits are in main page
    // cullet is in main page
    0x25D8,  // (◘)
    0x25CB,  // (○) (hollow circle)
    0x25D9,  // (◙)
    0x2642,  // (♂︎)
    0x2640,  // (♀︎)
    // notes in main page
    0x263C,  // (☼),
    0x25BA,  // (►),
    0x25C4,  // (◄),
    0x2195,  // (↕︎),
    // double exclamation, pilcrow, section in main page
    0x25AC,  // (▬),
    0x21A8,  // (↨), UP DOWN ARROW WITH BASE
    // up, down, right, left arrows in main page
    // right angle in main page
    0x2194,  // (↔︎)
    0x25B2,  // (▲)
    0x25BC,  // (▼)

    // Various line drawing; same order as CP-437
    0x2502,  // (│)
    0x2524,  // (┤)
    0x2561,  // (╡)
    0x2562,  // (╢)
    0x2556,  // (╖)
    0x2555,  // (╕)
    0x2563,  // (╣)
    0x2551,  // (║)
    0x2557,  // (╗)
    0x255D,  // (╝)
    0x255C,  // (╜)
    0x255B,  // (╛)
    0x2510,  // (┐)
    0x2514,  // (└)
    0x2534,  // (┴)
    0x252C,  // (┬)

    0x251C,  // (├)
    0x2500,  // (─)
    0x253C,  // (┼)
    0x255E,  // (╞)
    0x255F,  // (╟)
    0x255A,  // (╚)
    0x2554,  // (╔)
    0x2569,  // (╩)
    0x2566,  // (╦)
    0x2560,  // (╠)
    0x2550,  // (═)
    0x256C,  // (╬)
    0x2567,  // (╧)
    0x2568,  // (╨)
    0x2564,  // (╤)
    0x2565,  // (╥)

    0x2559,  // (╙)
    0x2558,  // (╘)
    0x2552,  // (╒)
    0x2553,  // (╓)
    0x256B,  // (╫)
    0x256A,  // (╪)
    0x2518,  // (┘)
    0x250C,  // (┌)

    // Integral signs and reversed not sign. In SYM1 (misc technical).
    -1, -1, -1,
    // Note that some fonts will show this as a P with a stroke,
    // but this is apparently an error. It should be a squished
    // "Pt" (if emulating DOS CP 437) or "Pts" if you have the
    // space.
    0x20A7,  // (₧) PESETA SIGN
    // Unclaimed
    -1, -1, -1, -1,

    // TODO: Maybe a good place to fill out stuff like CP 850.
    // For CP 850: U+2017

    // Two rows for Greek Extended (custom mapping)
    0x0385,  // (΅) GREEK DIALYTIKA TONOS
    0x0386,  // (Ά) GREEK CAPITAL LETTER ALPHA WITH TONOS
    0x0388,  // (Έ) GREEK CAPITAL LETTER EPSILON WITH TONOS
    0x0389,  // (Ή) GREEK CAPITAL LETTER ETA WITH TONOS
    0x038A,  // (Ί) GREEK CAPITAL LETTER IOTA WITH TONOS
    0x038C,  // (Ό) GREEK CAPITAL LETTER OMICRON WITH TONOS
    0x038E,  // (Ύ) GREEK CAPITAL LETTER UPSILON WITH TONOS
    0x038F,  // (Ώ) GREEK CAPITAL LETTER OMEGA WITH TONOS
    0x0390,  // (ΐ) GREEK SMALL LETTER IOTA WITH DIALYTIKA AND TONOS
    0x03CA,  // (ϊ) GREEK SMALL LETTER IOTA WITH DIALYTIKA
    0x03CB,  // (ϋ) GREEK SMALL LETTER UPSILON WITH DIALYTIKA
    0x03CD,  // (ύ) GREEK SMALL LETTER UPSILON WITH TONOS
    0x03CE,  // (ώ) GREEK SMALL LETTER OMEGA WITH TONOS
    0x03AC,  // (ά) GREEK SMALL LETTER ALPHA WITH TONOS
    0x03AD,  // (έ) GREEK SMALL LETTER EPSILON WITH TONOS
    0x03AE,  // (ή) GREEK SMALL LETTER ETA WITH TONOS

    0x03AF,  // (ί) GREEK SMALL LETTER IOTA WITH TONOS
    0x03B0,  // (ΰ) GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND TONOS
    0x03DA,  // (Ϛ) GREEK LETTER STIGMA
    0x03DB,  // (ϛ) GREEK SMALL LETTER STIGMA
    0x03DC,  // (Ϝ) GREEK LETTER DIGAMMA
    0x03DD,  // (ϝ) GREEK SMALL LETTER DIGAMMA
    // Note that because the lowercase koppa was introduced
    // later, lots of text uses 03DE as the numeral.
    // Modern practice is to make the glyphs look different
    // but for the first to still make sense in a numeral
    // context (since that is the main way it actually
    // appears).
    0x03DE,  // (Ϟ) GREEK LETTER KOPPA
    0x03DF,  // (ϟ) GREEK SMALL LETTER KOPPA
    // Similar...
    0x03E0,  // (Ϡ) GREEK LETTER SAMPI
    0x03E1,  // (ϡ) GREEK SMALL LETTER SAMPI

    // Space for more Greek Extended
    -1, -1, -1,


    // Characters often used in Japanese writing systems.

    0x300C,  // (「) LEFT CORNER BRACKET
    0x300D,  // (」) RIGHT CORNER BRACKET
    0x3001,  // (、) IDEOGRAPHIC COMMA

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

  CHECK(info.codepoints.size() == 16 * 24) << info.codepoints.size();
  return info;
}


// Standard size is: 16x24
static PageInfo PageBit7Extended2() {
  PageInfo info;
  info.sections = {
    {0, 16 * 3 - 6},
    {16 * 3, 5 * 16},
    {16 * 8, 16 * 8},
  };

  info.codepoints = {
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
    0x02D8, // (˘) Breve
    0x02D9, // (˙) Dot Above
    0x02DA, // (˚) Ring Above
    0x02DB, // (˛) Ogonek
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


    // Unicode Phonetic Extensions

    0x1D00, // (ᴀ) Latin Letter Small Capital A
    0x1D01, // (ᴁ) Latin Letter Small Capital Ae
    0x1D02, // (ᴂ) Latin Small Letter Turned Ae
    0x1D03, // (ᴃ) Latin Letter Small Capital Barred B
    0x1D04, // (ᴄ) Latin Letter Small Capital C
    0x1D05, // (ᴅ) Latin Letter Small Capital D
    0x1D06, // (ᴆ) Latin Letter Small Capital Eth
    0x1D07, // (ᴇ) Latin Letter Small Capital E
    0x1D08, // (ᴈ) Latin Small Letter Turned Open E
    0x1D09, // (ᴉ) Latin Small Letter Turned I
    0x1D0A, // (ᴊ) Latin Letter Small Capital J
    0x1D0B, // (ᴋ) Latin Letter Small Capital K
    0x1D0C, // (ᴌ) Latin Letter Small Capital L with Stroke
    0x1D0D, // (ᴍ) Latin Letter Small Capital M
    0x1D0E, // (ᴎ) Latin Letter Small Capital Reversed N
    0x1D0F, // (ᴏ) Latin Letter Small Capital O
    0x1D10, // (ᴐ) Latin Letter Small Capital Open O
    0x1D11, // (ᴑ) Latin Small Letter Sideways O
    0x1D12, // (ᴒ) Latin Small Letter Sideways Open O
    0x1D13, // (ᴓ) Latin Small Letter Sideways O with Stroke
    0x1D14, // (ᴔ) Latin Small Letter Turned Oe
    0x1D15, // (ᴕ) Latin Letter Small Capital Ou
    0x1D16, // (ᴖ) Latin Small Letter Top Half O
    0x1D17, // (ᴗ) Latin Small Letter Bottom Half O
    0x1D18, // (ᴘ) Latin Letter Small Capital P
    0x1D19, // (ᴙ) Latin Letter Small Capital Reversed R
    0x1D1A, // (ᴚ) Latin Letter Small Capital Turned R
    0x1D1B, // (ᴛ) Latin Letter Small Capital T
    0x1D1C, // (ᴜ) Latin Letter Small Capital U
    0x1D1D, // (ᴝ) Latin Small Letter Sideways U
    0x1D1E, // (ᴞ) Latin Small Letter Sideways Diaeresized U
    0x1D1F, // (ᴟ) Latin Small Letter Sideways Turned M
    0x1D20, // (ᴠ) Latin Letter Small Capital V
    0x1D21, // (ᴡ) Latin Letter Small Capital W
    0x1D22, // (ᴢ) Latin Letter Small Capital Z
    0x1D23, // (ᴣ) Latin Letter Small Capital Ezh
    0x1D24, // (ᴤ) Latin Letter Voiced Laryngeal Spirant
    0x1D25, // (ᴥ) Latin Letter Ain
    0x1D26, // (ᴦ) Greek Letter Small Capital Gamma
    0x1D27, // (ᴧ) Greek Letter Small Capital Lamda
    0x1D28, // (ᴨ) Greek Letter Small Capital Pi
    0x1D29, // (ᴩ) Greek Letter Small Capital Rho
    0x1D2A, // (ᴪ) Greek Letter Small Capital Psi
    0x1D2B, // (ᴫ) Cyrillic Letter Small Capital El
    0x1D2C, // (ᴬ) Modifier Letter Capital A
    0x1D2D, // (ᴭ) Modifier Letter Capital Ae
    0x1D2E, // (ᴮ) Modifier Letter Capital B
    0x1D2F, // (ᴯ) Modifier Letter Capital Barred B
    0x1D30, // (ᴰ) Modifier Letter Capital D
    0x1D31, // (ᴱ) Modifier Letter Capital E
    0x1D32, // (ᴲ) Modifier Letter Capital Reversed E
    0x1D33, // (ᴳ) Modifier Letter Capital G
    0x1D34, // (ᴴ) Modifier Letter Capital H
    0x1D35, // (ᴵ) Modifier Letter Capital I
    0x1D36, // (ᴶ) Modifier Letter Capital J
    0x1D37, // (ᴷ) Modifier Letter Capital K
    0x1D38, // (ᴸ) Modifier Letter Capital L
    0x1D39, // (ᴹ) Modifier Letter Capital M
    0x1D3A, // (ᴺ) Modifier Letter Capital N
    0x1D3B, // (ᴻ) Modifier Letter Capital Reversed N
    0x1D3C, // (ᴼ) Modifier Letter Capital O
    0x1D3D, // (ᴽ) Modifier Letter Capital Ou
    0x1D3E, // (ᴾ) Modifier Letter Capital P
    0x1D3F, // (ᴿ) Modifier Letter Capital R
    0x1D40, // (ᵀ) Modifier Letter Capital T
    0x1D41, // (ᵁ) Modifier Letter Capital U
    0x1D42, // (ᵂ) Modifier Letter Capital W
    0x1D43, // (ᵃ) Modifier Letter Small A
    0x1D44, // (ᵄ) Modifier Letter Small Turned A
    0x1D45, // (ᵅ) Modifier Letter Small Alpha
    0x1D46, // (ᵆ) Modifier Letter Small Turned Ae
    0x1D47, // (ᵇ) Modifier Letter Small B
    0x1D48, // (ᵈ) Modifier Letter Small D
    0x1D49, // (ᵉ) Modifier Letter Small E
    0x1D4A, // (ᵊ) Modifier Letter Small Schwa
    0x1D4B, // (ᵋ) Modifier Letter Small Open E
    0x1D4C, // (ᵌ) Modifier Letter Small Turned Open E
    0x1D4D, // (ᵍ) Modifier Letter Small G
    0x1D4E, // (ᵎ) Modifier Letter Small Turned I
    0x1D4F, // (ᵏ) Modifier Letter Small K
    0x1D50, // (ᵐ) Modifier Letter Small M
    0x1D51, // (ᵑ) Modifier Letter Small Eng
    0x1D52, // (ᵒ) Modifier Letter Small O
    0x1D53, // (ᵓ) Modifier Letter Small Open O
    0x1D54, // (ᵔ) Modifier Letter Small Top Half O
    0x1D55, // (ᵕ) Modifier Letter Small Bottom Half O
    0x1D56, // (ᵖ) Modifier Letter Small P
    0x1D57, // (ᵗ) Modifier Letter Small T
    0x1D58, // (ᵘ) Modifier Letter Small U
    0x1D59, // (ᵙ) Modifier Letter Small Sideways U
    0x1D5A, // (ᵚ) Modifier Letter Small Turned M
    0x1D5B, // (ᵛ) Modifier Letter Small V
    0x1D5C, // (ᵜ) Modifier Letter Small Ain
    0x1D5D, // (ᵝ) Modifier Letter Small Beta
    0x1D5E, // (ᵞ) Modifier Letter Small Greek Gamma
    0x1D5F, // (ᵟ) Modifier Letter Small Delta
    0x1D60, // (ᵠ) Modifier Letter Small Greek Phi
    0x1D61, // (ᵡ) Modifier Letter Small Chi
    0x1D62, // (ᵢ) Latin Subscript Small Letter I
    0x1D63, // (ᵣ) Latin Subscript Small Letter R
    0x1D64, // (ᵤ) Latin Subscript Small Letter U
    0x1D65, // (ᵥ) Latin Subscript Small Letter V
    0x1D66, // (ᵦ) Greek Subscript Small Letter Beta
    0x1D67, // (ᵧ) Greek Subscript Small Letter Gamma
    0x1D68, // (ᵨ) Greek Subscript Small Letter Rho
    0x1D69, // (ᵩ) Greek Subscript Small Letter Phi
    0x1D6A, // (ᵪ) Greek Subscript Small Letter Chi
    0x1D6B, // (ᵫ) Latin Small Letter Ue
    0x1D6C, // (ᵬ) Latin Small Letter B with Middle Tilde
    0x1D6D, // (ᵭ) Latin Small Letter D with Middle Tilde
    0x1D6E, // (ᵮ) Latin Small Letter F with Middle Tilde
    0x1D6F, // (ᵯ) Latin Small Letter M with Middle Tilde
    0x1D70, // (ᵰ) Latin Small Letter N with Middle Tilde
    0x1D71, // (ᵱ) Latin Small Letter P with Middle Tilde
    0x1D72, // (ᵲ) Latin Small Letter R with Middle Tilde
    0x1D73, // (ᵳ) Latin Small Letter R with Fishhook and Middle Tilde
    0x1D74, // (ᵴ) Latin Small Letter S with Middle Tilde
    0x1D75, // (ᵵ) Latin Small Letter T with Middle Tilde
    0x1D76, // (ᵶ) Latin Small Letter Z with Middle Tilde
    0x1D77, // (ᵷ) Latin Small Letter Turned G
    0x1D78, // (ᵸ) Modifier Letter Cyrillic En
    0x1D79, // (ᵹ) Latin Small Letter Insular G
    0x1D7A, // (ᵺ) Latin Small Letter Th with Strikethrough
    0x1D7B, // (ᵻ) Latin Small Capital Letter I with Stroke
    0x1D7C, // (ᵼ) Latin Small Letter Iota with Stroke
    0x1D7D, // (ᵽ) Latin Small Letter P with Stroke
    0x1D7E, // (ᵾ) Latin Small Capital Letter U with Stroke
    0x1D7F, // (ᵿ) Latin Small Letter Upsilon with Stroke

    // Free to a good home

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  };

  CHECK(info.codepoints.size() == 16 * 24) << info.codepoints.size();
  return info;
};

static PageInfo PageBit7Cyrillic() {
  PageInfo info;

  info.sections = {
    {0, 16 * 16},
    {(24 - 8) * 16, 3 * 16},
  };

  info.codepoints = {
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

  CHECK(info.codepoints.size() == 16 * 24) << info.codepoints.size();
  return info;
}

static PageInfo PageBit7Math() {
  PageInfo info;
  info.sections = {
    {0, 16 * 16},
    {(24 - 8) * 16, 5 * 16},
    {(24 - 3) * 16, 3 * 16},
  };


  info.codepoints = {
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

    // Miscellaneous Mathematical Symbols-A
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


  CHECK(info.codepoints.size() == 16 * 24) << info.codepoints.size();
  return info;
};

static PageInfo PageBit7Sym1() {
  PageInfo info;
  info.sections = {
    // Misc technical
    {0, 16 * 16},
    // Geometric Shapes
    {16 * 16, 6 * 16},
    // Supplemental Arrows A
    {(16 + 6) * 16, 16},
  };


  info.codepoints = {
    // Miscellaneous Technical
    0x2300,  // (⌀) DIAMETER SIGN
    0x2301,  // (⌁) ELECTRIC ARROW
    0x2302,  // (⌂) HOUSE
    0x2303,  // (⌃) UP ARROWHEAD
    0x2304,  // (⌄) DOWN ARROWHEAD
    0x2305,  // (⌅) PROJECTIVE
    0x2306,  // (⌆) PERSPECTIVE
    0x2307,  // (⌇) WAVY LINE
    0x2308,  // (⌈) LEFT CEILING
    0x2309,  // (⌉) RIGHT CEILING
    0x230a,  // (⌊) LEFT FLOOR
    0x230b,  // (⌋) RIGHT FLOOR
    0x230c,  // (⌌) BOTTOM RIGHT CROP
    0x230d,  // (⌍) BOTTOM LEFT CROP
    0x230e,  // (⌎) TOP RIGHT CROP
    0x230f,  // (⌏) TOP LEFT CROP
    0x2310,  // (⌐) REVERSED NOT SIGN
    0x2311,  // (⌑) SQUARE LOZENGE
    0x2312,  // (⌒) ARC
    0x2313,  // (⌓) SEGMENT
    0x2314,  // (⌔) SECTOR
    0x2315,  // (⌕) TELEPHONE RECORDER
    0x2316,  // (⌖) POSITION INDICATOR
    0x2317,  // (⌗) VIEWDATA SQUARE
    0x2318,  // (⌘) PLACE OF INTEREST SIGN (open apple)
    0x2319,  // (⌙) TURNED NOT SIGN
    0x231a,  // (⌚) WATCH
    0x231b,  // (⌛) HOURGLASS
    0x231c,  // (⌜) TOP LEFT CORNER
    0x231d,  // (⌝) TOP RIGHT CORNER
    0x231e,  // (⌞) BOTTOM LEFT CORNER
    0x231f,  // (⌟) BOTTOM RIGHT CORNER
    0x2320,  // (⌠) TOP HALF INTEGRAL
    0x2321,  // (⌡) BOTTOM HALF INTEGRAL
    0x2322,  // (⌢) FROWN
    0x2323,  // (⌣) SMILE
    0x2324,  // (⌤) UP ARROWHEAD BETWEEN TWO HORIZONTAL BARS
    0x2325,  // (⌥) OPTION KEY
    0x2326,  // (⌦) ERASE TO THE RIGHT
    0x2327,  // (⌧) X IN A RECTANGLE BOX
    0x2328,  // (⌨) KEYBOARD
    0x2329,  // (〈) LEFT-POINTING ANGLE BRACKET
    0x232a,  // (〉) RIGHT-POINTING ANGLE BRACKET
    0x232b,  // (⌫) ERASE TO THE LEFT
    0x232c,  // (⌬) BENZENE RING
    0x232d,  // (⌭) CYLINDRICITY
    0x232e,  // (⌮) ALL AROUND-PROFILE
    0x232f,  // (⌯) SYMMETRY
    0x2330,  // (⌰) TOTAL RUNOUT
    0x2331,  // (⌱) DIMENSION ORIGIN
    0x2332,  // (⌲) CONICAL TAPER
    0x2333,  // (⌳) SLOPE
    0x2334,  // (⌴) COUNTERBORE
    0x2335,  // (⌵) COUNTERSINK
    0x2336,  // (⌶) APL FUNCTIONAL SYMBOL I-BEAM
    0x2337,  // (⌷) APL FUNCTIONAL SYMBOL SQUISH QUAD
    0x2338,  // (⌸) APL FUNCTIONAL SYMBOL QUAD EQUAL
    0x2339,  // (⌹) APL FUNCTIONAL SYMBOL QUAD DIVIDE
    0x233a,  // (⌺) APL FUNCTIONAL SYMBOL QUAD DIAMOND
    0x233b,  // (⌻) APL FUNCTIONAL SYMBOL QUAD JOT
    0x233c,  // (⌼) APL FUNCTIONAL SYMBOL QUAD CIRCLE
    0x233d,  // (⌽) APL FUNCTIONAL SYMBOL CIRCLE STILE
    0x233e,  // (⌾) APL FUNCTIONAL SYMBOL CIRCLE JOT
    0x233f,  // (⌿) APL FUNCTIONAL SYMBOL SLASH BAR
    0x2340,  // (⍀) APL FUNCTIONAL SYMBOL BACKSLASH BAR
    0x2341,  // (⍁) APL FUNCTIONAL SYMBOL QUAD SLASH
    0x2342,  // (⍂) APL FUNCTIONAL SYMBOL QUAD BACKSLASH
    0x2343,  // (⍃) APL FUNCTIONAL SYMBOL QUAD LESS-THAN
    0x2344,  // (⍄) APL FUNCTIONAL SYMBOL QUAD GREATER-THAN
    0x2345,  // (⍅) APL FUNCTIONAL SYMBOL LEFTWARDS VANE
    0x2346,  // (⍆) APL FUNCTIONAL SYMBOL RIGHTWARDS VANE
    0x2347,  // (⍇) APL FUNCTIONAL SYMBOL QUAD LEFTWARDS ARROW
    0x2348,  // (⍈) APL FUNCTIONAL SYMBOL QUAD RIGHTWARDS ARROW
    0x2349,  // (⍉) APL FUNCTIONAL SYMBOL CIRCLE BACKSLASH
    0x234a,  // (⍊) APL FUNCTIONAL SYMBOL DOWN TACK UNDERBAR
    0x234b,  // (⍋) APL FUNCTIONAL SYMBOL DELTA STILE
    0x234c,  // (⍌) APL FUNCTIONAL SYMBOL QUAD DOWN CARET
    0x234d,  // (⍍) APL FUNCTIONAL SYMBOL QUAD DELTA
    0x234e,  // (⍎) APL FUNCTIONAL SYMBOL DOWN TACK JOT
    0x234f,  // (⍏) APL FUNCTIONAL SYMBOL UPWARDS VANE
    0x2350,  // (⍐) APL FUNCTIONAL SYMBOL QUAD UPWARDS ARROW
    0x2351,  // (⍑) APL FUNCTIONAL SYMBOL UP TACK OVERBAR
    0x2352,  // (⍒) APL FUNCTIONAL SYMBOL DEL STILE
    0x2353,  // (⍓) APL FUNCTIONAL SYMBOL QUAD UP CARET
    0x2354,  // (⍔) APL FUNCTIONAL SYMBOL QUAD DEL
    0x2355,  // (⍕) APL FUNCTIONAL SYMBOL UP TACK JOT
    0x2356,  // (⍖) APL FUNCTIONAL SYMBOL DOWNWARDS VANE
    0x2357,  // (⍗) APL FUNCTIONAL SYMBOL QUAD DOWNWARDS ARROW
    0x2358,  // (⍘) APL FUNCTIONAL SYMBOL QUOTE UNDERBAR
    0x2359,  // (⍙) APL FUNCTIONAL SYMBOL DELTA UNDERBAR
    0x235a,  // (⍚) APL FUNCTIONAL SYMBOL DIAMOND UNDERBAR
    0x235b,  // (⍛) APL FUNCTIONAL SYMBOL JOT UNDERBAR
    0x235c,  // (⍜) APL FUNCTIONAL SYMBOL CIRCLE UNDERBAR
    0x235d,  // (⍝) APL FUNCTIONAL SYMBOL UP SHOE JOT
    0x235e,  // (⍞) APL FUNCTIONAL SYMBOL QUOTE QUAD
    0x235f,  // (⍟) APL FUNCTIONAL SYMBOL CIRCLE STAR
    0x2360,  // (⍠) APL FUNCTIONAL SYMBOL QUAD COLON
    0x2361,  // (⍡) APL FUNCTIONAL SYMBOL UP TACK DIAERESIS
    0x2362,  // (⍢) APL FUNCTIONAL SYMBOL DEL DIAERESIS
    0x2363,  // (⍣) APL FUNCTIONAL SYMBOL STAR DIAERESIS
    0x2364,  // (⍤) APL FUNCTIONAL SYMBOL JOT DIAERESIS
    0x2365,  // (⍥) APL FUNCTIONAL SYMBOL CIRCLE DIAERESIS
    0x2366,  // (⍦) APL FUNCTIONAL SYMBOL DOWN SHOE STILE
    0x2367,  // (⍧) APL FUNCTIONAL SYMBOL LEFT SHOE STILE
    0x2368,  // (⍨) APL FUNCTIONAL SYMBOL TILDE DIAERESIS
    0x2369,  // (⍩) APL FUNCTIONAL SYMBOL GREATER-THAN DIAERESIS
    0x236a,  // (⍪) APL FUNCTIONAL SYMBOL COMMA BAR
    0x236b,  // (⍫) APL FUNCTIONAL SYMBOL DEL TILDE
    0x236c,  // (⍬) APL FUNCTIONAL SYMBOL ZILDE
    0x236d,  // (⍭) APL FUNCTIONAL SYMBOL STILE TILDE
    0x236e,  // (⍮) APL FUNCTIONAL SYMBOL SEMICOLON UNDERBAR
    0x236f,  // (⍯) APL FUNCTIONAL SYMBOL QUAD NOT EQUAL
    0x2370,  // (⍰) APL FUNCTIONAL SYMBOL QUAD QUESTION
    0x2371,  // (⍱) APL FUNCTIONAL SYMBOL DOWN CARET TILDE
    0x2372,  // (⍲) APL FUNCTIONAL SYMBOL UP CARET TILDE
    0x2373,  // (⍳) APL FUNCTIONAL SYMBOL IOTA
    0x2374,  // (⍴) APL FUNCTIONAL SYMBOL RHO
    0x2375,  // (⍵) APL FUNCTIONAL SYMBOL OMEGA
    0x2376,  // (⍶) APL FUNCTIONAL SYMBOL ALPHA UNDERBAR
    0x2377,  // (⍷) APL FUNCTIONAL SYMBOL EPSILON UNDERBAR
    0x2378,  // (⍸) APL FUNCTIONAL SYMBOL IOTA UNDERBAR
    0x2379,  // (⍹) APL FUNCTIONAL SYMBOL OMEGA UNDERBAR
    0x237a,  // (⍺) APL FUNCTIONAL SYMBOL ALPHA
    0x237b,  // (⍻) NOT CHECK MARK
    0x237c,  // (⍼) RIGHT ANGLE WITH DOWNWARDS ZIGZAG ARROW
    0x237d,  // (⍽) SHOULDERED OPEN BOX
    0x237e,  // (⍾) BELL SYMBOL
    0x237f,  // (⍿) VERTICAL LINE WITH MIDDLE DOT
    0x2380,  // (⎀) INSERTION SYMBOL
    0x2381,  // (⎁) CONTINUOUS UNDERLINE SYMBOL
    0x2382,  // (⎂) DISCONTINUOUS UNDERLINE SYMBOL
    0x2383,  // (⎃) EMPHASIS SYMBOL
    0x2384,  // (⎄) COMPOSITION SYMBOL
    0x2385,  // (⎅) WHITE SQUARE WITH CENTRE VERTICAL LINE
    0x2386,  // (⎆) ENTER SYMBOL
    0x2387,  // (⎇) ALTERNATIVE KEY SYMBOL
    0x2388,  // (⎈) HELM SYMBOL
    0x2389,  // (⎉) CIRCLED HORIZONTAL BAR WITH NOTCH
    0x238a,  // (⎊) CIRCLED TRIANGLE DOWN
    0x238b,  // (⎋) BROKEN CIRCLE WITH NORTHWEST ARROW
    0x238c,  // (⎌) UNDO SYMBOL
    0x238d,  // (⎍) MONOSTABLE SYMBOL
    0x238e,  // (⎎) HYSTERESIS SYMBOL
    0x238f,  // (⎏) OPEN-CIRCUIT-OUTPUT H-TYPE SYMBOL
    0x2390,  // (⎐) OPEN-CIRCUIT-OUTPUT L-TYPE SYMBOL
    0x2391,  // (⎑) PASSIVE-PULL-DOWN-OUTPUT SYMBOL
    0x2392,  // (⎒) PASSIVE-PULL-UP-OUTPUT SYMBOL
    0x2393,  // (⎓) DIRECT CURRENT SYMBOL FORM TWO
    0x2394,  // (⎔) SOFTWARE-FUNCTION SYMBOL
    0x2395,  // (⎕) APL FUNCTIONAL SYMBOL QUAD
    0x2396,  // (⎖) DECIMAL SEPARATOR KEY SYMBOL
    0x2397,  // (⎗) PREVIOUS PAGE
    0x2398,  // (⎘) NEXT PAGE
    0x2399,  // (⎙) PRINT SCREEN SYMBOL
    0x239a,  // (⎚) CLEAR SCREEN SYMBOL
    0x239b,  // (⎛) LEFT PARENTHESIS UPPER HOOK
    0x239c,  // (⎜) LEFT PARENTHESIS EXTENSION
    0x239d,  // (⎝) LEFT PARENTHESIS LOWER HOOK
    0x239e,  // (⎞) RIGHT PARENTHESIS UPPER HOOK
    0x239f,  // (⎟) RIGHT PARENTHESIS EXTENSION
    0x23a0,  // (⎠) RIGHT PARENTHESIS LOWER HOOK
    0x23a1,  // (⎡) LEFT SQUARE BRACKET UPPER CORNER
    0x23a2,  // (⎢) LEFT SQUARE BRACKET EXTENSION
    0x23a3,  // (⎣) LEFT SQUARE BRACKET LOWER CORNER
    0x23a4,  // (⎤) RIGHT SQUARE BRACKET UPPER CORNER
    0x23a5,  // (⎥) RIGHT SQUARE BRACKET EXTENSION
    0x23a6,  // (⎦) RIGHT SQUARE BRACKET LOWER CORNER
    0x23a7,  // (⎧) LEFT CURLY BRACKET UPPER HOOK
    0x23a8,  // (⎨) LEFT CURLY BRACKET MIDDLE PIECE
    0x23a9,  // (⎩) LEFT CURLY BRACKET LOWER HOOK
    0x23aa,  // (⎪) CURLY BRACKET EXTENSION
    0x23ab,  // (⎫) RIGHT CURLY BRACKET UPPER HOOK
    0x23ac,  // (⎬) RIGHT CURLY BRACKET MIDDLE PIECE
    0x23ad,  // (⎭) RIGHT CURLY BRACKET LOWER HOOK
    0x23ae,  // (⎮) INTEGRAL EXTENSION
    0x23af,  // (⎯) HORIZONTAL LINE EXTENSION
    0x23b0,  // (⎰) UPPER LEFT OR LOWER RIGHT CURLY BRACKET SECTION
    0x23b1,  // (⎱) UPPER RIGHT OR LOWER LEFT CURLY BRACKET SECTION
    0x23b2,  // (⎲) SUMMATION TOP
    0x23b3,  // (⎳) SUMMATION BOTTOM
    0x23b4,  // (⎴) TOP SQUARE BRACKET
    0x23b5,  // (⎵) BOTTOM SQUARE BRACKET
    0x23b6,  // (⎶) BOTTOM SQUARE BRACKET OVER TOP SQUARE BRACKET
    0x23b7,  // (⎷) RADICAL SYMBOL BOTTOM
    0x23b8,  // (⎸) LEFT VERTICAL BOX LINE
    0x23b9,  // (⎹) RIGHT VERTICAL BOX LINE
    0x23ba,  // (⎺) HORIZONTAL SCAN LINE-1
    0x23bb,  // (⎻) HORIZONTAL SCAN LINE-3 (fyi: LINE-5 is U+2500)
    0x23bc,  // (⎼) HORIZONTAL SCAN LINE-7
    0x23bd,  // (⎽) HORIZONTAL SCAN LINE-9
    0x23be,  // (⎾) DENTISTRY SYMBOL LIGHT VERTICAL AND TOP RIGHT
    0x23bf,  // (⎿) DENTISTRY SYMBOL LIGHT VERTICAL AND BOTTOM RIGHT
    0x23c0,  // (⏀) DENTISTRY SYMBOL LIGHT VERTICAL WITH CIRCLE
    0x23c1,  // (⏁) DENTISTRY SYMBOL LIGHT DOWN AND HORIZONTAL WITH CIRCLE
    0x23c2,  // (⏂) DENTISTRY SYMBOL LIGHT UP AND HORIZONTAL WITH CIRCLE
    0x23c3,  // (⏃) DENTISTRY SYMBOL LIGHT VERTICAL WITH TRIANGLE
    0x23c4,  // (⏄) DENTISTRY SYMBOL LIGHT DOWN AND HORIZONTAL WITH TRIANGLE
    0x23c5,  // (⏅) DENTISTRY SYMBOL LIGHT UP AND HORIZONTAL WITH TRIANGLE
    0x23c6,  // (⏆) DENTISTRY SYMBOL LIGHT VERTICAL AND WAVE
    0x23c7,  // (⏇) DENTISTRY SYMBOL LIGHT DOWN AND HORIZONTAL WITH WAVE
    0x23c8,  // (⏈) DENTISTRY SYMBOL LIGHT UP AND HORIZONTAL WITH WAVE
    0x23c9,  // (⏉) DENTISTRY SYMBOL LIGHT DOWN AND HORIZONTAL
    0x23ca,  // (⏊) DENTISTRY SYMBOL LIGHT UP AND HORIZONTAL
    0x23cb,  // (⏋) DENTISTRY SYMBOL LIGHT VERTICAL AND TOP LEFT
    0x23cc,  // (⏌) DENTISTRY SYMBOL LIGHT VERTICAL AND BOTTOM LEFT
    0x23cd,  // (⏍) SQUARE FOOT
    0x23ce,  // (⏎) RETURN SYMBOL
    0x23cf,  // (⏏) EJECT SYMBOL
    0x23d0,  // (⏐) VERTICAL LINE EXTENSION
    0x23d1,  // (⏑) METRICAL BREVE
    0x23d2,  // (⏒) METRICAL LONG OVER SHORT
    0x23d3,  // (⏓) METRICAL SHORT OVER LONG
    0x23d4,  // (⏔) METRICAL LONG OVER TWO SHORTS
    0x23d5,  // (⏕) METRICAL TWO SHORTS OVER LONG
    0x23d6,  // (⏖) METRICAL TWO SHORTS JOINED
    0x23d7,  // (⏗) METRICAL TRISEME
    0x23d8,  // (⏘) METRICAL TETRASEME
    0x23d9,  // (⏙) METRICAL PENTASEME
    0x23da,  // (⏚) EARTH GROUND
    0x23db,  // (⏛) FUSE
    0x23dc,  // (⏜) TOP PARENTHESIS
    0x23dd,  // (⏝) BOTTOM PARENTHESIS
    0x23de,  // (⏞) TOP CURLY BRACKET
    0x23df,  // (⏟) BOTTOM CURLY BRACKET
    0x23e0,  // (⏠) TOP TORTOISE SHELL BRACKET
    0x23e1,  // (⏡) BOTTOM TORTOISE SHELL BRACKET
    0x23e2,  // (⏢) WHITE TRAPEZIUM
    0x23e3,  // (⏣) BENZENE RING WITH CIRCLE
    0x23e4,  // (⏤) STRAIGHTNESS
    0x23e5,  // (⏥) FLATNESS
    0x23e6,  // (⏦) AC CURRENT
    0x23e7,  // (⏧) ELECTRICAL INTERSECTION
    0x23e8,  // (⏨) DECIMAL EXPONENT SYMBOL
    0x23e9,  // (⏩) BLACK RIGHT-POINTING DOUBLE TRIANGLE
    0x23ea,  // (⏪) BLACK LEFT-POINTING DOUBLE TRIANGLE
    0x23eb,  // (⏫) BLACK UP-POINTING DOUBLE TRIANGLE
    0x23ec,  // (⏬) BLACK DOWN-POINTING DOUBLE TRIANGLE
    0x23ed,  // (⏭) BLACK RIGHT-POINTING DOUBLE TRIANGLE WITH VERTICAL BAR
    0x23ee,  // (⏮) BLACK LEFT-POINTING DOUBLE TRIANGLE WITH VERTICAL BAR
    0x23ef,  // (⏯) BLACK RIGHT-POINTING TRIANGLE WITH DOUBLE VERTICAL BAR
    0x23f0,  // (⏰) ALARM CLOCK
    0x23f1,  // (⏱) STOPWATCH
    0x23f2,  // (⏲) TIMER CLOCK
    0x23f3,  // (⏳) HOURGLASS WITH FLOWING SAND
    0x23f4,  // (⏴) BLACK MEDIUM LEFT-POINTING TRIANGLE
    0x23f5,  // (⏵) BLACK MEDIUM RIGHT-POINTING TRIANGLE
    0x23f6,  // (⏶) BLACK MEDIUM UP-POINTING TRIANGLE
    0x23f7,  // (⏷) BLACK MEDIUM DOWN-POINTING TRIANGLE
    0x23f8,  // (⏸) DOUBLE VERTICAL BAR
    0x23f9,  // (⏹) BLACK SQUARE FOR STOP
    0x23fa,  // (⏺) BLACK CIRCLE FOR RECORD
    0x23fb,  // (⏻) POWER SYMBOL
    0x23fc,  // (⏼) POWER ON-OFF SYMBOL
    0x23fd,  // (⏽) POWER ON SYMBOL
    0x23fe,  // (⏾) POWER SLEEP SYMBOL
    0x23ff,  // (⏿) OBSERVER EYE SYMBOL

    // Geometric shapes
    // Square size: 25A0 > 25FC > 25FE > 25AA

    0x25a0,  // (■) BLACK SQUARE
    0x25a1,  // (□) WHITE SQUARE
    0x25a2,  // (▢) WHITE SQUARE WITH ROUNDED CORNERS
    0x25a3,  // (▣) WHITE SQUARE CONTAINING BLACK SMALL SQUARE
    0x25a4,  // (▤) SQUARE WITH HORIZONTAL FILL
    0x25a5,  // (▥) SQUARE WITH VERTICAL FILL
    0x25a6,  // (▦) SQUARE WITH ORTHOGONAL CROSSHATCH FILL
    0x25a7,  // (▧) SQUARE WITH UPPER LEFT TO LOWER RIGHT FILL
    0x25a8,  // (▨) SQUARE WITH UPPER RIGHT TO LOWER LEFT FILL
    0x25a9,  // (▩) SQUARE WITH DIAGONAL CROSSHATCH FILL
    0x25aa,  // (▪) BLACK SMALL SQUARE
    0x25ab,  // (▫) WHITE SMALL SQUARE
    0x25ac,  // (▬) BLACK RECTANGLE
    0x25ad,  // (▭) WHITE RECTANGLE
    0x25ae,  // (▮) BLACK VERTICAL RECTANGLE
    0x25af,  // (▯) WHITE VERTICAL RECTANGLE
    0x25b0,  // (▰) BLACK PARALLELOGRAM
    0x25b1,  // (▱) WHITE PARALLELOGRAM
    0x25b2,  // (▲) BLACK UP-POINTING TRIANGLE
    0x25b3,  // (△) WHITE UP-POINTING TRIANGLE
    0x25b4,  // (▴) BLACK UP-POINTING SMALL TRIANGLE
    0x25b5,  // (▵) WHITE UP-POINTING SMALL TRIANGLE
    0x25b6,  // (▶) BLACK RIGHT-POINTING TRIANGLE
    0x25b7,  // (▷) WHITE RIGHT-POINTING TRIANGLE
    0x25b8,  // (▸) BLACK RIGHT-POINTING SMALL TRIANGLE
    0x25b9,  // (▹) WHITE RIGHT-POINTING SMALL TRIANGLE
    0x25ba,  // (►) BLACK RIGHT-POINTING POINTER
    0x25bb,  // (▻) WHITE RIGHT-POINTING POINTER
    0x25bc,  // (▼) BLACK DOWN-POINTING TRIANGLE
    0x25bd,  // (▽) WHITE DOWN-POINTING TRIANGLE
    0x25be,  // (▾) BLACK DOWN-POINTING SMALL TRIANGLE
    0x25bf,  // (▿) WHITE DOWN-POINTING SMALL TRIANGLE
    0x25c0,  // (◀) BLACK LEFT-POINTING TRIANGLE
    0x25c1,  // (◁) WHITE LEFT-POINTING TRIANGLE
    0x25c2,  // (◂) BLACK LEFT-POINTING SMALL TRIANGLE
    0x25c3,  // (◃) WHITE LEFT-POINTING SMALL TRIANGLE
    0x25c4,  // (◄) BLACK LEFT-POINTING POINTER
    0x25c5,  // (◅) WHITE LEFT-POINTING POINTER
    0x25c6,  // (◆) BLACK DIAMOND
    0x25c7,  // (◇) WHITE DIAMOND
    0x25c8,  // (◈) WHITE DIAMOND CONTAINING BLACK SMALL DIAMOND
    0x25c9,  // (◉) FISHEYE
    0x25ca,  // (◊) LOZENGE
    0x25cb,  // (○) WHITE CIRCLE
    0x25cc,  // (◌) DOTTED CIRCLE
    0x25cd,  // (◍) CIRCLE WITH VERTICAL FILL
    0x25ce,  // (◎) BULLSEYE
    0x25cf,  // (●) BLACK CIRCLE
    0x25d0,  // (◐) CIRCLE WITH LEFT HALF BLACK
    0x25d1,  // (◑) CIRCLE WITH RIGHT HALF BLACK
    0x25d2,  // (◒) CIRCLE WITH LOWER HALF BLACK
    0x25d3,  // (◓) CIRCLE WITH UPPER HALF BLACK
    0x25d4,  // (◔) CIRCLE WITH UPPER RIGHT QUADRANT BLACK
    0x25d5,  // (◕) CIRCLE WITH ALL BUT UPPER LEFT QUADRANT BLACK
    0x25d6,  // (◖) LEFT HALF BLACK CIRCLE
    0x25d7,  // (◗) RIGHT HALF BLACK CIRCLE
    0x25d8,  // (◘) INVERSE BULLET
    0x25d9,  // (◙) INVERSE WHITE CIRCLE
    0x25da,  // (◚) UPPER HALF INVERSE WHITE CIRCLE
    0x25db,  // (◛) LOWER HALF INVERSE WHITE CIRCLE
    0x25dc,  // (◜) UPPER LEFT QUADRANT CIRCULAR ARC
    0x25dd,  // (◝) UPPER RIGHT QUADRANT CIRCULAR ARC
    0x25de,  // (◞) LOWER RIGHT QUADRANT CIRCULAR ARC
    0x25df,  // (◟) LOWER LEFT QUADRANT CIRCULAR ARC
    0x25e0,  // (◠) UPPER HALF CIRCLE
    0x25e1,  // (◡) LOWER HALF CIRCLE
    0x25e2,  // (◢) BLACK LOWER RIGHT TRIANGLE
    0x25e3,  // (◣) BLACK LOWER LEFT TRIANGLE
    0x25e4,  // (◤) BLACK UPPER LEFT TRIANGLE
    0x25e5,  // (◥) BLACK UPPER RIGHT TRIANGLE
    0x25e6,  // (◦) WHITE BULLET
    0x25e7,  // (◧) SQUARE WITH LEFT HALF BLACK
    0x25e8,  // (◨) SQUARE WITH RIGHT HALF BLACK
    0x25e9,  // (◩) SQUARE WITH UPPER LEFT DIAGONAL HALF BLACK
    0x25ea,  // (◪) SQUARE WITH LOWER RIGHT DIAGONAL HALF BLACK
    0x25eb,  // (◫) WHITE SQUARE WITH VERTICAL BISECTING LINE
    0x25ec,  // (◬) WHITE UP-POINTING TRIANGLE WITH DOT
    0x25ed,  // (◭) UP-POINTING TRIANGLE WITH LEFT HALF BLACK
    0x25ee,  // (◮) UP-POINTING TRIANGLE WITH RIGHT HALF BLACK
    0x25ef,  // (◯) LARGE CIRCLE
    0x25f0,  // (◰) WHITE SQUARE WITH UPPER LEFT QUADRANT
    0x25f1,  // (◱) WHITE SQUARE WITH LOWER LEFT QUADRANT
    0x25f2,  // (◲) WHITE SQUARE WITH LOWER RIGHT QUADRANT
    0x25f3,  // (◳) WHITE SQUARE WITH UPPER RIGHT QUADRANT
    0x25f4,  // (◴) WHITE CIRCLE WITH UPPER LEFT QUADRANT
    0x25f5,  // (◵) WHITE CIRCLE WITH LOWER LEFT QUADRANT
    0x25f6,  // (◶) WHITE CIRCLE WITH LOWER RIGHT QUADRANT
    0x25f7,  // (◷) WHITE CIRCLE WITH UPPER RIGHT QUADRANT
    0x25f8,  // (◸) UPPER LEFT TRIANGLE
    0x25f9,  // (◹) UPPER RIGHT TRIANGLE
    0x25fa,  // (◺) LOWER LEFT TRIANGLE
    0x25fb,  // (◻) WHITE MEDIUM SQUARE
    0x25fc,  // (◼) BLACK MEDIUM SQUARE
    0x25fd,  // (◽) WHITE MEDIUM SMALL SQUARE
    0x25fe,  // (◾) BLACK MEDIUM SMALL SQUARE
    0x25ff,  // (◿) LOWER RIGHT TRIANGLE

    // Supplemental Arrows-A
    0x27F0,  // (⟰) UPWARDS QUADRUPLE ARROW
    0x27F1,  // (⟱) DOWNWARDS QUADRUPLE ARROW
    0x27F2,  // (⟲) ANTICLOCKWISE GAPPED CIRCLE ARROW
    0x27F3,  // (⟳) CLOCKWISE GAPPED CIRCLE ARROW
    0x27F4,  // (⟴) RIGHT ARROW WITH CIRCLED PLUS
    0x27F5,  // (⟵) LONG LEFTWARDS ARROW
    0x27F6,  // (⟶) LONG RIGHTWARDS ARROW
    0x27F7,  // (⟷) LONG LEFT RIGHT ARROW
    0x27F8,  // (⟸) LONG LEFTWARDS DOUBLE ARROW
    0x27F9,  // (⟹) LONG RIGHTWARDS DOUBLE ARROW
    0x27FA,  // (⟺) LONG LEFT RIGHT DOUBLE ARROW
    0x27FB,  // (⟻) LONG LEFTWARDS ARROW FROM BAR
    0x27FC,  // (⟼) LONG RIGHTWARDS ARROW FROM BAR
    0x27FD,  // (⟽) LONG LEFTWARDS DOUBLE ARROW FROM BAR
    0x27FE,  // (⟾) LONG RIGHTWARDS DOUBLE ARROW FROM BAR
    0x27FF,  // (⟿) LONG RIGHTWARDS SQUIGGLE ARROW

    // Free to a good home
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  };

  CHECK(info.codepoints.size() == 16 * 24) << info.codepoints.size();
  return info;
};

static PageInfo PageBit7MathFonts() {
  // 996 symbols in
  // https://www.compart.com/en/unicode/block/U+1D400
  // Just a subset here:
  //   - We skip all the italic forms.
  //   - Homoglyphs are copied
  //   - No Sans/Serif variants
  PageInfo info;

  auto AddSection = [&info](size_t n) {
      size_t start_pos = 0;
      if (!info.sections.empty()) {
        const auto &[ss, nn] = info.sections.back();
        start_pos = ss + nn;
      }
      info.sections.emplace_back(start_pos, n);
    };
  auto Add = [&](const std::initializer_list<uint32_t> &cps) {
      AddSection(cps.size());
      for (uint32_t cp : cps) info.codepoints.push_back(cp);
    };

  // bold A-Z, a-z
  AddSection(52);
  for (int i = 0; i < 52; i++) {
    info.codepoints.push_back(0x1D400 + i);
  }

  // Bold digits 0-9.
  AddSection(10);
  for (int d = 0; d < 10; d++)
    info.codepoints.push_back(0x1D7CE + d);

  // Bold Greek letters and digits.
  Add({
    0x1D6AA,  // (𝚪) Mathematical Bold Capital Gamma
    0x1D6AB,  // (𝚫) Mathematical Bold Capital Delta
    0x1D6AF,  // (𝚯) Mathematical Bold Capital Theta
    0x1D6B2,  // (𝚲) Mathematical Bold Capital Lamda
    0x1D6B5,  // (𝚵) Mathematical Bold Capital Xi
    0x1D6B7,  // (𝚷) Mathematical Bold Capital Pi
    0x1D6B9,  // (𝚹) Mathematical Bold Capital Theta Symbol
    0x1D6BA,  // (𝚺) Mathematical Bold Capital Sigma
    0x1D6BC,  // (𝚼) Mathematical Bold Capital Upsilon
    0x1D6BD,  // (𝚽) Mathematical Bold Capital Phi
    0x1D6BF,  // (𝚿) Mathematical Bold Capital Psi
    0x1D6C0,  // (𝛀) Mathematical Bold Capital Omega
    0x1D6C1,  // (𝛁) Mathematical Bold Nabla
    0x1D6C2,  // (𝛂) Mathematical Bold Small Alpha
    0x1D6C3,  // (𝛃) Mathematical Bold Small Beta
    0x1D6C4,  // (𝛄) Mathematical Bold Small Gamma
    0x1D6C5,  // (𝛅) Mathematical Bold Small Delta
    0x1D6C6,  // (𝛆) Mathematical Bold Small Epsilon
    0x1D6C7,  // (𝛇) Mathematical Bold Small Zeta
    0x1D6C8,  // (𝛈) Mathematical Bold Small Eta
    0x1D6C9,  // (𝛉) Mathematical Bold Small Theta
    0x1D6CA,  // (𝛊) Mathematical Bold Small Iota
    0x1D6CB,  // (𝛋) Mathematical Bold Small Kappa
    0x1D6CC,  // (𝛌) Mathematical Bold Small Lamda
    0x1D6CD,  // (𝛍) Mathematical Bold Small Mu
    0x1D6CE,  // (𝛎) Mathematical Bold Small Nu
    0x1D6CF,  // (𝛏) Mathematical Bold Small Xi
    0x1D6D1,  // (𝛑) Mathematical Bold Small Pi
    0x1D6D2,  // (𝛒) Mathematical Bold Small Rho
    0x1D6D3,  // (𝛓) Mathematical Bold Small Final Sigma
    0x1D6D4,  // (𝛔) Mathematical Bold Small Sigma
    0x1D6D5,  // (𝛕) Mathematical Bold Small Tau
    0x1D6D6,  // (𝛖) Mathematical Bold Small Upsilon
    0x1D6D7,  // (𝛗) Mathematical Bold Small Phi
    0x1D6D8,  // (𝛘) Mathematical Bold Small Chi
    0x1D6D9,  // (𝛙) Mathematical Bold Small Psi
    0x1D6DA,  // (𝛚) Mathematical Bold Small Omega
    0x1D6DB,  // (𝛛) Mathematical Bold Partial Differential
    0x1D6DC,  // (𝛜) Mathematical Bold Epsilon Symbol
    0x1D6DD,  // (𝛝) Mathematical Bold Theta Symbol
    0x1D6DE,  // (𝛞) Mathematical Bold Kappa Symbol
    0x1D6DF,  // (𝛟) Mathematical Bold Phi Symbol
    0x1D6E0,  // (𝛠) Mathematical Bold Rho Symbol
    0x1D6E1,  // (𝛡) Mathematical Bold Pi Symbol
    0x1D7CA,  // (𝟊) Mathematical Bold Capital Digamma
    0x1D7CB,  // (𝟊) Mathematical Bold Small Digamma
    });

  // Careful: script has holes, since some of these are
  // already present in e.g. letterlike forms.
  Add({
    0x1D49C,  // (𝒜) Mathematical Script Capital A
    0x1D49E,  // (𝒞) Mathematical Script Capital C
    0x1D49F,  // (𝒟) Mathematical Script Capital D
    0x1D4A2,  // (𝒢) Mathematical Script Capital G
    0x1D4A5,  // (𝒥) Mathematical Script Capital J
    0x1D4A6,  // (𝒦) Mathematical Script Capital K
    0x1D4A9,  // (𝒩) Mathematical Script Capital N
    0x1D4AA,  // (𝒪) Mathematical Script Capital O
    0x1D4AB,  // (𝒫) Mathematical Script Capital P
    0x1D4AC,  // (𝒬) Mathematical Script Capital Q
    0x1D4AE,  // (𝒮) Mathematical Script Capital S
    0x1D4AF,  // (𝒯) Mathematical Script Capital T
    0x1D4B0,  // (𝒰) Mathematical Script Capital U
    0x1D4B1,  // (𝒱) Mathematical Script Capital V
    0x1D4B2,  // (𝒲) Mathematical Script Capital W
    0x1D4B3,  // (𝒳) Mathematical Script Capital X
    0x1D4B4,  // (𝒴) Mathematical Script Capital Y
    0x1D4B5,  // (𝒵) Mathematical Script Capital Z
    0x1D4B6,  // (𝒶) Mathematical Script Small A
    0x1D4B7,  // (𝒷) Mathematical Script Small B
    0x1D4B8,  // (𝒸) Mathematical Script Small C
    0x1D4B9,  // (𝒹) Mathematical Script Small D
    0x1D4BB,  // (𝒻) Mathematical Script Small F
    0x1D4BD,  // (𝒽) Mathematical Script Small H
    0x1D4BE,  // (𝒾) Mathematical Script Small I
    0x1D4BF,  // (𝒿) Mathematical Script Small J
    0x1D4C0,  // (𝓀) Mathematical Script Small K
    0x1D4C1,  // (𝓁) Mathematical Script Small L
    0x1D4C2,  // (𝓂) Mathematical Script Small M
    0x1D4C3,  // (𝓃) Mathematical Script Small N
    0x1D4C5,  // (𝓅) Mathematical Script Small P
    0x1D4C6,  // (𝓆) Mathematical Script Small Q
    0x1D4C7,  // (𝓇) Mathematical Script Small R
    0x1D4C8,  // (𝓈) Mathematical Script Small S
    0x1D4C9,  // (𝓉) Mathematical Script Small T
    0x1D4CA,  // (𝓊) Mathematical Script Small U
    0x1D4CB,  // (𝓋) Mathematical Script Small V
    0x1D4CC,  // (𝓌) Mathematical Script Small W
    0x1D4CD,  // (𝓍) Mathematical Script Small X
    0x1D4CE,  // (𝓎) Mathematical Script Small Y
    0x1D4CF,  // (𝓏) Mathematical Script Small Z
    });

  // Fraktur also has holes:
  Add({
    0x1D504,  // (𝔄) Mathematical Fraktur Capital A
    0x1D505,  // (𝔅) Mathematical Fraktur Capital B
    0x1D507,  // (𝔇) Mathematical Fraktur Capital D
    0x1D508,  // (𝔈) Mathematical Fraktur Capital E
    0x1D509,  // (𝔉) Mathematical Fraktur Capital F
    0x1D50A,  // (𝔊) Mathematical Fraktur Capital G
    0x1D50D,  // (𝔍) Mathematical Fraktur Capital J
    0x1D50E,  // (𝔎) Mathematical Fraktur Capital K
    0x1D50F,  // (𝔏) Mathematical Fraktur Capital L
    0x1D510,  // (𝔐) Mathematical Fraktur Capital M
    0x1D511,  // (𝔑) Mathematical Fraktur Capital N
    0x1D512,  // (𝔒) Mathematical Fraktur Capital O
    0x1D513,  // (𝔓) Mathematical Fraktur Capital P
    0x1D514,  // (𝔔) Mathematical Fraktur Capital Q
    0x1D516,  // (𝔖) Mathematical Fraktur Capital S
    0x1D517,  // (𝔗) Mathematical Fraktur Capital T
    0x1D518,  // (𝔘) Mathematical Fraktur Capital U
    0x1D519,  // (𝔙) Mathematical Fraktur Capital V
    0x1D51A,  // (𝔚) Mathematical Fraktur Capital W
    0x1D51B,  // (𝔛) Mathematical Fraktur Capital X
    0x1D51C,  // (𝔜) Mathematical Fraktur Capital Y
    0x1D51E,  // (𝔞) Mathematical Fraktur Small A
    0x1D51F,  // (𝔟) Mathematical Fraktur Small B
    0x1D520,  // (𝔠) Mathematical Fraktur Small C
    0x1D521,  // (𝔡) Mathematical Fraktur Small D
    0x1D522,  // (𝔢) Mathematical Fraktur Small E
    0x1D523,  // (𝔣) Mathematical Fraktur Small F
    0x1D524,  // (𝔤) Mathematical Fraktur Small G
    0x1D525,  // (𝔥) Mathematical Fraktur Small H
    0x1D526,  // (𝔦) Mathematical Fraktur Small I
    0x1D527,  // (𝔧) Mathematical Fraktur Small J
    0x1D528,  // (𝔨) Mathematical Fraktur Small K
    0x1D529,  // (𝔩) Mathematical Fraktur Small L
    0x1D52A,  // (𝔪) Mathematical Fraktur Small M
    0x1D52B,  // (𝔫) Mathematical Fraktur Small N
    0x1D52C,  // (𝔬) Mathematical Fraktur Small O
    0x1D52D,  // (𝔭) Mathematical Fraktur Small P
    0x1D52E,  // (𝔮) Mathematical Fraktur Small Q
    0x1D52F,  // (𝔯) Mathematical Fraktur Small R
    0x1D530,  // (𝔰) Mathematical Fraktur Small S
    0x1D531,  // (𝔱) Mathematical Fraktur Small T
    0x1D532,  // (𝔲) Mathematical Fraktur Small U
    0x1D533,  // (𝔳) Mathematical Fraktur Small V
    0x1D534,  // (𝔴) Mathematical Fraktur Small W
    0x1D535,  // (𝔵) Mathematical Fraktur Small X
    0x1D536,  // (𝔶) Mathematical Fraktur Small Y
    0x1D537,  // (𝔷) Mathematical Fraktur Small Z
    });

  // And so too for double-struck "blackboard bold" letters.
  Add({
    0x1D538,  // (𝔸) Mathematical Double-Struck Capital A
    0x1D539,  // (𝔹) Mathematical Double-Struck Capital B
    0x1D53B,  // (𝔻) Mathematical Double-Struck Capital D
    0x1D53C,  // (𝔼) Mathematical Double-Struck Capital E
    0x1D53D,  // (𝔽) Mathematical Double-Struck Capital F
    0x1D53E,  // (𝔾) Mathematical Double-Struck Capital G
    0x1D540,  // (𝕀) Mathematical Double-Struck Capital I
    0x1D541,  // (𝕁) Mathematical Double-Struck Capital J
    0x1D542,  // (𝕂) Mathematical Double-Struck Capital K
    0x1D543,  // (𝕃) Mathematical Double-Struck Capital L
    0x1D544,  // (𝕄) Mathematical Double-Struck Capital M
    0x1D546,  // (𝕆) Mathematical Double-Struck Capital O
    0x1D54A,  // (𝕊) Mathematical Double-Struck Capital S
    0x1D54B,  // (𝕋) Mathematical Double-Struck Capital T
    0x1D54C,  // (𝕌) Mathematical Double-Struck Capital U
    0x1D54D,  // (𝕍) Mathematical Double-Struck Capital V
    0x1D54E,  // (𝕎) Mathematical Double-Struck Capital W
    0x1D54F,  // (𝕏) Mathematical Double-Struck Capital X
    0x1D550,  // (𝕐) Mathematical Double-Struck Capital Y
    0x1D552,  // (𝕒) Mathematical Double-Struck Small A
    0x1D553,  // (𝕓) Mathematical Double-Struck Small B
    0x1D554,  // (𝕔) Mathematical Double-Struck Small C
    0x1D555,  // (𝕕) Mathematical Double-Struck Small D
    0x1D556,  // (𝕖) Mathematical Double-Struck Small E
    0x1D557,  // (𝕗) Mathematical Double-Struck Small F
    0x1D558,  // (𝕘) Mathematical Double-Struck Small G
    0x1D559,  // (𝕙) Mathematical Double-Struck Small H
    0x1D55A,  // (𝕚) Mathematical Double-Struck Small I
    0x1D55B,  // (𝕛) Mathematical Double-Struck Small J
    0x1D55C,  // (𝕜) Mathematical Double-Struck Small K
    0x1D55D,  // (𝕝) Mathematical Double-Struck Small L
    0x1D55E,  // (𝕞) Mathematical Double-Struck Small M
    0x1D55F,  // (𝕟) Mathematical Double-Struck Small N
    0x1D560,  // (𝕠) Mathematical Double-Struck Small O
    0x1D561,  // (𝕡) Mathematical Double-Struck Small P
    0x1D562,  // (𝕢) Mathematical Double-Struck Small Q
    0x1D563,  // (𝕣) Mathematical Double-Struck Small R
    0x1D564,  // (𝕤) Mathematical Double-Struck Small S
    0x1D565,  // (𝕥) Mathematical Double-Struck Small T
    0x1D566,  // (𝕦) Mathematical Double-Struck Small U
    0x1D567,  // (𝕧) Mathematical Double-Struck Small V
    0x1D568,  // (𝕨) Mathematical Double-Struck Small W
    0x1D569,  // (𝕩) Mathematical Double-Struck Small X
    0x1D56A,  // (𝕪) Mathematical Double-Struck Small Y
    0x1D56B,  // (𝕫) Mathematical Double-Struck Small Z
    });

  // Double-struck digits 0-9.
  AddSection(10);
  for (int d = 0; d < 10; d++)
    info.codepoints.push_back(0x1D7D8 + d);

  while (info.codepoints.size() < 16 * 24) info.codepoints.push_back(-1);

  CHECK(info.codepoints.size() == 16 * 24) << info.codepoints.size();
  return info;
}

static PageInfo GetPageInfo(Page p) {
  switch (p) {
  case Page::BIT7_CLASSIC: return PageBit7Classic();
  case Page::BIT7_LATINABC: return PageBit7LatinABC();
  case Page::BIT7_EXTENDED: return PageBit7Extended();
  case Page::BIT7_EXTENDED2: return PageBit7Extended2();
  case Page::BIT7_CYRILLIC: return PageBit7Cyrillic();
  case Page::BIT7_MATH: return PageBit7Math();
  case Page::BIT7_SYM1: return PageBit7Sym1();
  case Page::BIT7_MATHFONTS: return PageBit7MathFonts();
  }
  LOG(FATAL) << "Unimplemented page!";
}

// e.g. use the glyph for hyphen (0x2D) to render U+2212 (minus).
// Both source and destination are Unicode codepoints.
static constexpr std::initializer_list<std::pair<int, int>>
REUSE_FOR = {
  // hyphen-minus used as hyphen or minus
  {'-', 0x2010},
  {'-', 0x2212},
  // And non-breaking hyphen.
  {'-', 0x2011},

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
  {'j', 0x03F3},

  // Coptic homoglyphs
  {'C', 0x2CA4},
  {'c', 0x2CA5},
  {'O', 0x2C9E},
  {'o', 0x2C9F},

  // Greek numeral sign is "based on" modifier letter prime.
  {0x02B9, 0x0374},
  // Similarly, Greek tonos is an accute accent.
  {0x02CA, 0x0384},
  {0x00B7, 0x0387},  // middle dot -> ano teleia

  // Modifier Letters remapped to standalone accents
  {0x02CA, 0x00B4},  // acute accent
  {0x02CB, 0x0060},  // grave

  // "equal and parallel to" is like an octothorpe
  {'#', 0x22D5},

  // Greek -> Math
  {0x03A0, 0x220F},  // Pi -> Product
  {0x03A3, 0x2211},  // Sigma -> Sum

  // Greek -> Latin
  {0x03D5, 0x0278},  // closed phi
  {0x03B2, 0xA7B5},  // beta
  {0x03C9, 0xA7B7},  // small omega
  {0x03A9, 0xA7B6},  // capital omega
  {0x03B3, 0x0263},  // gamma
  {0x03B9, 0x0269},  // iota
  {0x03BB, 0xA7DB},  // lambda

  // Latin Ext A -> Greek
  {0x0178, 0x03AB},  // Ÿ
  {0x00CF, 0x03AA},  // Ï
  {0x00F3, 0x03CC},  // ó

  // "Prohibited sign" is identical to "No entry sign" but
  // black instead of red.
  {0x1F6AB, 0x1F6C7},

  // ISO Latin-1 Macron to overline
  {0x00AF, 0x203E},

  // middle dot -> katakana middle dot
  {0x00B7, 0x30FB},

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
  // Kelvin symbol
  {'K', 0x212A},
  // Ohm symbol from greek Omega
  {0x03A9, 0x2126},
  // "micro" symbol from greek mu
  {0x03BC, 0x00B5},
  // A with circle -> Angstrom
  {0x00C5, 0x212B},
  // Hebrew letters
  {0x05D0, 0x2135},  // Alef
  {0x05D1, 0x2136},  // Bet
  {0x05D2, 0x2137},  // Gimel
  {0x05D3, 0x2138},  // Dalet

  // Some math operators exist in n-ary forms, which look
  // the same.
  {0x2295, 0x2A01},  // oplus
  {0x2279, 0x2A02},  // otimes
  // ... more here.

  // APL symbols have copies of some lowercase greek
  {0x03B9, 0x2373},  // iota
  {0x03C1, 0x2374},  // rho
  {0x03C9, 0x2375},  // omega
  {0x03B1, 0x237A},  // alpha

  // math bold roman -> bold greek homoglyphs
  {0x1D400 + ('A' - 'A'), 0x1D6A8},  // Alpha
  {0x1D400 + ('B' - 'A'), 0x1D6A9},  // Beta
  {0x1D400 + ('E' - 'A'), 0x1D6AC},  // Epsilon
  {0x1D400 + ('Z' - 'A'), 0x1D6AD},  // Zeta
  {0x1D400 + ('H' - 'A'), 0x1D6AE},  // Eta
  {0x1D400 + ('I' - 'A'), 0x1D6B0},  // Iota
  {0x1D400 + ('K' - 'A'), 0x1D6B1},  // Kappa
  {0x1D400 + ('M' - 'A'), 0x1D6B3},  // Mu
  {0x1D400 + ('N' - 'A'), 0x1D6B4},  // Nu
  {0x1D400 + ('O' - 'A'), 0x1D6B6},  // Omicron
  {0x1D400 + ('P' - 'A'), 0x1D6B8},  // Rho
  {0x1D400 + ('T' - 'A'), 0x1D6BB},  // Tau
  {0x1D400 + ('X' - 'A'), 0x1D6BE},  // Chi
  {0x1D400 + ('o' - 'A'), 0x1D6D0},  // omicron

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

static constexpr std::initializer_list<std::pair<int, int>>
MONOSPACE_REUSE_FOR = {
  {'0', 0x1D7F6}, // (𝟶) MATHEMATICAL MONOSPACE DIGIT ZERO
  {'1', 0x1D7F7}, // (𝟷) MATHEMATICAL MONOSPACE DIGIT ONE
  {'2', 0x1D7F8}, // (𝟸) MATHEMATICAL MONOSPACE DIGIT TWO
  {'3', 0x1D7F9}, // (𝟹) MATHEMATICAL MONOSPACE DIGIT THREE
  {'4', 0x1D7FA}, // (𝟺) MATHEMATICAL MONOSPACE DIGIT FOUR
  {'5', 0x1D7FB}, // (𝟻) MATHEMATICAL MONOSPACE DIGIT FIVE
  {'6', 0x1D7FC}, // (𝟼) MATHEMATICAL MONOSPACE DIGIT SIX
  {'7', 0x1D7FD}, // (𝟽) MATHEMATICAL MONOSPACE DIGIT SEVEN
  {'8', 0x1D7FE}, // (𝟾) MATHEMATICAL MONOSPACE DIGIT EIGHT
  {'9', 0x1D7FF}, // (𝟿) MATHEMATICAL MONOSPACE DIGIT NINE
};

// Unicode has width variants for characters that normally
// take double-width, but since all of our glyphs are the
// same width, these can be mapped from the standard codepoints.
// (And also: Vice versa.)
//
// (This is just like REUSE_FOR, but many of these are computed
// programmatically.)
//
// Maps e.g. ASCII 'X' to U+FF3A, the "full-width form" X.
// and 'ト' (normal wide U+30C8 "to") to U+FF84, the half-width
// form for that same katakana. Calls f(src, dst) for each
// pair in the mapping.
template<class F>
static void WidthVariants(const F &f)
  requires requires (uint32_t src, uint32_t dst) { f(src, dst); }
 {
  // ASCII fullwidth forms (FF01-FF5E map from 0021-007E)
  for (uint32_t c = 0x0021; c <= 0x007E; c++) {
    f(c, c + 0xFEE0);
  }

  // Map regular Katakana to half-width.
  static constexpr uint32_t KATAKANA_SRC[61] = {
    0x3002, 0x300C, 0x300D, 0x3001, 0x30FB, 0x30F2, 0x30A1, 0x30A3,
    0x30A5, 0x30A7, 0x30A9, 0x30E3, 0x30E5, 0x30E7, 0x30C3, 0x30FC,
    0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD, 0x30AF,
    0x30B1, 0x30B3, 0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD, 0x30BF,
    0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC, 0x30CD,
    0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30DE, 0x30DF,
    0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9, 0x30EA,
    0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F3,
  };

  for (int idx = 0; idx < 64; idx++) {
    if (uint32_t src = KATAKANA_SRC[idx]) {
      f(src, 0xFF61 + idx);
    }
  }

  // Note: Missing are Hangul characters and a handful of
  // rare symbols.

  // Fullwidth and halfwidth symbol variants
  for (const auto &[src, dst] :
         std::initializer_list<std::pair<uint32_t, uint32_t>>{
      {0x00A2, 0xFFE0}, // FULLWIDTH CENT SIGN
      {0x00A3, 0xFFE1}, // FULLWIDTH POUND SIGN
      {0x00AC, 0xFFE2}, // FULLWIDTH NOT SIGN
      {0x00AF, 0xFFE3}, // FULLWIDTH MACRON
      {0x00A6, 0xFFE4}, // FULLWIDTH BROKEN BAR
      {0x00A5, 0xFFE5}, // FULLWIDTH YEN SIGN
      {0x2502, 0xFFE8}, // HALFWIDTH FORMS LIGHT VERTICAL
      {0x2190, 0xFFE9}, // HALFWIDTH LEFTWARDS ARROW
      {0x2191, 0xFFEA}, // HALFWIDTH UPWARDS ARROW
      {0x2192, 0xFFEB}, // HALFWIDTH RIGHTWARDS ARROW
      {0x2193, 0xFFEC}, // HALFWIDTH DOWNWARDS ARROW
      {0x25A0, 0xFFED}, // HALFWIDTH BLACK SQUARE
      {0x25CB, 0xFFEE}, // HALFWIDTH WHITE CIRCLE
    }) {
    f(src, dst);
  }
 };

// Reads the lines and populates the lines like Util::ReadFileToMap
// does. But allows a multi-line value delimited by [ and ], like
// for bitmaps in braille-on and braille-off.
static std::map<string, string> ReadConfigToMap(std::string_view cfgfile) {
  std::map<string, string> m;
  std::vector<string> lines = Util::ReadFileToLines(cfgfile);
  for (int i = 0; i < (int)lines.size(); i++) {
    string_view rest = lines[i];
    Util::RemoveOuterWhitespace(&rest);
    string_view key = Util::Chop(&rest);

    Util::RemoveOuterWhitespace(&rest);

    // [ ] delimits a multi-line value. We trim the brackets but keep
    // everything else intact.
    if (rest.starts_with('[')) {
      rest.remove_prefix(1);
      string value{rest};
      while (value.find(']') == string::npos && i + 1 < (int)lines.size()) {
        i++;
        value.push_back('\n');
        value.append(lines[i]);
      }
      size_t end_pos = value.find(']');
      CHECK(end_pos != std::string_view::npos) << "Unclosed [ in multi-line "
        "value for key " << key;
      value.resize(end_pos);
      m.emplace(key, std::move(value));
    } else {
      m.emplace(key, rest);
    }
  }
  return m;
}

// value is a multi-line string. Leading and trailing whitespace
// (including newlines) is ignored. Otherwise, each line is a row
// of pixels with @ denoting a 1 pixel and . denoting 0. Spaces
// are allowed and ignored, but anything else is a fatal error.
static Image1 ParseBitmap(std::string_view value) {
  Util::RemoveOuterWhitespace(&value);
  std::vector<std::string> lines = Util::SplitToLines(value);
  int height = lines.size();
  int width = 0;

  for (const std::string &line : lines) {
    int line_width = 0;
    for (char c : line) {
      if (c == ' ' || c == '\t' || c == '\r') {
        continue;
      } else if (c == '@' || c == '.') {
        line_width++;
      } else {
        LOG(FATAL) << "Invalid character in bitmap: '" << c << "'";
      }
    }
    width = std::max(width, line_width);
  }

  Image1 img(width, height);
  for (int y = 0; y < height; y++) {
    int x = 0;
    for (char c : lines[y]) {
      if (c == '@') {
        img.SetPixel(x++, y, true);
      } else if (c == '.') {
        img.SetPixel(x++, y, false);
      } else {
        CHECK(c == ' ');
      }
    }
  }

  return img;
}

Config Config::ParseConfig(std::string_view cfgfile) {
  std::string path = Util::PathOf(cfgfile);

  Config config;
  std::map<string, string> m = ReadConfigToMap(cfgfile);

  CHECK(!m.empty()) << "Couldn't read config file " << cfgfile;
  config.pngfile = Util::DirPlus(path, m["pngfile"]);
  if (m.contains("fallback-pngfile")) {
    config.fallback_pngfile = Util::DirPlus(path, m["fallback-pngfile"]);
  }

  config.family_name = m["family-name"];
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

  auto IsSpace = [](char c) { return c == ' '; };

  std::vector<std::string> pp = Util::Tokens(m["pages"], IsSpace);

  if (m.find("vendor") != m.end()) {
    std::string v = m["vendor"];
    CHECK(v.size() == 4) << "Vendor must be exactly 4 bytes.";
    config.vendor[0] = v[0];
    config.vendor[1] = v[1];
    config.vendor[2] = v[2];
    config.vendor[3] = v[3];
  }

  if (m.contains("ribbi")) {
    std::string r = m["ribbi"];
    if (r == "regular") config.ribbi = RIBBI::REGULAR;
    else if (r == "italic") config.ribbi = RIBBI::ITALIC;
    else if (r == "bold") config.ribbi = RIBBI::BOLD;
    else if (r == "bold-italic") config.ribbi = RIBBI::BOLD_ITALIC;
    else LOG(FATAL) << "Unknown ribbi value: " << r;
  }

  if (m.find("weight") != m.end()) {
    config.weight = atoi(m["weight"].c_str());
  }

  if (m.find("width") != m.end()) {
    config.width = atoi(m["width"].c_str());
  }

  for (const std::string &p : pp) {
    if (!p.empty()) {
      config.pages.push_back(ParsePage(p));
    }
  }

  if (m.find("braille-on") != m.end()) {
    config.braille_on = ParseBitmap(m["braille-on"]);
  }

  if (m.find("braille-off") != m.end()) {
    config.braille_off = ParseBitmap(m["braille-off"]);
  }

  if (m.find("braille-x") != m.end()) {
    for (const std::string &t : Util::Tokens(m["braille-x"], IsSpace)) {
      if (!t.empty()) config.braille_x.push_back(atoi(t.c_str()));
    }
    CHECK(config.braille_x.size() == 2) << "There should be exactly 2 "
      "x positions for braille-x (space-separated integers)";
  }

  if (m.find("braille-y") != m.end()) {
    for (const std::string &t : Util::Tokens(m["braille-y"], IsSpace)) {
      if (!t.empty()) config.braille_y.push_back(atoi(t.c_str()));
    }
    CHECK(config.braille_y.size() == 4) << "There should be exactly 4 "
      "y positions for braille-y (space-separated integers)";
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
          Print("{}: "
                "Character at cx={}, cy={} has no width (black column) but "
                "has a glyph (white pixels).\n",
                config.pngfile, cx, cy);
          CHECK(false);
        }

        continue;
      } else if (width == 0) {
        Print("{}: Character at cx={}, cy={} has zero width; "
              "not supported!\n",
              config.pngfile, cx, cy);
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
  const PageInfo page_info = GetPageInfo(p);
  const std::vector<int> &codepoints = page_info.codepoints;

  for (const auto &[cidx, gidx] : pos_to_glyph) {
    CHECK(gidx >= 0 && gidx < (int)glyphs.size());
    const Glyph &glyph = glyphs[gidx];
    const bool is_empty = EmptyGlyph(glyph);
    const bool ok_missing = config.fixed_width && is_empty;

    if (cidx >= (int)codepoints.size()) {
      if (!ok_missing) {
        Print("Skipping glyph at {},{} because it is outside the codepoint "
              "array for page {}! There are {} codepoints configured.\n",
              cidx % config.chars_across, cidx / config.chars_across,
              Config::PageString(p),
              codepoints.size());
        Print("{}", GlyphString(glyph));
      }
      continue;
    }

    CHECK(cidx >= 0 && cidx < (int)codepoints.size());
    const int codepoint = codepoints[cidx];
    if (codepoint < 0) {
      if (!ok_missing) {
        Print("Skipping glyph at {} = {},{} because the codepoint is not "
              "configured in page {} (it's {})!\n",
              cidx,
              cidx % config.chars_across, cidx / config.chars_across,
              Config::PageString(p), codepoint);
        Print("{}", FontImage::GlyphString(glyph));
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

// If we have images for braille_on and braille_off, and the
// correct number of row and column positions, then compose
// glyphs for the 256 codepoints starting at U+2800; insert
// them in the glyphs array and map them in the codepoint_to_glyph
// map.
void FontImage::ComposeBraille() {
  if (!config.braille_on.has_value() || !config.braille_off.has_value() ||
      config.braille_x.size() != 2 || config.braille_y.size() != 4) {
    return;
  }

  int width = config.charbox_width - config.spacing;
  if (width <= 0) {
    width = config.charbox_width;
  }

  for (int i = 0; i < 256; i++) {
    ImageA glyph(width, config.charbox_height);
    glyph.Clear(0x00);

    for (int dot_idx = 0; dot_idx < 8; dot_idx++) {
      // Note that the bits are arrayed like this:
      // 1 4
      // 2 5
      // 3 6
      // 7 8
      const auto &[row, col] = [dot_idx]() -> std::pair<int, int> {
          switch (dot_idx) {
          default:
          case 0: return {0, 0};
          case 1: return {1, 0};
          case 2: return {2, 0};
          case 3: return {0, 1};
          case 4: return {1, 1};
          case 5: return {2, 1};
          case 6: return {3, 0};
          case 7: return {3, 1};
          }
        }();

      const bool is_on = !!(i & (1 << dot_idx));
      const Image1 &dot = is_on ?
        config.braille_on.value() :
        config.braille_off.value();

      const int dx = config.braille_x[col];
      const int dy = config.braille_y[row];

      for (int y = 0; y < dot.Height(); y++) {
        for (int x = 0; x < dot.Width(); x++) {
          if (dot.GetPixel(x, y)) {
            glyph.SetPixel(dx + x, dy + y, 0xFF);
          }
        }
      }
    }

    const int gidx = (int)glyphs.size();
    glyphs.push_back(Glyph{.left_edge = 0, .pic = std::move(glyph)});
    unicode_to_glyph[0x2800 + i] = gidx;
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

  std::unique_ptr<ImageRGBA> fallback;
  if (!config.fallback_pngfile.empty())
    fallback.reset(ImageRGBA::Load(config.fallback_pngfile));

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
  auto LoadPages = [&](const ImageRGBA &img) {
    for (int page_num = 0; page_num < (int)config.pages.size(); page_num++) {
      const Page &p = config.pages[page_num];
      const int page_x = page_num * spaced_page_width;

      ImageRGBA page_img(page_width, page_height);

      page_img.CopyImageRect(0, 0, img, page_x, 0,
                             page_width, page_height);
      AddPage(page_img, p);
    }
  };

  // First load fallback glyphs.
  if (fallback.get() != nullptr) {
    LoadPages(*fallback);
  }
  // ... and then overwrite any of them with the explicit glyphs from
  // the input.
  LoadPages(*input);

  ComposeBraille();

  // After every page is loaded, fill in any unused codepoints that
  // can be copied from existing ones.
  auto Reuse = [&](uint32_t src, uint32_t dst) {
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
            Print("Copy {:04x} to {:04x}\n", src, dst);
          }
          unicode_to_glyph[dst] = unicode_to_glyph[src];
        }
      }
    };

  for (const auto &[src, dst] : REUSE_FOR) {
    Reuse(src, dst);
  }

  if (config.fixed_width) {
    for (const auto &[src, dst] : MONOSPACE_REUSE_FOR) {
      Reuse(src, dst);
    }
  }


  WidthVariants(Reuse);
}

namespace {
inline static constexpr std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>
Unpack32(uint32_t color) {
  return {(uint8_t)((color >> 24) & 255),
          (uint8_t)((color >> 16) & 255),
          (uint8_t)((color >> 8) & 255),
          (uint8_t)(color & 255)};
}

inline static constexpr uint32_t Pack32(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return ((uint32_t)r << 24) | ((uint32_t)g << 16) |
    ((uint32_t)b << 8) | (uint32_t)a;
}

inline static constexpr uint32_t Darken(uint32_t c) {
  const auto &[r, g, b, a] = Unpack32(c);

  uint8_t rr = r * 0.5;
  uint8_t gg = g * 0.5;
  uint8_t bb = b * 0.5;

  return Pack32(rr, gg, bb, a);
}

struct SectionColors {
  // Anything other than the standard pair 0x828a19FF, 0x594d96FF.
  static constexpr std::array ALT_COLORS = {
    // std::pair<uint32_t, uint32_t>{0x3a5f62ff, 0x6f2f30ff},
    // std::pair<uint32_t, uint32_t>{0x19561aff, 0x623a5dff},
    // brown
    std::pair<uint32_t, uint32_t>{0x6b4b1cff, 0x4b3514ff},
    // blue
    std::pair<uint32_t, uint32_t>{0x142a4bff, 0x1c3c6bff},
    // red
    std::pair<uint32_t, uint32_t>{0x4b1514ff, 0x6b1d1cff},
  };

  SectionColors(const Config &config,
                const PageInfo &info) {
    int size = config.chars_across * config.chars_down;
    colors.resize(size);

    // Pass the bright color; the darker color is computed automatically.
    auto SetColor = [&config, this](int cidx,
                                    uint32_t ecolor, uint32_t ocolor) {
        int x = cidx % config.chars_across;
        int y = cidx / config.chars_across;
        const bool odd = !!((x + y) & 1);

        uint32_t c = odd ? ocolor : ecolor;

        this->colors[cidx] = std::make_pair(c, Darken(c));
      };

    // First set plain background colors.
    for (int cidx = 0; cidx < size; cidx++) {
      SetColor(cidx, 0x828a19FF, 0x594d96FF);
    }

    // Now for each span.
    int ac = 0;
    for (const auto &[start, len] : info.sections) {
      for (int off = 0; off < len; off++) {
        int cidx = start + off;
        CHECK(cidx < size);
        SetColor(cidx, ALT_COLORS[ac].first, ALT_COLORS[ac].second);
      }

      ac ++;
      ac %= ALT_COLORS.size();
    }

  }

  std::vector<std::pair<uint32_t, uint32_t>> colors;
};
}

ImageRGBA FontImage::ImagePage(Page p) {
  const int ww = config.charbox_width;
  const int hh = config.charbox_height;
  ImageRGBA out(config.chars_across * ww, config.chars_down * hh);
  out.Clear32(0xFF0000FF);

  PageInfo page_info = GetPageInfo(p);
  const std::vector<int> &codepoints = page_info.codepoints;

  SectionColors section_colors(config, page_info);

  for (int y = 0; y < config.chars_down; y++) {
    for (int x = 0; x < config.chars_across; x++) {
      const int cidx = y * config.chars_across + x;
      // const bool odd = !!((x + y) & 1);

      const auto &[bgcolor, locolor] =
        section_colors.colors[cidx];

      // Fill whole grid cell to start.
      int xs = x * ww;
      int ys = y * hh;
      out.BlendRect32(xs, ys, ww, hh, bgcolor);
      out.BlendRect32(xs, ys + hh - config.descent,
                      ww, config.descent, locolor);

      int codepoint = -1;
      if (cidx < (int)codepoints.size())
        codepoint = codepoints[cidx];

      const bool combining = codepoint >= 0 && IsCombining(codepoint);
      if (combining) {
        // Draw a shadow glyph, mainly as a hint that this is
        // a combining character, but also to help with designing.
        // TODO: Get from config.
        const int pixel_width = config.charbox_width - config.spacing;
        int sw = pixel_width - 2;
        int xmargin = (pixel_width - sw) / 2;
        int sh = config.charbox_height / 3;
        int ymargin = (config.charbox_height - sh) / 2;
        out.BlendRect32(xs + xmargin, ys + ymargin, sw, sh, locolor);
      }

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
      // Print("{} - {} - {}\n", config.charbox_width, glyph_width, sp);
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
  Config cfg = Config::ParseConfig(configfile);
  FontImage font_image(cfg);
  return std::make_unique<BitmapFont>(std::move(font_image));
}

bool FontImage::IsCombining(uint32_t codepoint) {
  static const std::unordered_set<uint32_t> *c = []{
      return new std::unordered_set<uint32_t>(COMBINING.begin(), COMBINING.end());
  }();
  return (*c).contains(codepoint);
}

// The entire set of combining codepoints dumped here so that we don't need
// to depend on UnicodeData or ICU, etc.
namespace {
const std::array<uint32_t, NUM_COMBINING> COMBINING = {
  0x300, 0x301, 0x302, 0x303, 0x304, 0x305, 0x306, 0x307, 0x308, 0x309, 0x30a,
  0x30b, 0x30c, 0x30d, 0x30e, 0x30f, 0x310, 0x311, 0x312, 0x313, 0x314, 0x315,
  0x316, 0x317, 0x318, 0x319, 0x31a, 0x31b, 0x31c, 0x31d, 0x31e, 0x31f, 0x320,
  0x321, 0x322, 0x323, 0x324, 0x325, 0x326, 0x327, 0x328, 0x329, 0x32a, 0x32b,
  0x32c, 0x32d, 0x32e, 0x32f, 0x330, 0x331, 0x332, 0x333, 0x334, 0x335, 0x336,
  0x337, 0x338, 0x339, 0x33a, 0x33b, 0x33c, 0x33d, 0x33e, 0x33f, 0x340, 0x341,
  0x342, 0x343, 0x344, 0x345, 0x346, 0x347, 0x348, 0x349, 0x34a, 0x34b, 0x34c,
  0x34d, 0x34e, 0x34f, 0x350, 0x351, 0x352, 0x353, 0x354, 0x355, 0x356, 0x357,
  0x358, 0x359, 0x35a, 0x35b, 0x35c, 0x35d, 0x35e, 0x35f, 0x360, 0x361, 0x362,
  0x363, 0x364, 0x365, 0x366, 0x367, 0x368, 0x369, 0x36a, 0x36b, 0x36c, 0x36d,
  0x36e, 0x36f, 0x483, 0x484, 0x485, 0x486, 0x487, 0x488, 0x489, 0x591, 0x592,
  0x593, 0x594, 0x595, 0x596, 0x597, 0x598, 0x599, 0x59a, 0x59b, 0x59c, 0x59d,
  0x59e, 0x59f, 0x5a0, 0x5a1, 0x5a2, 0x5a3, 0x5a4, 0x5a5, 0x5a6, 0x5a7, 0x5a8,
  0x5a9, 0x5aa, 0x5ab, 0x5ac, 0x5ad, 0x5ae, 0x5af, 0x5b0, 0x5b1, 0x5b2, 0x5b3,
  0x5b4, 0x5b5, 0x5b6, 0x5b7, 0x5b8, 0x5b9, 0x5ba, 0x5bb, 0x5bc, 0x5bd, 0x5bf,
  0x5c1, 0x5c2, 0x5c4, 0x5c5, 0x5c7, 0x610, 0x611, 0x612, 0x613, 0x614, 0x615,
  0x616, 0x617, 0x618, 0x619, 0x61a, 0x64b, 0x64c, 0x64d, 0x64e, 0x64f, 0x650,
  0x651, 0x652, 0x653, 0x654, 0x655, 0x656, 0x657, 0x658, 0x659, 0x65a, 0x65b,
  0x65c, 0x65d, 0x65e, 0x65f, 0x670, 0x6d6, 0x6d7, 0x6d8, 0x6d9, 0x6da, 0x6db,
  0x6dc, 0x6df, 0x6e0, 0x6e1, 0x6e2, 0x6e3, 0x6e4, 0x6e7, 0x6e8, 0x6ea, 0x6eb,
  0x6ec, 0x6ed, 0x711, 0x730, 0x731, 0x732, 0x733, 0x734, 0x735, 0x736, 0x737,
  0x738, 0x739, 0x73a, 0x73b, 0x73c, 0x73d, 0x73e, 0x73f, 0x740, 0x741, 0x742,
  0x743, 0x744, 0x745, 0x746, 0x747, 0x748, 0x749, 0x74a, 0x7a6, 0x7a7, 0x7a8,
  0x7a9, 0x7aa, 0x7ab, 0x7ac, 0x7ad, 0x7ae, 0x7af, 0x7b0, 0x7eb, 0x7ec, 0x7ed,
  0x7ee, 0x7ef, 0x7f0, 0x7f1, 0x7f2, 0x7f3, 0x7fd, 0x816, 0x817, 0x818, 0x819,
  0x81b, 0x81c, 0x81d, 0x81e, 0x81f, 0x820, 0x821, 0x822, 0x823, 0x825, 0x826,
  0x827, 0x829, 0x82a, 0x82b, 0x82c, 0x82d, 0x859, 0x85a, 0x85b, 0x897, 0x898,
  0x899, 0x89a, 0x89b, 0x89c, 0x89d, 0x89e, 0x89f, 0x8ca, 0x8cb, 0x8cc, 0x8cd,
  0x8ce, 0x8cf, 0x8d0, 0x8d1, 0x8d2, 0x8d3, 0x8d4, 0x8d5, 0x8d6, 0x8d7, 0x8d8,
  0x8d9, 0x8da, 0x8db, 0x8dc, 0x8dd, 0x8de, 0x8df, 0x8e0, 0x8e1, 0x8e3, 0x8e4,
  0x8e5, 0x8e6, 0x8e7, 0x8e8, 0x8e9, 0x8ea, 0x8eb, 0x8ec, 0x8ed, 0x8ee, 0x8ef,
  0x8f0, 0x8f1, 0x8f2, 0x8f3, 0x8f4, 0x8f5, 0x8f6, 0x8f7, 0x8f8, 0x8f9, 0x8fa,
  0x8fb, 0x8fc, 0x8fd, 0x8fe, 0x8ff, 0x900, 0x901, 0x902, 0x93a, 0x93c, 0x941,
  0x942, 0x943, 0x944, 0x945, 0x946, 0x947, 0x948, 0x94d, 0x951, 0x952, 0x953,
  0x954, 0x955, 0x956, 0x957, 0x962, 0x963, 0x981, 0x9bc, 0x9c1, 0x9c2, 0x9c3,
  0x9c4, 0x9cd, 0x9e2, 0x9e3, 0x9fe, 0xa01, 0xa02, 0xa3c, 0xa41, 0xa42, 0xa47,
  0xa48, 0xa4b, 0xa4c, 0xa4d, 0xa51, 0xa70, 0xa71, 0xa75, 0xa81, 0xa82, 0xabc,
  0xac1, 0xac2, 0xac3, 0xac4, 0xac5, 0xac7, 0xac8, 0xacd, 0xae2, 0xae3, 0xafa,
  0xafb, 0xafc, 0xafd, 0xafe, 0xaff, 0xb01, 0xb3c, 0xb3f, 0xb41, 0xb42, 0xb43,
  0xb44, 0xb4d, 0xb55, 0xb56, 0xb62, 0xb63, 0xb82, 0xbc0, 0xbcd, 0xc00, 0xc04,
  0xc3c, 0xc3e, 0xc3f, 0xc40, 0xc46, 0xc47, 0xc48, 0xc4a, 0xc4b, 0xc4c, 0xc4d,
  0xc55, 0xc56, 0xc62, 0xc63, 0xc81, 0xcbc, 0xcbf, 0xcc6, 0xccc, 0xccd, 0xce2,
  0xce3, 0xd00, 0xd01, 0xd3b, 0xd3c, 0xd41, 0xd42, 0xd43, 0xd44, 0xd4d, 0xd62,
  0xd63, 0xd81, 0xdca, 0xdd2, 0xdd3, 0xdd4, 0xdd6, 0xe31, 0xe34, 0xe35, 0xe36,
  0xe37, 0xe38, 0xe39, 0xe3a, 0xe47, 0xe48, 0xe49, 0xe4a, 0xe4b, 0xe4c, 0xe4d,
  0xe4e, 0xeb1, 0xeb4, 0xeb5, 0xeb6, 0xeb7, 0xeb8, 0xeb9, 0xeba, 0xebb, 0xebc,
  0xec8, 0xec9, 0xeca, 0xecb, 0xecc, 0xecd, 0xece, 0xf18, 0xf19, 0xf35, 0xf37,
  0xf39, 0xf71, 0xf72, 0xf73, 0xf74, 0xf75, 0xf76, 0xf77, 0xf78, 0xf79, 0xf7a,
  0xf7b, 0xf7c, 0xf7d, 0xf7e, 0xf80, 0xf81, 0xf82, 0xf83, 0xf84, 0xf86, 0xf87,
  0xf8d, 0xf8e, 0xf8f, 0xf90, 0xf91, 0xf92, 0xf93, 0xf94, 0xf95, 0xf96, 0xf97,
  0xf99, 0xf9a, 0xf9b, 0xf9c, 0xf9d, 0xf9e, 0xf9f, 0xfa0, 0xfa1, 0xfa2, 0xfa3,
  0xfa4, 0xfa5, 0xfa6, 0xfa7, 0xfa8, 0xfa9, 0xfaa, 0xfab, 0xfac, 0xfad, 0xfae,
  0xfaf, 0xfb0, 0xfb1, 0xfb2, 0xfb3, 0xfb4, 0xfb5, 0xfb6, 0xfb7, 0xfb8, 0xfb9,
  0xfba, 0xfbb, 0xfbc, 0xfc6, 0x102d, 0x102e, 0x102f, 0x1030, 0x1032, 0x1033,
  0x1034, 0x1035, 0x1036, 0x1037, 0x1039, 0x103a, 0x103d, 0x103e, 0x1058, 0x1059,
  0x105e, 0x105f, 0x1060, 0x1071, 0x1072, 0x1073, 0x1074, 0x1082, 0x1085, 0x1086,
  0x108d, 0x109d, 0x135d, 0x135e, 0x135f, 0x1712, 0x1713, 0x1714, 0x1732, 0x1733,
  0x1752, 0x1753, 0x1772, 0x1773, 0x17b4, 0x17b5, 0x17b7, 0x17b8, 0x17b9, 0x17ba,
  0x17bb, 0x17bc, 0x17bd, 0x17c6, 0x17c9, 0x17ca, 0x17cb, 0x17cc, 0x17cd, 0x17ce,
  0x17cf, 0x17d0, 0x17d1, 0x17d2, 0x17d3, 0x17dd, 0x180b, 0x180c, 0x180d, 0x180f,
  0x1885, 0x1886, 0x18a9, 0x1920, 0x1921, 0x1922, 0x1927, 0x1928, 0x1932, 0x1939,
  0x193a, 0x193b, 0x1a17, 0x1a18, 0x1a1b, 0x1a56, 0x1a58, 0x1a59, 0x1a5a, 0x1a5b,
  0x1a5c, 0x1a5d, 0x1a5e, 0x1a60, 0x1a62, 0x1a65, 0x1a66, 0x1a67, 0x1a68, 0x1a69,
  0x1a6a, 0x1a6b, 0x1a6c, 0x1a73, 0x1a74, 0x1a75, 0x1a76, 0x1a77, 0x1a78, 0x1a79,
  0x1a7a, 0x1a7b, 0x1a7c, 0x1a7f, 0x1ab0, 0x1ab1, 0x1ab2, 0x1ab3, 0x1ab4, 0x1ab5,
  0x1ab6, 0x1ab7, 0x1ab8, 0x1ab9, 0x1aba, 0x1abb, 0x1abc, 0x1abd, 0x1abe, 0x1abf,
  0x1ac0, 0x1ac1, 0x1ac2, 0x1ac3, 0x1ac4, 0x1ac5, 0x1ac6, 0x1ac7, 0x1ac8, 0x1ac9,
  0x1aca, 0x1acb, 0x1acc, 0x1acd, 0x1ace, 0x1acf, 0x1ad0, 0x1ad1, 0x1ad2, 0x1ad3,
  0x1ad4, 0x1ad5, 0x1ad6, 0x1ad7, 0x1ad8, 0x1ad9, 0x1ada, 0x1adb, 0x1adc, 0x1add,
  0x1ae0, 0x1ae1, 0x1ae2, 0x1ae3, 0x1ae4, 0x1ae5, 0x1ae6, 0x1ae7, 0x1ae8, 0x1ae9,
  0x1aea, 0x1aeb, 0x1b00, 0x1b01, 0x1b02, 0x1b03, 0x1b34, 0x1b36, 0x1b37, 0x1b38,
  0x1b39, 0x1b3a, 0x1b3c, 0x1b42, 0x1b6b, 0x1b6c, 0x1b6d, 0x1b6e, 0x1b6f, 0x1b70,
  0x1b71, 0x1b72, 0x1b73, 0x1b80, 0x1b81, 0x1ba2, 0x1ba3, 0x1ba4, 0x1ba5, 0x1ba8,
  0x1ba9, 0x1bab, 0x1bac, 0x1bad, 0x1be6, 0x1be8, 0x1be9, 0x1bed, 0x1bef, 0x1bf0,
  0x1bf1, 0x1c2c, 0x1c2d, 0x1c2e, 0x1c2f, 0x1c30, 0x1c31, 0x1c32, 0x1c33, 0x1c36,
  0x1c37, 0x1cd0, 0x1cd1, 0x1cd2, 0x1cd4, 0x1cd5, 0x1cd6, 0x1cd7, 0x1cd8, 0x1cd9,
  0x1cda, 0x1cdb, 0x1cdc, 0x1cdd, 0x1cde, 0x1cdf, 0x1ce0, 0x1ce2, 0x1ce3, 0x1ce4,
  0x1ce5, 0x1ce6, 0x1ce7, 0x1ce8, 0x1ced, 0x1cf4, 0x1cf8, 0x1cf9, 0x1dc0, 0x1dc1,
  0x1dc2, 0x1dc3, 0x1dc4, 0x1dc5, 0x1dc6, 0x1dc7, 0x1dc8, 0x1dc9, 0x1dca, 0x1dcb,
  0x1dcc, 0x1dcd, 0x1dce, 0x1dcf, 0x1dd0, 0x1dd1, 0x1dd2, 0x1dd3, 0x1dd4, 0x1dd5,
  0x1dd6, 0x1dd7, 0x1dd8, 0x1dd9, 0x1dda, 0x1ddb, 0x1ddc, 0x1ddd, 0x1dde, 0x1ddf,
  0x1de0, 0x1de1, 0x1de2, 0x1de3, 0x1de4, 0x1de5, 0x1de6, 0x1de7, 0x1de8, 0x1de9,
  0x1dea, 0x1deb, 0x1dec, 0x1ded, 0x1dee, 0x1def, 0x1df0, 0x1df1, 0x1df2, 0x1df3,
  0x1df4, 0x1df5, 0x1df6, 0x1df7, 0x1df8, 0x1df9, 0x1dfa, 0x1dfb, 0x1dfc, 0x1dfd,
  0x1dfe, 0x1dff, 0x20d0, 0x20d1, 0x20d2, 0x20d3, 0x20d4, 0x20d5, 0x20d6, 0x20d7,
  0x20d8, 0x20d9, 0x20da, 0x20db, 0x20dc, 0x20dd, 0x20de, 0x20df, 0x20e0, 0x20e1,
  0x20e2, 0x20e3, 0x20e4, 0x20e5, 0x20e6, 0x20e7, 0x20e8, 0x20e9, 0x20ea, 0x20eb,
  0x20ec, 0x20ed, 0x20ee, 0x20ef, 0x20f0, 0x2cef, 0x2cf0, 0x2cf1, 0x2d7f, 0x2de0,
  0x2de1, 0x2de2, 0x2de3, 0x2de4, 0x2de5, 0x2de6, 0x2de7, 0x2de8, 0x2de9, 0x2dea,
  0x2deb, 0x2dec, 0x2ded, 0x2dee, 0x2def, 0x2df0, 0x2df1, 0x2df2, 0x2df3, 0x2df4,
  0x2df5, 0x2df6, 0x2df7, 0x2df8, 0x2df9, 0x2dfa, 0x2dfb, 0x2dfc, 0x2dfd, 0x2dfe,
  0x2dff, 0x302a, 0x302b, 0x302c, 0x302d, 0x3099, 0x309a, 0xa66f, 0xa670, 0xa671,
  0xa672, 0xa674, 0xa675, 0xa676, 0xa677, 0xa678, 0xa679, 0xa67a, 0xa67b, 0xa67c,
  0xa67d, 0xa69e, 0xa69f, 0xa6f0, 0xa6f1, 0xa802, 0xa806, 0xa80b, 0xa825, 0xa826,
  0xa82c, 0xa8c4, 0xa8c5, 0xa8e0, 0xa8e1, 0xa8e2, 0xa8e3, 0xa8e4, 0xa8e5, 0xa8e6,
  0xa8e7, 0xa8e8, 0xa8e9, 0xa8ea, 0xa8eb, 0xa8ec, 0xa8ed, 0xa8ee, 0xa8ef, 0xa8f0,
  0xa8f1, 0xa8ff, 0xa926, 0xa927, 0xa928, 0xa929, 0xa92a, 0xa92b, 0xa92c, 0xa92d,
  0xa947, 0xa948, 0xa949, 0xa94a, 0xa94b, 0xa94c, 0xa94d, 0xa94e, 0xa94f, 0xa950,
  0xa951, 0xa980, 0xa981, 0xa982, 0xa9b3, 0xa9b6, 0xa9b7, 0xa9b8, 0xa9b9, 0xa9bc,
  0xa9bd, 0xa9e5, 0xaa29, 0xaa2a, 0xaa2b, 0xaa2c, 0xaa2d, 0xaa2e, 0xaa31, 0xaa32,
  0xaa35, 0xaa36, 0xaa43, 0xaa4c, 0xaa7c, 0xaab0, 0xaab2, 0xaab3, 0xaab4, 0xaab7,
  0xaab8, 0xaabe, 0xaabf, 0xaac1, 0xaaec, 0xaaed, 0xaaf6, 0xabe5, 0xabe8, 0xabed,
  0xfb1e, 0xfe00, 0xfe01, 0xfe02, 0xfe03, 0xfe04, 0xfe05, 0xfe06, 0xfe07, 0xfe08,
  0xfe09, 0xfe0a, 0xfe0b, 0xfe0c, 0xfe0d, 0xfe0e, 0xfe0f, 0xfe20, 0xfe21, 0xfe22,
  0xfe23, 0xfe24, 0xfe25, 0xfe26, 0xfe27, 0xfe28, 0xfe29, 0xfe2a, 0xfe2b, 0xfe2c,
  0xfe2d, 0xfe2e, 0xfe2f, 0x101fd, 0x102e0, 0x10376, 0x10377, 0x10378, 0x10379,
  0x1037a, 0x10a01, 0x10a02, 0x10a03, 0x10a05, 0x10a06, 0x10a0c, 0x10a0d, 0x10a0e,
  0x10a0f, 0x10a38, 0x10a39, 0x10a3a, 0x10a3f, 0x10ae5, 0x10ae6, 0x10d24, 0x10d25,
  0x10d26, 0x10d27, 0x10d69, 0x10d6a, 0x10d6b, 0x10d6c, 0x10d6d, 0x10eab, 0x10eac,
  0x10efa, 0x10efb, 0x10efc, 0x10efd, 0x10efe, 0x10eff, 0x10f46, 0x10f47, 0x10f48,
  0x10f49, 0x10f4a, 0x10f4b, 0x10f4c, 0x10f4d, 0x10f4e, 0x10f4f, 0x10f50, 0x10f82,
  0x10f83, 0x10f84, 0x10f85, 0x11001, 0x11038, 0x11039, 0x1103a, 0x1103b, 0x1103c,
  0x1103d, 0x1103e, 0x1103f, 0x11040, 0x11041, 0x11042, 0x11043, 0x11044, 0x11045,
  0x11046, 0x11070, 0x11073, 0x11074, 0x1107f, 0x11080, 0x11081, 0x110b3, 0x110b4,
  0x110b5, 0x110b6, 0x110b9, 0x110ba, 0x110c2, 0x11100, 0x11101, 0x11102, 0x11127,
  0x11128, 0x11129, 0x1112a, 0x1112b, 0x1112d, 0x1112e, 0x1112f, 0x11130, 0x11131,
  0x11132, 0x11133, 0x11134, 0x11173, 0x11180, 0x11181, 0x111b6, 0x111b7, 0x111b8,
  0x111b9, 0x111ba, 0x111bb, 0x111bc, 0x111bd, 0x111be, 0x111c9, 0x111ca, 0x111cb,
  0x111cc, 0x111cf, 0x1122f, 0x11230, 0x11231, 0x11234, 0x11236, 0x11237, 0x1123e,
  0x11241, 0x112df, 0x112e3, 0x112e4, 0x112e5, 0x112e6, 0x112e7, 0x112e8, 0x112e9,
  0x112ea, 0x11300, 0x11301, 0x1133b, 0x1133c, 0x11340, 0x11366, 0x11367, 0x11368,
  0x11369, 0x1136a, 0x1136b, 0x1136c, 0x11370, 0x11371, 0x11372, 0x11373, 0x11374,
  0x113bb, 0x113bc, 0x113bd, 0x113be, 0x113bf, 0x113c0, 0x113ce, 0x113d0, 0x113d2,
  0x113e1, 0x113e2, 0x11438, 0x11439, 0x1143a, 0x1143b, 0x1143c, 0x1143d, 0x1143e,
  0x1143f, 0x11442, 0x11443, 0x11444, 0x11446, 0x1145e, 0x114b3, 0x114b4, 0x114b5,
  0x114b6, 0x114b7, 0x114b8, 0x114ba, 0x114bf, 0x114c0, 0x114c2, 0x114c3, 0x115b2,
  0x115b3, 0x115b4, 0x115b5, 0x115bc, 0x115bd, 0x115bf, 0x115c0, 0x115dc, 0x115dd,
  0x11633, 0x11634, 0x11635, 0x11636, 0x11637, 0x11638, 0x11639, 0x1163a, 0x1163d,
  0x1163f, 0x11640, 0x116ab, 0x116ad, 0x116b0, 0x116b1, 0x116b2, 0x116b3, 0x116b4,
  0x116b5, 0x116b7, 0x1171d, 0x1171f, 0x11722, 0x11723, 0x11724, 0x11725, 0x11727,
  0x11728, 0x11729, 0x1172a, 0x1172b, 0x1182f, 0x11830, 0x11831, 0x11832, 0x11833,
  0x11834, 0x11835, 0x11836, 0x11837, 0x11839, 0x1183a, 0x1193b, 0x1193c, 0x1193e,
  0x11943, 0x119d4, 0x119d5, 0x119d6, 0x119d7, 0x119da, 0x119db, 0x119e0, 0x11a01,
  0x11a02, 0x11a03, 0x11a04, 0x11a05, 0x11a06, 0x11a07, 0x11a08, 0x11a09, 0x11a0a,
  0x11a33, 0x11a34, 0x11a35, 0x11a36, 0x11a37, 0x11a38, 0x11a3b, 0x11a3c, 0x11a3d,
  0x11a3e, 0x11a47, 0x11a51, 0x11a52, 0x11a53, 0x11a54, 0x11a55, 0x11a56, 0x11a59,
  0x11a5a, 0x11a5b, 0x11a8a, 0x11a8b, 0x11a8c, 0x11a8d, 0x11a8e, 0x11a8f, 0x11a90,
  0x11a91, 0x11a92, 0x11a93, 0x11a94, 0x11a95, 0x11a96, 0x11a98, 0x11a99, 0x11b60,
  0x11b62, 0x11b63, 0x11b64, 0x11b66, 0x11c30, 0x11c31, 0x11c32, 0x11c33, 0x11c34,
  0x11c35, 0x11c36, 0x11c38, 0x11c39, 0x11c3a, 0x11c3b, 0x11c3c, 0x11c3d, 0x11c3f,
  0x11c92, 0x11c93, 0x11c94, 0x11c95, 0x11c96, 0x11c97, 0x11c98, 0x11c99, 0x11c9a,
  0x11c9b, 0x11c9c, 0x11c9d, 0x11c9e, 0x11c9f, 0x11ca0, 0x11ca1, 0x11ca2, 0x11ca3,
  0x11ca4, 0x11ca5, 0x11ca6, 0x11ca7, 0x11caa, 0x11cab, 0x11cac, 0x11cad, 0x11cae,
  0x11caf, 0x11cb0, 0x11cb2, 0x11cb3, 0x11cb5, 0x11cb6, 0x11d31, 0x11d32, 0x11d33,
  0x11d34, 0x11d35, 0x11d36, 0x11d3a, 0x11d3c, 0x11d3d, 0x11d3f, 0x11d40, 0x11d41,
  0x11d42, 0x11d43, 0x11d44, 0x11d45, 0x11d47, 0x11d90, 0x11d91, 0x11d95, 0x11d97,
  0x11ef3, 0x11ef4, 0x11f00, 0x11f01, 0x11f36, 0x11f37, 0x11f38, 0x11f39, 0x11f3a,
  0x11f40, 0x11f42, 0x11f5a, 0x13440, 0x13447, 0x13448, 0x13449, 0x1344a, 0x1344b,
  0x1344c, 0x1344d, 0x1344e, 0x1344f, 0x13450, 0x13451, 0x13452, 0x13453, 0x13454,
  0x13455, 0x1611e, 0x1611f, 0x16120, 0x16121, 0x16122, 0x16123, 0x16124, 0x16125,
  0x16126, 0x16127, 0x16128, 0x16129, 0x1612d, 0x1612e, 0x1612f, 0x16af0, 0x16af1,
  0x16af2, 0x16af3, 0x16af4, 0x16b30, 0x16b31, 0x16b32, 0x16b33, 0x16b34, 0x16b35,
  0x16b36, 0x16f4f, 0x16f8f, 0x16f90, 0x16f91, 0x16f92, 0x16fe4, 0x1bc9d, 0x1bc9e,
  0x1cf00, 0x1cf01, 0x1cf02, 0x1cf03, 0x1cf04, 0x1cf05, 0x1cf06, 0x1cf07, 0x1cf08,
  0x1cf09, 0x1cf0a, 0x1cf0b, 0x1cf0c, 0x1cf0d, 0x1cf0e, 0x1cf0f, 0x1cf10, 0x1cf11,
  0x1cf12, 0x1cf13, 0x1cf14, 0x1cf15, 0x1cf16, 0x1cf17, 0x1cf18, 0x1cf19, 0x1cf1a,
  0x1cf1b, 0x1cf1c, 0x1cf1d, 0x1cf1e, 0x1cf1f, 0x1cf20, 0x1cf21, 0x1cf22, 0x1cf23,
  0x1cf24, 0x1cf25, 0x1cf26, 0x1cf27, 0x1cf28, 0x1cf29, 0x1cf2a, 0x1cf2b, 0x1cf2c,
  0x1cf2d, 0x1cf30, 0x1cf31, 0x1cf32, 0x1cf33, 0x1cf34, 0x1cf35, 0x1cf36, 0x1cf37,
  0x1cf38, 0x1cf39, 0x1cf3a, 0x1cf3b, 0x1cf3c, 0x1cf3d, 0x1cf3e, 0x1cf3f, 0x1cf40,
  0x1cf41, 0x1cf42, 0x1cf43, 0x1cf44, 0x1cf45, 0x1cf46, 0x1d167, 0x1d168, 0x1d169,
  0x1d17b, 0x1d17c, 0x1d17d, 0x1d17e, 0x1d17f, 0x1d180, 0x1d181, 0x1d182, 0x1d185,
  0x1d186, 0x1d187, 0x1d188, 0x1d189, 0x1d18a, 0x1d18b, 0x1d1aa, 0x1d1ab, 0x1d1ac,
  0x1d1ad, 0x1d242, 0x1d243, 0x1d244, 0x1da00, 0x1da01, 0x1da02, 0x1da03, 0x1da04,
  0x1da05, 0x1da06, 0x1da07, 0x1da08, 0x1da09, 0x1da0a, 0x1da0b, 0x1da0c, 0x1da0d,
  0x1da0e, 0x1da0f, 0x1da10, 0x1da11, 0x1da12, 0x1da13, 0x1da14, 0x1da15, 0x1da16,
  0x1da17, 0x1da18, 0x1da19, 0x1da1a, 0x1da1b, 0x1da1c, 0x1da1d, 0x1da1e, 0x1da1f,
  0x1da20, 0x1da21, 0x1da22, 0x1da23, 0x1da24, 0x1da25, 0x1da26, 0x1da27, 0x1da28,
  0x1da29, 0x1da2a, 0x1da2b, 0x1da2c, 0x1da2d, 0x1da2e, 0x1da2f, 0x1da30, 0x1da31,
  0x1da32, 0x1da33, 0x1da34, 0x1da35, 0x1da36, 0x1da3b, 0x1da3c, 0x1da3d, 0x1da3e,
  0x1da3f, 0x1da40, 0x1da41, 0x1da42, 0x1da43, 0x1da44, 0x1da45, 0x1da46, 0x1da47,
  0x1da48, 0x1da49, 0x1da4a, 0x1da4b, 0x1da4c, 0x1da4d, 0x1da4e, 0x1da4f, 0x1da50,
  0x1da51, 0x1da52, 0x1da53, 0x1da54, 0x1da55, 0x1da56, 0x1da57, 0x1da58, 0x1da59,
  0x1da5a, 0x1da5b, 0x1da5c, 0x1da5d, 0x1da5e, 0x1da5f, 0x1da60, 0x1da61, 0x1da62,
  0x1da63, 0x1da64, 0x1da65, 0x1da66, 0x1da67, 0x1da68, 0x1da69, 0x1da6a, 0x1da6b,
  0x1da6c, 0x1da75, 0x1da84, 0x1da9b, 0x1da9c, 0x1da9d, 0x1da9e, 0x1da9f, 0x1daa1,
  0x1daa2, 0x1daa3, 0x1daa4, 0x1daa5, 0x1daa6, 0x1daa7, 0x1daa8, 0x1daa9, 0x1daaa,
  0x1daab, 0x1daac, 0x1daad, 0x1daae, 0x1daaf, 0x1e000, 0x1e001, 0x1e002, 0x1e003,
  0x1e004, 0x1e005, 0x1e006, 0x1e008, 0x1e009, 0x1e00a, 0x1e00b, 0x1e00c, 0x1e00d,
  0x1e00e, 0x1e00f, 0x1e010, 0x1e011, 0x1e012, 0x1e013, 0x1e014, 0x1e015, 0x1e016,
  0x1e017, 0x1e018, 0x1e01b, 0x1e01c, 0x1e01d, 0x1e01e, 0x1e01f, 0x1e020, 0x1e021,
  0x1e023, 0x1e024, 0x1e026, 0x1e027, 0x1e028, 0x1e029, 0x1e02a, 0x1e08f, 0x1e130,
  0x1e131, 0x1e132, 0x1e133, 0x1e134, 0x1e135, 0x1e136, 0x1e2ae, 0x1e2ec, 0x1e2ed,
  0x1e2ee, 0x1e2ef, 0x1e4ec, 0x1e4ed, 0x1e4ee, 0x1e4ef, 0x1e5ee, 0x1e5ef, 0x1e6e3,
  0x1e6e6, 0x1e6ee, 0x1e6ef, 0x1e6f5, 0x1e8d0, 0x1e8d1, 0x1e8d2, 0x1e8d3, 0x1e8d4,
  0x1e8d5, 0x1e8d6, 0x1e944, 0x1e945, 0x1e946, 0x1e947, 0x1e948, 0x1e949, 0x1e94a,
  0xe0100, 0xe0101, 0xe0102, 0xe0103, 0xe0104, 0xe0105, 0xe0106, 0xe0107, 0xe0108,
  0xe0109, 0xe010a, 0xe010b, 0xe010c, 0xe010d, 0xe010e, 0xe010f, 0xe0110, 0xe0111,
  0xe0112, 0xe0113, 0xe0114, 0xe0115, 0xe0116, 0xe0117, 0xe0118, 0xe0119, 0xe011a,
  0xe011b, 0xe011c, 0xe011d, 0xe011e, 0xe011f, 0xe0120, 0xe0121, 0xe0122, 0xe0123,
  0xe0124, 0xe0125, 0xe0126, 0xe0127, 0xe0128, 0xe0129, 0xe012a, 0xe012b, 0xe012c,
  0xe012d, 0xe012e, 0xe012f, 0xe0130, 0xe0131, 0xe0132, 0xe0133, 0xe0134, 0xe0135,
  0xe0136, 0xe0137, 0xe0138, 0xe0139, 0xe013a, 0xe013b, 0xe013c, 0xe013d, 0xe013e,
  0xe013f, 0xe0140, 0xe0141, 0xe0142, 0xe0143, 0xe0144, 0xe0145, 0xe0146, 0xe0147,
  0xe0148, 0xe0149, 0xe014a, 0xe014b, 0xe014c, 0xe014d, 0xe014e, 0xe014f, 0xe0150,
  0xe0151, 0xe0152, 0xe0153, 0xe0154, 0xe0155, 0xe0156, 0xe0157, 0xe0158, 0xe0159,
  0xe015a, 0xe015b, 0xe015c, 0xe015d, 0xe015e, 0xe015f, 0xe0160, 0xe0161, 0xe0162,
  0xe0163, 0xe0164, 0xe0165, 0xe0166, 0xe0167, 0xe0168, 0xe0169, 0xe016a, 0xe016b,
  0xe016c, 0xe016d, 0xe016e, 0xe016f, 0xe0170, 0xe0171, 0xe0172, 0xe0173, 0xe0174,
  0xe0175, 0xe0176, 0xe0177, 0xe0178, 0xe0179, 0xe017a, 0xe017b, 0xe017c, 0xe017d,
  0xe017e, 0xe017f, 0xe0180, 0xe0181, 0xe0182, 0xe0183, 0xe0184, 0xe0185, 0xe0186,
  0xe0187, 0xe0188, 0xe0189, 0xe018a, 0xe018b, 0xe018c, 0xe018d, 0xe018e, 0xe018f,
  0xe0190, 0xe0191, 0xe0192, 0xe0193, 0xe0194, 0xe0195, 0xe0196, 0xe0197, 0xe0198,
  0xe0199, 0xe019a, 0xe019b, 0xe019c, 0xe019d, 0xe019e, 0xe019f, 0xe01a0, 0xe01a1,
  0xe01a2, 0xe01a3, 0xe01a4, 0xe01a5, 0xe01a6, 0xe01a7, 0xe01a8, 0xe01a9, 0xe01aa,
  0xe01ab, 0xe01ac, 0xe01ad, 0xe01ae, 0xe01af, 0xe01b0, 0xe01b1, 0xe01b2, 0xe01b3,
  0xe01b4, 0xe01b5, 0xe01b6, 0xe01b7, 0xe01b8, 0xe01b9, 0xe01ba, 0xe01bb, 0xe01bc,
  0xe01bd, 0xe01be, 0xe01bf, 0xe01c0, 0xe01c1, 0xe01c2, 0xe01c3, 0xe01c4, 0xe01c5,
  0xe01c6, 0xe01c7, 0xe01c8, 0xe01c9, 0xe01ca, 0xe01cb, 0xe01cc, 0xe01cd, 0xe01ce,
  0xe01cf, 0xe01d0, 0xe01d1, 0xe01d2, 0xe01d3, 0xe01d4, 0xe01d5, 0xe01d6, 0xe01d7,
  0xe01d8, 0xe01d9, 0xe01da, 0xe01db, 0xe01dc, 0xe01dd, 0xe01de, 0xe01df, 0xe01e0,
  0xe01e1, 0xe01e2, 0xe01e3, 0xe01e4, 0xe01e5, 0xe01e6, 0xe01e7, 0xe01e8, 0xe01e9,
  0xe01ea, 0xe01eb, 0xe01ec, 0xe01ed, 0xe01ee, 0xe01ef,
};
}
