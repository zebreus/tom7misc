
#include "re2/re2.h"

#include <stdio.h>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

using namespace std;

// TODO: More tests, though of course RE2 has its own tests; here
// we are just checking that we didn't screw up in the cc-lib import.
// (Might also be helpful for me to give some syntax examples too
// because I can never remember what needs to be backslashed!)
// (Would also perhaps be nice to have some tests that remind me
// of traps like "longest match" vs "first match".)
void TestSimple() {
  CHECK(RE2::FullMatch("the quick brown fox", "[a-z ]+"));
  CHECK(!RE2::FullMatch("the quick brown fox", "[a-z]+"));

  string title, artist;
  CHECK(RE2::FullMatch("Blimps Go 90 by Guided by Voices",
                       "(.+) by (.+)", &title, &artist) &&
        title == "Blimps Go 90 by Guided" &&
        artist == "Voices") << title << " / " << artist;
}

void TestReplace() {
  string artist = "They might be giants";
  CHECK(RE2::GlobalReplace(&artist, "[aeiou]", "_"));
  CHECK_EQ(artist, "Th_y m_ght b_ g__nts") << artist;
  string title = "Aphex Twin";
  CHECK(RE2::GlobalReplace(&title, "([aeiou])", "<\\1>"));
  CHECK_EQ(title, "Aph<e>x Tw<i>n") << title;
}

void TestObject() {
  static const RE2 liz("Li*z Ph[aeiou]+r");
  CHECK(RE2::FullMatch("Liiiz Phair", liz));
  CHECK(!RE2::FullMatch("Lz Phr", liz));
}

#define ANY_CHAR "(?:.|[\n])"
void TestConsume() {
  const std::string msg =
    "This is the part to ignore.\n"
    "And then <THIS> is the good stuff.</THIS>bye.\n";

  re2::StringPiece view(msg);

  std::string prefix, body;
  CHECK(RE2::Consume(&view, "(" ANY_CHAR "*)<THIS>(" ANY_CHAR "*)</THIS>",
                     &prefix, &body));
  CHECK_EQ(prefix, "This is the part to ignore.\nAnd then ");
  CHECK_EQ(body, " is the good stuff.");
  CHECK_EQ(view, "bye.\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  TestSimple();
  TestReplace();
  TestObject();
  TestConsume();
  Print("OK\n");
  return 0;
}
