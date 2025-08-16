

#include "ansi.h"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cstdint>
#include <tuple>

#include "base/logging.h"
#include "base/stringprintf.h"

using namespace std;

static void TestProgress() {
  // Note that this will not match the display, since it is
  // calculating 927/1979ths worth of progress in that amount of time.
  const double sec = 45.0 * 3600.0 * 24.0 * 365.25;
  printf("%s\n", ANSI::ProgressBar(927, 1979,
                                   "a " AYELLOW("~\\_(ツ)_/~") " birthday boy",
                                   sec).c_str());
}

static std::string Escaped(const std::string &ansi) {
  std::string ret;
  for (int i = 0; i < (int)ansi.size(); i++) {
    if (ansi[i] == '\x1B') StringAppendF(&ret, "(ESC)");
    else ret.push_back(ansi[i]);
  }
  return ret;
}

static void TestMacros() {
  printf("NORMAL" " "
         ARED("ARED") " "
         AGREY("AGREY") " "
         ABLUE("ABLUE") " "
         ACYAN("ACYAN") " "
         AYELLOW("AYELLOW") " "
         AGREEN("AGREEN") " "
         AWHITE("AWHITE") " "
         APURPLE("APURPLE") "\n");

  printf(ADARKRED("ADARKRED") " "
         ADARKGREY("ADARKGREY") " "
         ADARKBLUE("ADARKBLUE") " "
         ADARKCYAN("ADARKCYAN") " "
         ADARKYELLOW("ADARKYELLOW") " "
         ADARKGREEN("ADARKGREEN") " "
         ADARKWHITE("ADARKWHITE") " "
         ADARKPURPLE("ADARKPURPLE") "\n");

  printf(AFGCOLOR(50, 74, 168, "Blue FG") " "
         ABGCOLOR(50, 74, 168, "Blue BG") " "
         AFGCOLOR(226, 242, 136, ABGCOLOR(50, 74, 168, "Combined")) "\n");

  printf(ABGCOLOR(200, 0, 200,
                  ANSI_DARK_GREY "black on magenta "
                  ANSI_YELLOW "yellow"
                  ANSI_DARK_GREY " black again") "\n");
}

static void TestComposite() {
  std::vector<std::pair<uint32_t, int>> fgs =
    {{0xFFFFFFAA, 5},
     {0xFF00003F, 6},
     {0x123456FF, 3}};
  std::vector<std::pair<uint32_t, int>> bgs =
    {{0x333333FF, 3},
     {0xCCAA22FF, 4},
     {0xFFFFFFFF, 1}};

  printf("Composited:\n");
  for (const char *s :
         {"", "##############",
          "short", "long string that gets truncated",
          ("Unic\xE2\x99\xA5"  "de")}) {
    printf("%s\n", ANSI::Composite(s,
                                   ANSI::Rasterize(fgs, 14),
                                   ANSI::Rasterize(bgs, 14)).c_str());
  }
}

static void TestStringWidth() {
  CHECK(ANSI::StringWidth("asdf") == 4);
  CHECK(ANSI::StringWidth("") == 0);
  CHECK(ANSI::StringWidth(ANSI_RESET "") == 0);
  CHECK(ANSI::StringWidth("xx" ANSI_RESET "ee") == 4);
  CHECK(ANSI::StringWidth("xx" ARED("hi") "ee") == 6);
  CHECK(ANSI::StringWidth("xx" AFGCOLOR(1, 2, 3, "hi") "ee") == 6);
  CHECK(ANSI::StringWidth("xx" ABGCOLOR(1, 2, 3, "hi") "ee") == 6);
  CHECK(ANSI::StringWidth("♚") == 1);
  CHECK(ANSI::StringWidth(ABLUE("♚")) == 1);
}

static void TestRasterize() {
  std::vector<uint32_t> r =
    ANSI::Rasterize(
        {{0x123456FF, 2},
         {0x888888FF, 1},
         {0x991122FF, 1}}, 6);

  CHECK(r.size() == 6);
  CHECK(r[0] == 0x123456FF);
  CHECK(r[1] == 0x123456FF);
  CHECK(r[2] == 0x888888FF);
  CHECK(r[3] == 0x991122FF);
  CHECK(r[4] == 0x991122FF);
  CHECK(r[5] == 0x991122FF);
}

static std::string PrintColors(const std::vector<uint32_t> &c) {
  std::string ret;
  for (const uint32_t cc : c) {
    if (!ret.empty()) ret += ", ";
    StringAppendF(&ret,
                  "%s%08x" ANSI_RESET,
                  ANSI::BackgroundRGB32(cc).c_str(),
                  cc);
  }
  return ret;
}

