
// TODO: Clean up for CC_LIB.

#ifndef _CC_LIB_ANSI_H
#define _CC_LIB_ANSI_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#define ANSI_PREVLINE "\x1B[F"
#define ANSI_CLEARLINE "\x1B[2K"
#define ANSI_CLEARTOEOL "\x1B[0K"
#define ANSI_BEGINNING_OF_LINE "\x1B[G"
// Put the cursor at the beginning of the current line, and clear it.
#define ANSI_RESTART_LINE ANSI_BEGINNING_OF_LINE ANSI_CLEARTOEOL
// Move to the previous line and clear it.
#define ANSI_UP ANSI_PREVLINE ANSI_CLEARLINE

#define ANSI_RESET "\x1B[m"

// These are "bright" or "high intensity" foreground colors.
//
// Note: Previously these would set the background to black
// explicitly, and used the "bold" versions. But many terminals
// will treat the bold colors as both bright and bold font.
// So this attempts to get "however your terminal is configured
// for bright red, but normal font weight."
//
// Note: These also turn everything else off (0;) which might
// not be what we want (makes it hard to compose with background
// color).
#define ANSI_RED "\x1B[0;91m"
#define ANSI_GREY "\x1B[0;90m"
#define ANSI_BLUE "\x1B[0;94m"
#define ANSI_CYAN "\x1B[0;96m"
#define ANSI_YELLOW "\x1B[0;93m"
#define ANSI_GREEN "\x1B[0;92m"
#define ANSI_WHITE "\x1B[0;97m"
#define ANSI_PURPLE "\x1B[0;95m"

// These are "regular" foreground colors.
// They explicitly set the non-bright fg state.
#define ANSI_DARK_RED "\x1B[22;31m"
// dark grey = black
#define ANSI_DARK_GREY "\x1B[22;30m"
#define ANSI_DARK_BLUE "\x1B[22;34m"
#define ANSI_DARK_CYAN "\x1B[22;36m"
#define ANSI_DARK_YELLOW "\x1B[22;33m"
#define ANSI_DARK_GREEN "\x1B[22;32m"
#define ANSI_DARK_WHITE "\x1B[22;37m"
#define ANSI_DARK_PURPLE "\x1B[22;35m"

#define ARED(s) ANSI_RED s ANSI_RESET
#define AGREY(s) ANSI_GREY s ANSI_RESET
#define ABLUE(s) ANSI_BLUE s ANSI_RESET
#define ACYAN(s) ANSI_CYAN s ANSI_RESET
#define AYELLOW(s) ANSI_YELLOW s ANSI_RESET
#define AGREEN(s) ANSI_GREEN s ANSI_RESET
#define AWHITE(s) ANSI_WHITE s ANSI_RESET
#define APURPLE(s) ANSI_PURPLE s ANSI_RESET

#define ADARKRED(s) ANSI_DARK_RED s ANSI_RESET
#define ADARKGREY(s) ANSI_DARK_GREY s ANSI_RESET
#define ADARKBLUE(s) ANSI_DARK_BLUE s ANSI_RESET
#define ADARKCYAN(s) ANSI_DARK_CYAN s ANSI_RESET
#define ADARKYELLOW(s) ANSI_DARK_YELLOW s ANSI_RESET
#define ADARKGREEN(s) ANSI_DARK_GREEN s ANSI_RESET
#define ADARKWHITE(s) ANSI_DARK_WHITE s ANSI_RESET
#define ADARKPURPLE(s) ANSI_DARK_PURPLE s ANSI_RESET

// Experimental:
// Because {} will work as a default placeholder for
// any type in std::format, these can make the
// format strings a lot less noisy.
#define RED_ARG ARED("{}")

// standard two-level trick to expand and stringify
#define ANSI_INTERNAL_STR2(s) #s
#define ANSI_INTERNAL_STR(s) ANSI_INTERNAL_STR2(s)

// r, g, b must be numbers (numeric literals or #defined constants)
// in [0,255].
#define ANSI_FG(r, g, b) "\x1B[38;2;" \
  ANSI_INTERNAL_STR(r) ";" \
  ANSI_INTERNAL_STR(g) ";" \
  ANSI_INTERNAL_STR(b) "m"

#define ANSI_BG(r, g, b) "\x1B[48;2;" \
  ANSI_INTERNAL_STR(r) ";" \
  ANSI_INTERNAL_STR(g) ";" \
  ANSI_INTERNAL_STR(b) "m"

#define AFGCOLOR(r, g, b, s) "\x1B[38;2;" \
  ANSI_INTERNAL_STR(r) ";" \
  ANSI_INTERNAL_STR(g) ";" \
  ANSI_INTERNAL_STR(b) "m" s ANSI_RESET

#define ABGCOLOR(r, g, b, s) "\x1B[48;2;" \
  ANSI_INTERNAL_STR(r) ";" \
  ANSI_INTERNAL_STR(g) ";" \
  ANSI_INTERNAL_STR(b) "m" s ANSI_RESET

