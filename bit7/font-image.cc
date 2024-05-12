#include "font-image.h"

#include <string>
#include <map>
#include <cstdint>
#include <memory>

#include "util.h"
#include "image.h"
#include "base/logging.h"

using namespace std;

static constexpr bool VERBOSE = false;

// TODO: Need to add page for "old" DFX fonts.

Page Config::ParsePage(const std::string &p) {
  if (p == "bit7-classic") return Page::BIT7_CLASSIC;
  if (p == "bit7-extended") return Page::BIT7_EXTENDED;
  LOG(FATAL) << "Unknown page " << p;
}

const char *Config::PageString(Page p) {
  switch (p) {
  case Page::BIT7_CLASSIC:
    return "bit7-classic";
  case Page::BIT7_EXTENDED:
    return "bit7-extended";
  default:
    break;
  }
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
    // APPROXIMATELY EQUAL
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

    // Unclaimed. Was once emoji, but I moved those to the extended
    // page.
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
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

    // Three free before replacement char
    -1,
    -1,
    -1,
    // <?> replacement char
    0xFFFD,

    // Black circle, black square
    0x25CF,
    0x25A0,
    // geometric shapes and bullets, unclaimed
    -1,
    0x203B, // reference mark
    // cont'd
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

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

    // math
    // exists, forall
    0x2203, 0x2200,
    // rest of math, unclaimed
    0x2213, // minus-plus
    0x222B, // integral sign
    0x2260, // not equal
    0x2261, // identical to (triple-eq)
    0x2243, // approximately equal (~ on -)
    0x2248, // almost equal (~ on =)
    0x2262, // logical not

    -1, -1, -1, -1, -1, -1, -1,

    // Block Elements, in unicode order
    0x2580, 0x2581, 0x2582, 0x2583, 0x2584, 0x2585, 0x2586, 0x2587,
    0x2588, 0x2589, 0x258A, 0x258B, 0x258C, 0x258D, 0x258E, 0x258F,
    0x2590, 0x2591, 0x2592, 0x2593, 0x2594, 0x2595, 0x2596, 0x2597,
    0x2598, 0x2599, 0x259A, 0x259B, 0x259C, 0x259D, 0x259E, 0x259F,
  };
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
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

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


static const std::vector<int> &GetCodepointsForPage(Page p) {
  switch (p) {
  case Page::BIT7_CLASSIC: return PageBit7Classic();
  case Page::BIT7_EXTENDED: return PageBit7Extended();
  }
  LOG(FATAL) << "Unimplemented page!";
}

// e.g. use the glyph for hyphen (0x2D) to render U+2212 (minus).
static constexpr std::initializer_list<std::pair<int, int>>
REUSE_FOR = {
  // hyphen used as minus
  {0x2D, 0x2212},
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
  {'T', 0x0422},
  {'X', 0x0425},
  {'a', 0x0430},
  {'e', 0x0435},
  {'o', 0x043E},
  {'p', 0x0440},
  {'c', 0x0441},
  {'x', 0x0445},
  {'s', 0x0455},
  {'i', 0x0456},
  {'j', 0x0458},
  // TODO: More cyrillic can be copied from Latin-1, Greek.

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

  // Full-width comma
  {',', 0xFF0C},
  // Ideographic space
  {' ', 0x3000},
  // Fullwidth parentheses
  // Actually we can just map these all from ascii?
  // en.wikipedia.org/wiki/Halfwidth_and_Fullwidth_Forms_(Unicode_block)
  {'(', 0xFF08},
  {')', 0xFF09},

  // "equal and parallel to" is like an octothorpe
  {'#', 0x22D5},

  // "Prohibited sign" is identical to "No entry sign" but
  // black instead of red.
  {0x1F6AB, 0x1F6C7},

  // Kelvin symbol
  {'K', 0x212A},
  // Ohm symbol from greek Omega
  {0x03A9, 0x2126},

  // ISO Latin-1 Macron to overline
  {0x00AF, 0x203E},

  // bullet -> katakana middle dot
  {0x2022, 0x30FB},

  // Black circle -> black circle for record
  {0x25CF, 0x23FA},
  // Same for square
  {0x25A0, 0x23F9},
};

Config Config::ParseConfig(const string &cfgfile) {
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
               "array for page %s!\n",
               cidx % config.chars_across, cidx / config.chars_across,
               Config::PageString(p));
        printf("%s", GlyphString(glyph).c_str());
      }
      continue;
    }

    CHECK(cidx >= 0 && cidx < (int)codepoints.size());
    const int codepoint = codepoints[cidx];
    if (codepoint < 0) {
      if (!ok_missing) {
        printf("Skipping glyph at %d,%d because the codepoint is not "
               "configured in page %s!\n",
               cidx % config.chars_across, cidx / config.chars_across,
               Config::PageString(p));
        printf("%s", FontImage::GlyphString(glyph).c_str());
      }

    } else {
      // A codepoint can appear multiple times in different pages. We take
      // the last one, but don't overwrite with an empty glyph.
      if (!is_empty || !unicode_to_glyph.contains(codepoint)) {
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
    // If we do have the source, but don't have the dest, copy.
    if (unicode_to_glyph.contains(src) &&
        !unicode_to_glyph.contains(dst)) {
      if (VERBOSE) {
        printf("Copy %04x to %04x\n", src, dst);
      }
      unicode_to_glyph[dst] = unicode_to_glyph[src];
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

void FontImage::SaveImage(const std::string &filename) {
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

void FontImage::InitUnicode() {
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
                           const std::string &s) const {
  std::vector<uint32_t> cps = Util::UTF8Codepoints(s);
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

std::unique_ptr<BitmapFont> BitmapFont::Load(const std::string &configfile) {
  // XXX interpret png in config relative to config path so that
  // this can load from other directories.
  Config cfg = Config::ParseConfig(configfile);
  FontImage font_image(cfg);
  return std::make_unique<BitmapFont>(std::move(font_image));
}