static void TestDecompose() {

  {
    const auto &[s, fg, bg] =
      ANSI::Decompose("no", 0x333333FF, 0x111111FF);
    CHECK(s == "no");
    CHECK(fg.size() == 2);
    CHECK(bg.size() == 2);
    CHECK(fg == std::vector<uint32_t>({0x333333FF, 0x333333FF}));
    CHECK(bg == std::vector<uint32_t>({0x111111FF, 0x111111FF}));
  }

  {
    // Note the ANSI_ macros used to set the background to black, but
    // I changed my mind on that.
    const auto &[s, fg, bg] =
      ANSI::Decompose("n" ANSI_RED "o" ANSI_RESET "w",
                      0x333333FF, 0x111111FF);
    CHECK(s == "now") << s;
    CHECK(fg.size() == 3);
    CHECK(bg.size() == 3);
    CHECK(fg == std::vector<uint32_t>({0x333333FF, 0xff7676FF, 0x333333FF}))
      << PrintColors(fg);
    CHECK(bg == std::vector<uint32_t>({0x111111FF, 0x111111FF, 0x111111FF}))
      << PrintColors(bg);
  }

  {
    const auto &[s, fg, bg] =
      ANSI::Decompose("n" ANSI_DARK_GREEN "ow",
                      0x333333FF, 0x111111FF);
    CHECK(s == "now");
    CHECK(fg.size() == 3);
    CHECK(bg.size() == 3);
    CHECK(fg == std::vector<uint32_t>({0x333333FF, 0x1ca800FF, 0x1ca800FF}))
      << PrintColors(fg);
    CHECK(bg == std::vector<uint32_t>({0x111111FF, 0x111111FF, 0x111111FF}))
      << PrintColors(bg);
  }

  {
    // Test multibyte UTF-8 codepoints.
    const auto &[s, fg, bg] =
      ANSI::Decompose("N" ANSI_RED "∃" ANSI_RESET "W",
                      0x333333FF, 0x111111FF);
    CHECK(s == "N∃W");
    CHECK(fg.size() == 3);
    CHECK(bg.size() == 3);
    CHECK(fg == std::vector<uint32_t>({0x333333FF, 0xff7676ff, 0x333333FF}))
      << PrintColors(fg);
    CHECK(bg == std::vector<uint32_t>({0x111111FF, 0x111111FF, 0x111111FF}))
      << PrintColors(bg);
  }

  {
    // Test with explicit foreground color.
    std::string str = "Y" AFGCOLOR(12, 34, 56, "∃") "S";
    const auto &[s, fg, bg] =
      ANSI::Decompose(str, 0x333333FF, 0x111111FF);
    CHECK(s == "Y∃S") << s;
    CHECK(fg.size() == 3);
    CHECK(bg.size() == 3);
    CHECK(fg == std::vector<uint32_t>({0x333333FF, 0x0c2238ff, 0x333333FF}))
      << Escaped(str) << "\n"
      << PrintColors(fg);
    CHECK(bg == std::vector<uint32_t>({0x111111FF, 0x111111FF, 0x111111FF}))
      << PrintColors(bg);
  }

  {
    // And background color.
    std::string str = "N" ABGCOLOR(12, 34, 56, "O") "T";
    const auto &[s, fg, bg] =
      ANSI::Decompose(str, 0x333333FF, 0x111111FF);
    CHECK(s == "NOT") << s;
    CHECK(fg.size() == 3);
    CHECK(bg.size() == 3);
    CHECK(fg == std::vector<uint32_t>({0x333333FF, 0x333333ff, 0x333333FF}))
      << Escaped(str) << "\n"
      << PrintColors(fg);
    CHECK(bg == std::vector<uint32_t>({0x111111FF, 0x0c2238ff, 0x111111FF}))
      << PrintColors(bg);
  }

}

static void TestTime() {
  CHECK(ANSI::StripCodes(ANSI::Time(61.0)) == "1m01s");
}

static void TestWidth() {
  std::optional<int> w = ANSI::TerminalWidth();
  printf("Terminal width: ");
  if (w.has_value()) {
    CHECK(w.value() > 0);
    printf("%d\n", w.value());
    for (int i = 0; i < w; i++)
      printf("-");
    printf("\n");
  } else {
    printf("(unknown)\n");
  }
}

static void TestSubstr() {
  std::string test =
    "Cool " ACYAN("N") ARED("∃")
    AFGCOLOR(12, 34, 156, ABGCOLOR(240, 220, 55, "∀")) ANSI_RESET
    ABGCOLOR(150, 0, 0, "TO ") "things";

  int testwidth = ANSI::StringWidth(test);
  for (int i = 0; i <= testwidth - 4; i++) {
    std::string four = ANSI::ColorSubstring(test, i, 4);
    CHECK(ANSI::StringWidth(four) == 4) << "[" << four <<
      "]\nLength: " << ANSI::StringWidth(four);
    printf("[%s]\n", four.c_str());
  }

  std::string to_end = ANSI::ColorSubstring(test, 2, std::string_view::npos);
  CHECK(ANSI::StringWidth(to_end) == testwidth - 2);
  printf("And to the end: [%s]\n",
         to_end.c_str());
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestProgress();

  TestMacros();
  TestStringWidth();
  TestComposite();
  TestRasterize();
  TestDecompose();
  TestTime();
  TestWidth();
  TestSubstr();

  printf(AGREEN("OK") "\n");
  return 0;
}