// non-standard colors. Perhaps should move some of these things
// to ansi-extended or something.
#define AORANGE(s) ANSI_FG(247, 155, 57) s ANSI_RESET
#define ADARKORANGE(s) ANSI_FG(189, 97, 0) s ANSI_RESET

// Same as printf, but using WriteConsole on windows so that we
// can communicate with pseudoterminal. Without this, ansi escape
// codes will work (VirtualTerminalProcessing) but not mintty-
// specific ones. Calls normal printf on other platforms, which
// hopefully support the ansi codes. Probably avoid this.
void CPrintf(const char* format, ...);

namespace internal {
// This currently needs to be hoisted out due to a compiler bug.
// After it's fixed, put it back in the struct below.
struct ProgressBarOptions {
  // including [], time.
  int full_width = 76;
  // Default RGBA color of text.
  uint32_t fg = 0xffffffcc;
  // RGBA color of progress bar.
  uint32_t bar_filled = 0x0f1591FF;
  uint32_t bar_empty  = 0x00031aFF;
  bool include_percent = true;
  bool include_frac = true;
  bool include_eta = true;
};
}

struct ANSI {

  // Call this in main for compatibility on windows.
  static void Init();

  // Without ANSI_RESET.
  static std::string ForegroundRGB(uint8_t r, uint8_t g, uint8_t b);
  static std::string BackgroundRGB(uint8_t r, uint8_t g, uint8_t b);
  // In RGBA format, but the alpha component is ignored.
  static std::string ForegroundRGB32(uint32_t rgba);
  static std::string BackgroundRGB32(uint32_t rgba);

  // Strip ANSI codes. Only CSI codes are supported (which is everything
  // in this file), and they are not validated. There are many nonstandard
  // codes that would be treated incorrectly here.
  static std::string StripCodes(std::string_view s);

  // Return the number of UTF-8 codepoints after stripping ANSI codes.
  // Same caveats as above.
  static int StringWidth(std::string_view s);

  // Indices are measured in visible output codepoints. Reconstructs
  // the colors for the substring. If you want to specify your own
  // default foreground and background, use Decompose and Composite
  // below.
  static std::string ColorSubstring(std::string_view s,
                                    size_t start,
                                    size_t len = std::string_view::npos);

  // Return an ansi-colored representation of the duration, with arbitrary
  // but Tom 7-approved choices of color and significant figures.
  static std::string Time(double seconds);

  using ProgressBarOptions = internal::ProgressBarOptions;
  // Print a color progress bar. [numer/denom  operation] ETA HH:MM:SS
  // Doesn't update in place; you need to move the cursor with
  // like ANSI_UP or use status-bar.h.
  static std::string ProgressBar(uint64_t numer, uint64_t denom,
                                 // This currently can't have ANSI Codes,
                                 // because we need to split it into pieces.
                                 std::string_view operation,
                                 double taken,
                                 ProgressBarOptions options =
                                 ProgressBarOptions{});

  // Composites foreground and background text colors into an ansi
  // string.
  // Generates a fixed-width string using the fg/bgcolors arrays.
  // The length is the maximum of these two, with the last element
  // extended to fill. Text is truncated if it's too long.
  static std::string Composite(
      // ANSI codes are stripped.
      std::string_view text,
      // entries are RGBA. Alpha is composited.
      const std::vector<uint32_t> &fgcolors,
      // entries are RGBA. Alpha is ignored.
      const std::vector<uint32_t> &bgcolors);

  // For historic uses of Composite that used to pass spans
  // (like "red for 12 characters, blue for 8 characters").
  // Generates a color per character, up to the length. The
  // final color is repeated if the spans do not cover the length.
  static std::vector<uint32_t> Rasterize(
      const std::vector<std::pair<uint32_t, int>> &spans,
      int length);

  // Decomposes text with ANSI color codes (other escape codes are
  // ignored and stripped) into:
  //   Plain stripped text
  //   Foreground RGBA
  //   Background RGBA.
  // The text is treated as UTF-8 and lengths are measured as though
  // every codepoint takes the same width.
  // It is roughly the inverse of Composite, although this always
  // generates alpha of 1.0 (assuming default_fg and default_bg are
  // also alpha=1.0). This interprets standard color codes like "red",
  // but uses its own internal RGB values for these; it will not
  // necessarily match how the terminal displays them. The default fg
  // and bg colors are the starting values (or after ANSI_RESET).
  static std::tuple<std::string,
                    std::vector<uint32_t>,
                    std::vector<uint32_t>>
  Decompose(std::string_view text_with_codes,
            uint32_t default_fg = 0xBFBFBFFF,
            uint32_t default_bg = 0x000000FF);

  // Tries to get the current terminal width using OS calls. Returns
  // nullopt if unavailable (e.g. not supported on this platform, or
  // output is not a terminal). Try TerminalWidth().value_or(80).
  static std::optional<int> TerminalWidth();

};

#endif
