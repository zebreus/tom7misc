
#include "re2/re2.h"

#include <stdio.h>
#include <string>
#include <string_view>

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

  std::string_view view(msg);

  std::string prefix, body;
  CHECK(RE2::Consume(&view, "(" ANY_CHAR "*)<THIS>(" ANY_CHAR "*)</THIS>",
                     &prefix, &body));
  CHECK_EQ(prefix, "This is the part to ignore.\nAnd then ");
  CHECK_EQ(body, " is the good stuff.");
  CHECK_EQ(view, "bye.\n");
}

// Test a potential regression in bitstate when migrating to string_view.
void TestBitStateFirstMatchBug() {

  {
    std::string_view text = "word";
    std::string_view match;
    CHECK(RE2::PartialMatch(text, "(\\w+)", &match));
    CHECK_EQ(match, "word") << "Match was not captured correctly. Got: " << match;
  }

  {
    printf("Bitstate...\n");
    const std::string text = "faa!!";
    const RE2 re("(.*)a");

    // 0 is the whole match, and 1 is the parenthesized group.
    std::string_view submatches[2] = {};
    submatches[0] = "";
    submatches[1] = "";
    CHECK(submatches[0].data() != nullptr);

    CHECK(re.Match(text, 0, text.size(), RE2::ANCHOR_START, submatches, 2));
    printf("MATCH OK\n");

    // Check that the submatches were correctly populated.
    // Buggy code would fail here: the submatches would be empty.
    CHECK_EQ(submatches[0], "faa") << submatches[0];
    CHECK_EQ(submatches[1], "fa") << submatches[1];
  }

  {
    printf("A|B\n");
    std::string_view text2 = "a";
    std::string_view cap1 = "asdf", cap2 = "1234";
    CHECK(RE2::FullMatch(text2, "(a)|(b)", &cap1, &cap2));
    CHECK_EQ(cap1, "a") << cap1;
    CHECK(cap2.empty()) << cap2;
    // In c++17, data can be null (and is, for a default-constructed
    // string_view). It's kinda harmless, but RE2 used to rely on
    // null data pointers, so I have tried to stamp that out when
    // switching to string_view.
    CHECK(cap2.data() != nullptr) << cap2;
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSimple();
  TestReplace();
  TestObject();
  TestConsume();
  TestBitStateFirstMatchBug();

  Print("OK\n");
  return 0;
}
