#include "font-image.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "image.h"
#include "utf8.h"
#include "util.h"

using namespace std;

static constexpr bool VERBOSE = true;

// TODO: Need to add page for "old" DFX fonts.

Page Config::ParsePage(const std::string &p) {
  if (p == "bit7-classic") return Page::BIT7_CLASSIC;
  if (p == "bit7-extended") return Page::BIT7_EXTENDED;
  if (p == "bit7-cyrillic") return Page::BIT7_CYRILLIC;
  if (p == "bit7-math") return Page::BIT7_MATH;
  LOG(FATAL) << "Unknown page " << p;
}

const char *Config::PageString(Page p) {
  switch (p) {
  case Page::BIT7_CLASSIC:
    return "bit7-classic";
  case Page::BIT7_EXTENDED:
    return "bit7-extended";
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
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,

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
    -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1,

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
    // Hourglass
    0x231B,
    // Pile of poo
    0x1F4A9,

    // Film strip.
    // I think there's also an emoji presentation of this
    // when followed by U+FE0F.
    0x1F39E,

    -1, -1, -1, -1, -1, -1, -1, -1, -1,

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

    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  };

  return CODEPOINTS;
};

static const std::vector<int> &GetCodepointsForPage(Page p) {
  switch (p) {
  case Page::BIT7_CLASSIC: return PageBit7Classic();
  case Page::BIT7_EXTENDED: return PageBit7Extended();
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
  // Ideographic space
  {' ', 0x3000},
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

  // Various for IPA (Latin Extended B)
  {0x03A3, 0x01A9}, // Greek Σ -> esh
  {'!', 0x01C3},
  {0x04D9, 0x0259}, // Cyrillic schwa -> IPA schwa

  // Letter-like symbols that are REALLY letter-like.
  // Kelvin
  {'K', 0x212A},
  // A with circle -> Angstrom
  {0x00C5, 0x212B},
  // Omega -> Ohm sign
  {0x03A9, 0x2126},
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

std::unique_ptr<BitmapFont> BitmapFont::Load(const std::string &configfile) {
  // XXX interpret png in config relative to config path so that
  // this can load from other directories.
  Config cfg = Config::ParseConfig(configfile);
  FontImage font_image(cfg);
  return std::make_unique<BitmapFont>(std::move(font_image));
}
