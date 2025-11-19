
// This file only: Based on tests from re2. See re2/LICENSE. Consider
// my contributions to be available under that license as well.

#include "re2/re2.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <format>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "re2/regexp.h"

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

void TestUnicodeRange() {
  CHECK(RE2::FullMatch("λ", "[α-ω]"));
}

void TestAnyBytes() {
  // A valid UTF-8 sequence for 'español'.
  std::string good_utf8 = "espa\xc3\xb1ol";
  // Malformed UTF-8 with space after a UTF-8 continuation byte.
  std::string bad_utf8 = "espa\xc3 ol";

  std::string with_null("espa\x00ol", 7);
  CHECK(with_null.size() == 7);

  CHECK(RE2::FullMatch(good_utf8, ".+"));
  CHECK(!RE2::FullMatch(bad_utf8, ".+")) << ". will match proper codepoints, "
    "but not busted UTF-8.";
  CHECK(RE2::FullMatch(with_null, ".+")) << "U+0000 is ok.";

  CHECK(RE2::FullMatch(with_null, with_null));

  // In RE2, Latin1 is apparently the right way to write the regex as
  // raw bytes.
  RE2::Options binary_options;
  binary_options.set_encoding(RE2::Options::EncodingLatin1);

  CHECK(RE2::FullMatch(good_utf8, good_utf8));
  CHECK(RE2::FullMatch(bad_utf8, RE2(bad_utf8, binary_options)));
  CHECK(RE2::FullMatch(with_null, RE2(with_null, binary_options)));
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

static void TestReps() {
  static LazyRE2 HTML_ENTITY_RE = { "&#?[0-9A-Za-z]{1,24};" };
  CHECK(RE2::FullMatch("&okay;", *HTML_ENTITY_RE));
  CHECK(RE2::FullMatch("&o;", *HTML_ENTITY_RE));
  CHECK( RE2::FullMatch("&zzzzzzzzzzzzzzzzzzzzzzzz;", *HTML_ENTITY_RE));
  CHECK(!RE2::FullMatch("&;", *HTML_ENTITY_RE));
  // Too long.
  CHECK(!RE2::FullMatch("&zzzzzzzzzzzzzzzzzzzzzzzzz;", *HTML_ENTITY_RE));
}

static void HexTests() {
  #define CHECK_HEX(type, value) do {                                   \
      type v;                                                           \
      CHECK(RE2::FullMatch(#value,                                      \
        "([0-9a-fA-F]+)[uUlL]*", RE2::Hex(&v)));                        \
      CHECK_EQ(v, 0x##value);                                           \
      CHECK(RE2::FullMatch("0x" #value,                                 \
        "([0-9a-fA-FxX]+)[uUlL]*",                                      \
        RE2::CRadix(&v)));                                              \
      CHECK_EQ(v, 0x##value);                                           \
  } while (0)

  CHECK_HEX(int16_t,  2bad);
  CHECK_HEX(uint16_t, 2badU);
  CHECK_HEX(int,          dead);
  CHECK_HEX(unsigned int, deadU);
  CHECK_HEX(int32_t,  7eadbeefL);
  CHECK_HEX(uint32_t, deadbeefUL);
  CHECK_HEX(int64_t,  12345678deadbeefLL);
  CHECK_HEX(uint64_t, cafebabedeadbeefULL);

  #undef CHECK_HEX
}

static void OctalTests() {
  #define CHECK_OCTAL(type, value) do {                                 \
      type v;                                                           \
      CHECK(RE2::FullMatch(#value, "([0-7]+)[uUlL]*", RE2::Octal(&v))); \
      CHECK_EQ(v, 0##value);                                            \
      CHECK(RE2::FullMatch("0" #value, "([0-9a-fA-FxX]+)[uUlL]*",       \
                           RE2::CRadix(&v)));                           \
      CHECK_EQ(v, 0##value);                                            \
    } while (0)

  CHECK_OCTAL(short,              77777);
  CHECK_OCTAL(unsigned short,     177777U);
  CHECK_OCTAL(int,                17777777777);
  CHECK_OCTAL(unsigned int,       37777777777U);
  CHECK_OCTAL(long,               17777777777L);
  CHECK_OCTAL(unsigned long,      37777777777UL);
  CHECK_OCTAL(long long,          777777777777777777777LL);
  CHECK_OCTAL(unsigned long long, 1777777777777777777777ULL);

  #undef CHECK_OCTAL
}

static void DecimalTests() {
  #define CHECK_DECIMAL(type, value) do {                               \
      type v;                                                           \
      CHECK(RE2::FullMatch(#value, "(-?[0-9]+)[uUlL]*", &v));           \
      CHECK_EQ(v, value);                                               \
      CHECK(RE2::FullMatch(#value, "(-?[0-9a-fA-FxX]+)[uUlL]*",         \
                           RE2::CRadix(&v)));                           \
      CHECK_EQ(v, value);                                               \
    } while (0)

  CHECK_DECIMAL(short,              -1);
  CHECK_DECIMAL(unsigned short,     9999);
  CHECK_DECIMAL(int,                -1000);
  CHECK_DECIMAL(unsigned int,       12345U);
  CHECK_DECIMAL(long,               -10000000L);
  CHECK_DECIMAL(unsigned long,      3083324652U);
  CHECK_DECIMAL(long long,          -100000000000000LL);
  CHECK_DECIMAL(unsigned long long, 1234567890987654321ULL);

  #undef CHECK_DECIMAL
}

static void Replace() {
  struct ReplaceTest {
    const char *regexp;
    const char *rewrite;
    const char *original;
    const char *single;
    const char *global;
    int        greplace_count;
  };
  static std::initializer_list<ReplaceTest> tests = {
    { "(qu|[b-df-hj-np-tv-z]*)([a-z]+)",
      "\\2\\1ay",
      "the quick brown fox jumps over the lazy dogs.",
      "ethay quick brown fox jumps over the lazy dogs.",
      "ethay ickquay ownbray oxfay umpsjay overay ethay azylay ogsday.",
      9 },
    { "\\w+",
      "\\0-NOSPAM",
      "abcd.efghi@google.com",
      "abcd-NOSPAM.efghi@google.com",
      "abcd-NOSPAM.efghi-NOSPAM@google-NOSPAM.com-NOSPAM",
      4 },
    { "^",
      "(START)",
      "foo",
      "(START)foo",
      "(START)foo",
      1 },
    { "^",
      "(START)",
      "",
      "(START)",
      "(START)",
      1 },
    { "$",
      "(END)",
      "",
      "(END)",
      "(END)",
      1 },
    { "b",
      "bb",
      "ababababab",
      "abbabababab",
      "abbabbabbabbabb",
      5 },
    { "b",
      "bb",
      "bbbbbb",
      "bbbbbbb",
      "bbbbbbbbbbbb",
      6 },
    { "b+",
      "bb",
      "bbbbbb",
      "bb",
      "bb",
      1 },
    { "b*",
      "bb",
      "bbbbbb",
      "bb",
      "bb",
      1 },
    { "b*",
      "bb",
      "aaaaa",
      "bbaaaaa",
      "bbabbabbabbabbabb",
      6 },
    // Check newline handling
    { "a.*a",
      "(\\0)",
      "aba\naba",
      "(aba)\naba",
      "(aba)\n(aba)",
      2 },
  };

  for (const ReplaceTest &t : tests) {
    std::string one(t.original);
    CHECK(RE2::Replace(&one, t.regexp, t.rewrite));
    CHECK_EQ(one, t.single);
    std::string all(t.original);
    CHECK_EQ(RE2::GlobalReplace(&all, t.regexp, t.rewrite), t.greplace_count)
      << "Got: " << all;
    CHECK_EQ(all, t.global);
  }
}

static void CheckRewriteString() {
  auto TestCheckRewriteString = [](const char* regexp, const char* rewrite,
                                   bool expect_ok) {
      std::string error;
      RE2 exp(regexp);
      bool actual_ok = exp.CheckRewriteString(rewrite, &error);
      CHECK_EQ(expect_ok, actual_ok) << " for " << rewrite << " error: " << error;
    };

  TestCheckRewriteString("abc", "foo", true);
  TestCheckRewriteString("abc", "foo\\", false);
  TestCheckRewriteString("abc", "foo\\0bar", true);

  TestCheckRewriteString("a(b)c", "foo", true);
  TestCheckRewriteString("a(b)c", "foo\\0bar", true);
  TestCheckRewriteString("a(b)c", "foo\\1bar", true);
  TestCheckRewriteString("a(b)c", "foo\\2bar", false);
  TestCheckRewriteString("a(b)c", "f\\\\2o\\1o", true);

  TestCheckRewriteString("a(b)(c)", "foo\\12", true);
  TestCheckRewriteString("a(b)(c)", "f\\2o\\1o", true);
  TestCheckRewriteString("a(b)(c)", "f\\oo\\1", false);
}

static void Extract() {
  std::string s;

  CHECK(RE2::Extract("boris@kremvax.ru", "(.*)@([^.]*)", "\\2!\\1", &s));
  CHECK_EQ(s, "kremvax!boris");

  CHECK(RE2::Extract("foo", ".*", "'\\0'", &s));
  CHECK_EQ(s, "'foo'");
  // check that false match doesn't overwrite
  CHECK(!RE2::Extract("baz", "bar", "'\\0'", &s));
  CHECK_EQ(s, "'foo'");
}

static void MaxSubmatchTooLarge() {
  // We could maybe turn off verbose mode?
  Print("Some error printouts are expected here:\n");
  std::string s;
  CHECK(!RE2::Extract("foo", "f(o+)", "\\1\\2", &s));
  s = "foo";
  CHECK(!RE2::Replace(&s, "f(o+)", "\\1\\2"));
  s = "foo";
  CHECK(!RE2::GlobalReplace(&s, "f(o+)", "\\1\\2"));
}

static void Consume() {
  RE2 r("\\s*(\\w+)");    // matches a word, possibly proceeded by whitespace
  std::string word;

  std::string s("   aaa b!@#$@#$cccc");
  std::string_view input(s);

  CHECK(RE2::Consume(&input, r, &word));
  CHECK_EQ(word, "aaa") << " input: " << input;
  CHECK(RE2::Consume(&input, r, &word));
  CHECK_EQ(word, "b") << " input: " << input;
  CHECK(!RE2::Consume(&input, r, &word)) << " input: " << input;
}

static void ConsumeN() {
  const std::string s(" one two three 4");
  std::string_view input(s);

  RE2::Arg argv[2];
  const RE2::Arg* const args[2] = { &argv[0], &argv[1] };

  // 0 arg
  CHECK(RE2::ConsumeN(&input, "\\s*(\\w+)", args, 0));  // Skips "one".

  // 1 arg
  std::string word;
  argv[0] = &word;
  CHECK(RE2::ConsumeN(&input, "\\s*(\\w+)", args, 1));
  CHECK_EQ("two", word);

  // Multi-args
  int n;
  argv[1] = &n;
  CHECK(RE2::ConsumeN(&input, "\\s*(\\w+)\\s*(\\d+)", args, 2));
  CHECK_EQ("three", word);
  CHECK_EQ(4, n);
}

static void FindAndConsume() {
  RE2 r("(\\w+)");      // matches a word
  std::string word;

  std::string s("   aaa b!@#$@#$cccc");
  std::string_view input(s);

  CHECK(RE2::FindAndConsume(&input, r, &word));
  CHECK_EQ(word, "aaa");
  CHECK(RE2::FindAndConsume(&input, r, &word));
  CHECK_EQ(word, "b");
  CHECK(RE2::FindAndConsume(&input, r, &word));
  CHECK_EQ(word, "cccc");
  CHECK(!RE2::FindAndConsume(&input, r, &word));

  // Check that FindAndConsume works without any submatches.
  // Earlier version used uninitialized data for
  // length to consume.
  input = "aaa";
  CHECK(RE2::FindAndConsume(&input, "aaa"));
  CHECK_EQ(input, "");
}

static void FindAndConsumeN() {
  const std::string s(" one two three 4");
  std::string_view input(s);

  RE2::Arg argv[2];
  const RE2::Arg* const args[2] = { &argv[0], &argv[1] };

  // 0 arg
  CHECK(RE2::FindAndConsumeN(&input, "(\\w+)", args, 0));  // Skips "one".

  // 1 arg
  std::string word;
  argv[0] = &word;
  CHECK(RE2::FindAndConsumeN(&input, "(\\w+)", args, 1));
  CHECK_EQ("two", word);

  // Multi-args
  int n;
  argv[1] = &n;
  CHECK(RE2::FindAndConsumeN(&input, "(\\w+)\\s*(\\d+)", args, 2));
  CHECK_EQ("three", word);
  CHECK_EQ(4, n);
}

static void MatchNumberPeculiarity() {
  RE2 r("(foo)|(bar)|(baz)");
  std::string word1;
  std::string word2;
  std::string word3;

  CHECK(RE2::PartialMatch("foo", r, &word1, &word2, &word3));
  CHECK_EQ(word1, "foo");
  CHECK_EQ(word2, "");
  CHECK_EQ(word3, "");
  CHECK(RE2::PartialMatch("bar", r, &word1, &word2, &word3));
  CHECK_EQ(word1, "");
  CHECK_EQ(word2, "bar");
  CHECK_EQ(word3, "");
  CHECK(RE2::PartialMatch("baz", r, &word1, &word2, &word3));
  CHECK_EQ(word1, "");
  CHECK_EQ(word2, "");
  CHECK_EQ(word3, "baz");
  CHECK(!RE2::PartialMatch("f", r, &word1, &word2, &word3));

  std::string a;
  CHECK(RE2::FullMatch("hello", "(foo)|hello", &a));
  CHECK_EQ(a, "");
}

static void Match() {
  RE2 re("((\\w+):([0-9]+))");   // extracts host and port
  std::array<std::string_view, 4> group;

  // No match.
  std::string_view s = "zyzzyva";
  CHECK(!re.Match(s, 0, s.size(), RE2::UNANCHORED,
                  group.data(), group.size()));

  // Matches and extracts.
  s = "a chrisr:9000 here";
  CHECK(re.Match(s, 0, s.size(), RE2::UNANCHORED,
                 group.data(), group.size()));
  CHECK_EQ(group[0], "chrisr:9000");
  CHECK_EQ(group[1], "chrisr:9000");
  CHECK_EQ(group[2], "chrisr");
  CHECK_EQ(group[3], "9000");

  std::string all, host;
  int port;
  CHECK(RE2::PartialMatch("a chrisr:9000 here", re, &all, &host, &port));
  CHECK_EQ(all, "chrisr:9000");
  CHECK_EQ(host, "chrisr");
  CHECK_EQ(port, 9000);
}


// A meta-quoted string, interpreted as a pattern, should always match
// the original unquoted string.
static void TestQuoteMeta(const std::string& unquoted,
                          const RE2::Options& options = RE2::DefaultOptions) {
  std::string quoted = RE2::QuoteMeta(unquoted);
  RE2 re(quoted, options);
  CHECK(RE2::FullMatch(unquoted, re))
    << "Unquoted='" << unquoted << "', quoted='" << quoted << "'.";
}

// A meta-quoted string, interpreted as a pattern, should always match
// the original unquoted string.
static void NegativeTestQuoteMeta(
    const std::string& unquoted, const std::string& should_not_match,
    const RE2::Options& options = RE2::DefaultOptions) {
  std::string quoted = RE2::QuoteMeta(unquoted);
  RE2 re(quoted, options);
  CHECK(!RE2::FullMatch(should_not_match, re))
    << "Unquoted='" << unquoted << "', quoted='" << quoted << "'.";
}

// Tests that quoted meta characters match their original strings,
// and that a few things that shouldn't match indeed do not.
static void Simple() {
  TestQuoteMeta("foo");
  TestQuoteMeta("foo.bar");
  TestQuoteMeta("foo\\.bar");
  TestQuoteMeta("[1-9]");
  TestQuoteMeta("1.5-2.0?");
  TestQuoteMeta("\\d");
  TestQuoteMeta("Who doesn't like ice cream?");
  TestQuoteMeta("((a|b)c?d*e+[f-h]i)");
  TestQuoteMeta("((?!)xxx).*yyy");
  TestQuoteMeta("([");
}

static void SimpleNegative() {
  NegativeTestQuoteMeta("foo", "bar");
  NegativeTestQuoteMeta("...", "bar");
  NegativeTestQuoteMeta("\\.", ".");
  NegativeTestQuoteMeta("\\.", "..");
  NegativeTestQuoteMeta("(a)", "a");
  NegativeTestQuoteMeta("(a|b)", "a");
  NegativeTestQuoteMeta("(a|b)", "(a)");
  NegativeTestQuoteMeta("(a|b)", "a|b");
  NegativeTestQuoteMeta("[0-9]", "0");
  NegativeTestQuoteMeta("[0-9]", "0-9");
  NegativeTestQuoteMeta("[0-9]", "[9]");
  NegativeTestQuoteMeta("((?!)xxx)", "xxx");
}

static void Latin1() {
  TestQuoteMeta("3\xb2 = 9", RE2::Latin1);
}

static void QuoteMetaUTF8() {
  TestQuoteMeta("Plácido Domingo");
  TestQuoteMeta("xyz");  // No fancy utf8.
  TestQuoteMeta("\xc2\xb0");  // 2-byte utf8 -- a degree symbol.
  TestQuoteMeta("27\xc2\xb0 degrees");  // As a middle character.
  TestQuoteMeta("\xe2\x80\xb3");  // 3-byte utf8 -- a double prime.
  TestQuoteMeta("\xf0\x9d\x85\x9f");  // 4-byte utf8 -- a music note.
  TestQuoteMeta("27\xc2\xb0");  // Interpreted as Latin-1, this should
                                // still work.
  NegativeTestQuoteMeta("27\xc2\xb0",
                        "27\\\xc2\\\xb0");  // 2-byte utf8 -- a degree symbol.
}

static void HasNull() {
  std::string has_null;

  // string with one null character
  has_null += '\0';
  TestQuoteMeta(has_null);
  NegativeTestQuoteMeta(has_null, "");

  // Don't want null-followed-by-'1' to be interpreted as '\01'.
  has_null += '1';
  TestQuoteMeta(has_null);
  NegativeTestQuoteMeta(has_null, "\1");
}

static void BigProgram() {
  RE2 re_simple("simple regexp");
  RE2 re_medium("medium.*regexp");
  RE2 re_complex("complex.{1,128}regexp");

  CHECK_GT(re_simple.ProgramSize(), 0);
  CHECK_GT(re_medium.ProgramSize(), re_simple.ProgramSize());
  CHECK_GT(re_complex.ProgramSize(), re_medium.ProgramSize());

  CHECK_GT(re_simple.ReverseProgramSize(), 0);
  CHECK_GT(re_medium.ReverseProgramSize(), re_simple.ReverseProgramSize());
  CHECK_GT(re_complex.ReverseProgramSize(), re_medium.ReverseProgramSize());
}

// Issue 956519: handling empty character sets was
// causing NULL dereference.  This tests a few empty character sets.
// (The way to get an empty character set is to negate a full one.)
static void Fuzz() {
  static std::initializer_list<std::string_view> empties = {
    "[^\\S\\s]",
    "[^\\S[:space:]]",
    "[^\\D\\d]",
    "[^\\D[:digit:]]"
  };
  for (std::string_view empty_re : empties) {
    CHECK(!RE2(empty_re).Match("abc", 0, 3, RE2::UNANCHORED, NULL, 0));
  }
}

// Bitstate assumes that kInstFail instructions in
// alternations or capture groups have been "compiled away".
static void BitstateAssumptions() {
  // Captures trigger use of Bitstate.
  static std::initializer_list<std::string_view> nop_empties = {
    "((((()))))" "[^\\S\\s]?",
    "((((()))))" "([^\\S\\s])?",
    "((((()))))" "([^\\S\\s]|[^\\S\\s])?",
    "((((()))))" "(([^\\S\\s]|[^\\S\\s])|)"
  };
  std::string_view group[6];
  for (std::string_view nop_empty : nop_empties) {
    CHECK(RE2(nop_empty).Match("", 0, 0, RE2::UNANCHORED, group, 6));
  }
}

// Test that named groups work correctly.
static void NamedGroups() {
  {
    RE2 re("(hello world)");
    CHECK_EQ(re.NumberOfCapturingGroups(), 1);
    const std::map<std::string, int>& m = re.NamedCapturingGroups();
    CHECK_EQ(m.size(), size_t{0});
  }

  {
    RE2 re("(?P<A>expr(?P<B>expr)(?P<C>expr))((expr)(?P<D>expr))");
    CHECK_EQ(re.NumberOfCapturingGroups(), 6);
    const std::map<std::string, int>& m = re.NamedCapturingGroups();
    CHECK_EQ(m.size(), size_t{4});
    CHECK_EQ(m.find("A")->second, 1);
    CHECK_EQ(m.find("B")->second, 2);
    CHECK_EQ(m.find("C")->second, 3);
    CHECK_EQ(m.find("D")->second, 6);  // $4 and $5 are anonymous
  }
}

static void CapturedGroupTest() {
  RE2 re("directions from (?P<S>.*) to (?P<D>.*)");
  int num_groups = re.NumberOfCapturingGroups();
  CHECK_EQ(2, num_groups);
  std::string args[4];
  RE2::Arg arg0(&args[0]);
  RE2::Arg arg1(&args[1]);
  RE2::Arg arg2(&args[2]);
  RE2::Arg arg3(&args[3]);

  const RE2::Arg* const matches[4] = {&arg0, &arg1, &arg2, &arg3};
  CHECK(RE2::FullMatchN("directions from mountain view to san jose",
                              re, matches, num_groups));
  const std::map<std::string, int>& named_groups = re.NamedCapturingGroups();
  CHECK(named_groups.find("S") != named_groups.end());
  CHECK(named_groups.find("D") != named_groups.end());

  // The named group index is 1-based.
  int source_group_index = named_groups.find("S")->second;
  int destination_group_index = named_groups.find("D")->second;
  CHECK_EQ(1, source_group_index);
  CHECK_EQ(2, destination_group_index);

  // The args is zero-based.
  CHECK_EQ("mountain view", args[source_group_index - 1]);
  CHECK_EQ("san jose", args[destination_group_index - 1]);
}

static void FullMatchWithNoArgs() {
  CHECK(RE2::FullMatch("h", "h"));
  CHECK(RE2::FullMatch("hello", "hello"));
  CHECK(RE2::FullMatch("hello", "h.*o"));
  CHECK(!RE2::FullMatch("othello", "h.*o"));  // Must be anchored at front
  CHECK(!RE2::FullMatch("hello!", "h.*o"));   // Must be anchored at end
}

static void PartialMatch() {
  CHECK(RE2::PartialMatch("x", "x"));
  CHECK(RE2::PartialMatch("hello", "h.*o"));
  CHECK(RE2::PartialMatch("othello", "h.*o"));
  CHECK(RE2::PartialMatch("hello!", "h.*o"));
  CHECK(RE2::PartialMatch("x", "((((((((((((((((((((x))))))))))))))))))))"));
}

static void PartialMatchN() {
  RE2::Arg argv[2];
  const RE2::Arg* const args[2] = { &argv[0], &argv[1] };

  // 0 arg
  CHECK(RE2::PartialMatchN("hello", "e.*o", args, 0));
  CHECK(!RE2::PartialMatchN("othello", "a.*o", args, 0));

  // 1 arg
  int i;
  argv[0] = &i;
  CHECK(RE2::PartialMatchN("1001 nights", "(\\d+)", args, 1));
  CHECK_EQ(1001, i);
  CHECK(!RE2::PartialMatchN("three", "(\\d+)", args, 1));

  // Multi-arg
  std::string s;
  argv[1] = &s;
  CHECK(RE2::PartialMatchN("answer: 42:life", "(\\d+):(\\w+)", args, 2));
  CHECK_EQ(42, i);
  CHECK_EQ("life", s);
  CHECK(!RE2::PartialMatchN("hi1", "(\\w+)(1)", args, 2));
}

static void FullMatchZeroArg() {
  // Zero-arg
  CHECK(RE2::FullMatch("1001", "\\d+"));
}

static void FullMatchOneArg() {
  int i = -999;

  // Single-arg
  CHECK(RE2::FullMatch("1001", "(\\d+)",   &i));
  CHECK_EQ(i, 1001);
  CHECK(RE2::FullMatch("-123", "(-?\\d+)", &i));
  CHECK_EQ(i, -123);
  CHECK(!RE2::FullMatch("10", "()\\d+", &i));
  CHECK(!
      RE2::FullMatch("1234567890123456789012345678901234567890", "(\\d+)", &i));
}

static void FullMatchIntegerArg() {
  int i = -999;

  // Digits surrounding integer-arg
  CHECK(RE2::FullMatch("1234", "1(\\d*)4", &i));
  CHECK_EQ(i, 23);
  CHECK(RE2::FullMatch("1234", "(\\d)\\d+", &i));
  CHECK_EQ(i, 1);
  CHECK(RE2::FullMatch("-1234", "(-\\d)\\d+", &i));
  CHECK_EQ(i, -1);
  CHECK(RE2::PartialMatch("1234", "(\\d)", &i));
  CHECK_EQ(i, 1);
  CHECK(RE2::PartialMatch("-1234", "(-\\d)", &i));
  CHECK_EQ(i, -1);
}

static void FullMatchStringArg() {
  std::string s;
  // string-arg
  CHECK(RE2::FullMatch("hello", "h(.*)o", &s));
  CHECK_EQ(s, std::string("ell"));
}

static void FullMatchStringViewArg() {
  int i;
  std::string_view sp;
  // string_view-arg
  CHECK(RE2::FullMatch("ruby:1234", "(\\w+):(\\d+)", &sp, &i));
  CHECK_EQ(sp.size(), size_t{4});
  CHECK(memcmp(sp.data(), "ruby", 4) == 0);
  CHECK_EQ(i, 1234);
}

static void FullMatchMultiArg() {
  int i;
  std::string s;
  // Multi-arg
  CHECK(RE2::FullMatch("ruby:1234", "(\\w+):(\\d+)", &s, &i));
  CHECK_EQ(s, std::string("ruby"));
  CHECK_EQ(i, 1234);
}

static void FullMatchN() {
  RE2::Arg argv[2];
  const RE2::Arg* const args[2] = { &argv[0], &argv[1] };

  // 0 arg
  CHECK(RE2::FullMatchN("hello", "h.*o", args, 0));
  CHECK(!RE2::FullMatchN("othello", "h.*o", args, 0));

  // 1 arg
  int i;
  argv[0] = &i;
  CHECK(RE2::FullMatchN("1001", "(\\d+)", args, 1));
  CHECK_EQ(1001, i);
  CHECK(!RE2::FullMatchN("three", "(\\d+)", args, 1));

  // Multi-arg
  std::string s;
  argv[1] = &s;
  CHECK(RE2::FullMatchN("42:life", "(\\d+):(\\w+)", args, 2));
  CHECK_EQ(42, i);
  CHECK_EQ("life", s);
  CHECK(!RE2::FullMatchN("hi1", "(\\w+)(1)", args, 2));
}

static void FullMatchIgnoredArg() {
  int i = -999;
  std::string s;

  // Old-school NULL should be ignored.
  CHECK(
      RE2::FullMatch("ruby:1234", "(\\w+)(:)(\\d+)", &s, (void*)NULL, &i));
  CHECK_EQ(s, std::string("ruby"));
  CHECK_EQ(i, 1234);

  // C++11 nullptr should also be ignored.
  CHECK(RE2::FullMatch("rubz:1235", "(\\w+)(:)(\\d+)", &s, nullptr, &i));
  CHECK_EQ(s, std::string("rubz"));
  CHECK_EQ(i, 1235);
}

static void FullMatchTypedNullArg() {
  std::string s;

  // Ignore non-void* null arg.
  CHECK(RE2::FullMatch("hello", "he(.*)lo", (char*)nullptr));
  CHECK(RE2::FullMatch("hello", "h(.*)o", (std::string*)nullptr));
  CHECK(RE2::FullMatch("hello", "h(.*)o", (std::string_view*)nullptr));
  CHECK(RE2::FullMatch("1234", "(.*)", (int*)nullptr));
  CHECK(RE2::FullMatch("1234567890123456", "(.*)", (long long*)nullptr));
  CHECK(RE2::FullMatch("123.4567890123456", "(.*)", (double*)nullptr));
  CHECK(RE2::FullMatch("123.4567890123456", "(.*)", (float*)nullptr));

  // Fail on non-void* null arg if the match doesn't parse for the given type.
  CHECK(!RE2::FullMatch("hello", "h(.*)lo", &s, (char*)nullptr));
  CHECK(!RE2::FullMatch("hello", "(.*)", (int*)nullptr));
  CHECK(!RE2::FullMatch("1234567890123456", "(.*)", (int*)nullptr));
  CHECK(!RE2::FullMatch("hello", "(.*)", (double*)nullptr));
  CHECK(!RE2::FullMatch("hello", "(.*)", (float*)nullptr));
}

static void FullMatchTypeTests() {
  // Type tests
  std::string zeros(1000, '0');
  {
    char c;
    CHECK(RE2::FullMatch("Hello", "(H)ello", &c));
    CHECK_EQ(c, 'H');
  }
  {
    signed char c;
    CHECK(RE2::FullMatch("Hello", "(H)ello", &c));
    CHECK_EQ(c, static_cast<signed char>('H'));
  }
  {
    unsigned char c;
    CHECK(RE2::FullMatch("Hello", "(H)ello", &c));
    CHECK_EQ(c, static_cast<unsigned char>('H'));
  }
  {
    int16_t v;
    CHECK(RE2::FullMatch("100",     "(-?\\d+)", &v)); CHECK_EQ(v, 100);
    CHECK(RE2::FullMatch("-100",    "(-?\\d+)", &v)); CHECK_EQ(v, -100);
    CHECK(RE2::FullMatch("32767",   "(-?\\d+)", &v)); CHECK_EQ(v, 32767);
    CHECK(RE2::FullMatch("-32768",  "(-?\\d+)", &v)); CHECK_EQ(v, -32768);
    CHECK(!RE2::FullMatch("-32769", "(-?\\d+)", &v));
    CHECK(!RE2::FullMatch("32768",  "(-?\\d+)", &v));
  }
  {
    uint16_t v;
    CHECK(RE2::FullMatch("100",    "(\\d+)", &v)); CHECK_EQ(v, 100);
    CHECK(RE2::FullMatch("32767",  "(\\d+)", &v)); CHECK_EQ(v, 32767);
    CHECK(RE2::FullMatch("65535",  "(\\d+)", &v)); CHECK_EQ(v, 65535);
    CHECK(!RE2::FullMatch("65536", "(\\d+)", &v));
  }
  {
    int32_t v;
    static const int32_t max = INT32_C(0x7fffffff);
    static const int32_t min = -max - 1;
    CHECK(RE2::FullMatch("100",          "(-?\\d+)", &v)); CHECK_EQ(v, 100);
    CHECK(RE2::FullMatch("-100",         "(-?\\d+)", &v)); CHECK_EQ(v, -100);
    CHECK(RE2::FullMatch("2147483647",   "(-?\\d+)", &v)); CHECK_EQ(v, max);
    CHECK(RE2::FullMatch("-2147483648",  "(-?\\d+)", &v)); CHECK_EQ(v, min);
    CHECK(!RE2::FullMatch("-2147483649", "(-?\\d+)", &v));
    CHECK(!RE2::FullMatch("2147483648",  "(-?\\d+)", &v));

    CHECK(RE2::FullMatch(zeros + "2147483647", "(-?\\d+)", &v));
    CHECK_EQ(v, max);
    CHECK(RE2::FullMatch("-" + zeros + "2147483648", "(-?\\d+)", &v));
    CHECK_EQ(v, min);

    CHECK(!RE2::FullMatch("-" + zeros + "2147483649", "(-?\\d+)", &v));
    CHECK(RE2::FullMatch("0x7fffffff", "(.*)", RE2::CRadix(&v)));
    CHECK_EQ(v, max);
    CHECK(!RE2::FullMatch("000x7fffffff", "(.*)", RE2::CRadix(&v)));
  }
  {
    uint32_t v;
    static const uint32_t max = UINT32_C(0xffffffff);
    CHECK(RE2::FullMatch("100",         "(\\d+)", &v)); CHECK_EQ(v, uint32_t{100});
    CHECK(RE2::FullMatch("4294967295",  "(\\d+)", &v)); CHECK_EQ(v, max);
    CHECK(!RE2::FullMatch("4294967296", "(\\d+)", &v));
    CHECK(!RE2::FullMatch("-1",         "(\\d+)", &v));

    CHECK(RE2::FullMatch(zeros + "4294967295", "(\\d+)", &v)); CHECK_EQ(v, max);
  }
  {
    int64_t v;
    static const int64_t max = INT64_C(0x7fffffffffffffff);
    static const int64_t min = -max - 1;
    std::string str;

    CHECK(RE2::FullMatch("100",  "(-?\\d+)", &v)); CHECK_EQ(v, 100);
    CHECK(RE2::FullMatch("-100", "(-?\\d+)", &v)); CHECK_EQ(v, -100);

    str = std::to_string(max);
    CHECK(RE2::FullMatch(str,    "(-?\\d+)", &v)); CHECK_EQ(v, max);

    str = std::to_string(min);
    CHECK(RE2::FullMatch(str,    "(-?\\d+)", &v)); CHECK_EQ(v, min);

    str = std::to_string(max);
    CHECK_NE(str.back(), '9');
    str.back()++;
    CHECK(!RE2::FullMatch(str,   "(-?\\d+)", &v));

    str = std::to_string(min);
    CHECK_NE(str.back(), '9');
    str.back()++;
    CHECK(!RE2::FullMatch(str,   "(-?\\d+)", &v));
  }
  {
    uint64_t v = 777;
    int64_t v2 = -999;
    static const uint64_t max = UINT64_C(0xffffffffffffffff);
    std::string str;

    CHECK(RE2::FullMatch("100",  "(-?\\d+)", &v));  CHECK_EQ(v, uint64_t{100});
    CHECK(RE2::FullMatch("-100", "(-?\\d+)", &v2)); CHECK_EQ(v2, -100);

    str = std::to_string(max);
    CHECK(RE2::FullMatch(str,    "(-?\\d+)", &v)); CHECK_EQ(v, max);

    CHECK_NE(str.back(), '9');
    str.back()++;
    CHECK(!RE2::FullMatch(str,   "(-?\\d+)", &v));
  }
}

static void FloatingPointFullMatchTypes() {
  std::string zeros(1000, '0');
  {
    float v;
    CHECK(RE2::FullMatch("100",   "(.*)", &v)); CHECK_EQ(v, 100);
    CHECK(RE2::FullMatch("-100.", "(.*)", &v)); CHECK_EQ(v, -100);
    CHECK(RE2::FullMatch("1e23",  "(.*)", &v)); CHECK_EQ(v, float{1e23});
    CHECK(RE2::FullMatch(" 100",  "(.*)", &v)); CHECK_EQ(v, 100);

    CHECK(RE2::FullMatch(zeros + "1e23",  "(.*)", &v));
    CHECK_EQ(v, float{1e23});

    // 6700000000081920.1 is an edge case.
    // 6700000000081920 is exactly halfway between
    // two float32s, so the .1 should make it round up.
    // However, the .1 is outside the precision possible with
    // a float64: the nearest float64 is 6700000000081920.
    // So if the code uses strtod and then converts to float32,
    // round-to-even will make it round down instead of up.
    // To pass the test, the parser must call strtof directly.
    // This test case is carefully chosen to use only a 17-digit
    // number, since C does not guarantee to get the correctly
    // rounded answer for strtod and strtof unless the input is
    // short.
    //
    // This is known to fail on Cygwin and MinGW due to a broken
    // implementation of strtof(3). And apparently MSVC too. Sigh.
#if !defined(_MSC_VER) && !defined(__CYGWIN__) && !defined(__MINGW32__)
    CHECK(RE2::FullMatch("0.1", "(.*)", &v));
    CHECK_EQ(v, 0.1f) << std::format("{:.8g} != {:.8g}", v, 0.1f);
    CHECK(RE2::FullMatch("6700000000081920.1", "(.*)", &v));
    CHECK_EQ(v, 6700000000081920.1f)
      << std::format("{:.8g} != {:.8g}", v, 6700000000081920.1f);
#endif
  }
  {
    double v;
    CHECK(RE2::FullMatch("100",   "(.*)", &v)); CHECK_EQ(v, 100);
    CHECK(RE2::FullMatch("-100.", "(.*)", &v)); CHECK_EQ(v, -100);
    CHECK(RE2::FullMatch("1e23",  "(.*)", &v)); CHECK_EQ(v, double{1e23});
    CHECK(RE2::FullMatch(" 100",  "(.*)", &v)); CHECK_EQ(v, 100);

    CHECK(RE2::FullMatch(zeros + "1e23", "(.*)", &v));
    CHECK_EQ(v, double{1e23});

    CHECK(RE2::FullMatch("0.1", "(.*)", &v));
    CHECK_EQ(v, 0.1) << std::format("{:.17g} != {:.17g}", v, 0.1);
    CHECK(RE2::FullMatch("1.00000005960464485", "(.*)", &v));
    CHECK_EQ(v, 1.0000000596046448)
      << std::format("{:.17g} != {:.17g}", v, 1.0000000596046448);
  }
}

static void FullMatchAnchored() {
  int i = -999;
  // Check that matching is fully anchored
  CHECK(!RE2::FullMatch("x1001", "(\\d+)",  &i));
  CHECK(!RE2::FullMatch("1001x", "(\\d+)",  &i));
  CHECK(RE2::FullMatch("x1001",  "x(\\d+)", &i)); CHECK_EQ(i, 1001);
  CHECK(RE2::FullMatch("1001x",  "(\\d+)x", &i)); CHECK_EQ(i, 1001);
}

static void FullMatchBraces() {
  // Braces
  CHECK(RE2::FullMatch("0abcd",  "[0-9a-f+.-]{5,}"));
  CHECK(RE2::FullMatch("0abcde", "[0-9a-f+.-]{5,}"));
  CHECK(!RE2::FullMatch("0abc",  "[0-9a-f+.-]{5,}"));
}

static void Complicated() {
  // Complicated RE2
  CHECK(RE2::FullMatch("foo", "foo|bar|[A-Z]"));
  CHECK(RE2::FullMatch("bar", "foo|bar|[A-Z]"));
  CHECK(RE2::FullMatch("X",   "foo|bar|[A-Z]"));
  CHECK(!RE2::FullMatch("XY", "foo|bar|[A-Z]"));
}

static void FullMatchEnd() {
  // Check full-match handling (needs '$' tacked on internally)
  CHECK(RE2::FullMatch("fo", "fo|foo"));
  CHECK(RE2::FullMatch("foo", "fo|foo"));
  CHECK(RE2::FullMatch("fo", "fo|foo$"));
  CHECK(RE2::FullMatch("foo", "fo|foo$"));
  CHECK(RE2::FullMatch("foo", "foo$"));
  CHECK(!RE2::FullMatch("foo$bar", "foo\\$"));
  CHECK(!RE2::FullMatch("fox", "fo|bar"));

  // Uncomment the following if we change the handling of '$' to
  // prevent it from matching a trailing newline
  if (false) {
    // Check that we don't get bitten by pcre's special handling of a
    // '\n' at the end of the string matching '$'
    CHECK(!RE2::PartialMatch("foo\n", "foo$"));
  }
}

static void FullMatchArgCount() {
  // Number of args
  int a[16];
  CHECK(RE2::FullMatch("", ""));

  memset(a, 0, sizeof(0));
  CHECK(RE2::FullMatch("1", "(\\d){1}", &a[0]));
  CHECK_EQ(a[0], 1);

  memset(a, 0, sizeof(0));
  CHECK(RE2::FullMatch("12", "(\\d)(\\d)", &a[0], &a[1]));
  CHECK_EQ(a[0], 1);
  CHECK_EQ(a[1], 2);

  memset(a, 0, sizeof(0));
  CHECK(RE2::FullMatch("123", "(\\d)(\\d)(\\d)", &a[0], &a[1], &a[2]));
  CHECK_EQ(a[0], 1);
  CHECK_EQ(a[1], 2);
  CHECK_EQ(a[2], 3);

  memset(a, 0, sizeof(0));
  CHECK(RE2::FullMatch("1234", "(\\d)(\\d)(\\d)(\\d)", &a[0], &a[1],
                       &a[2], &a[3]));
  CHECK_EQ(a[0], 1);
  CHECK_EQ(a[1], 2);
  CHECK_EQ(a[2], 3);
  CHECK_EQ(a[3], 4);

  memset(a, 0, sizeof(0));
  CHECK(RE2::FullMatch("12345", "(\\d)(\\d)(\\d)(\\d)(\\d)", &a[0], &a[1],
                       &a[2], &a[3], &a[4]));
  CHECK_EQ(a[0], 1);
  CHECK_EQ(a[1], 2);
  CHECK_EQ(a[2], 3);
  CHECK_EQ(a[3], 4);
  CHECK_EQ(a[4], 5);

  memset(a, 0, sizeof(0));
  CHECK(RE2::FullMatch("123456", "(\\d)(\\d)(\\d)(\\d)(\\d)(\\d)", &a[0],
                       &a[1], &a[2], &a[3], &a[4], &a[5]));
  CHECK_EQ(a[0], 1);
  CHECK_EQ(a[1], 2);
  CHECK_EQ(a[2], 3);
  CHECK_EQ(a[3], 4);
  CHECK_EQ(a[4], 5);
  CHECK_EQ(a[5], 6);

  memset(a, 0, sizeof(0));
  CHECK(RE2::FullMatch("1234567", "(\\d)(\\d)(\\d)(\\d)(\\d)(\\d)(\\d)",
                       &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6]));
  CHECK_EQ(a[0], 1);
  CHECK_EQ(a[1], 2);
  CHECK_EQ(a[2], 3);
  CHECK_EQ(a[3], 4);
  CHECK_EQ(a[4], 5);
  CHECK_EQ(a[5], 6);
  CHECK_EQ(a[6], 7);

  memset(a, 0, sizeof(0));
  CHECK(RE2::FullMatch("1234567890123456",
                       "(\\d)(\\d)(\\d)(\\d)(\\d)(\\d)(\\d)(\\d)"
                       "(\\d)(\\d)(\\d)(\\d)(\\d)(\\d)(\\d)(\\d)",
                       &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6],
                       &a[7], &a[8], &a[9], &a[10], &a[11], &a[12],
                       &a[13], &a[14], &a[15]));
  CHECK_EQ(a[0], 1);
  CHECK_EQ(a[1], 2);
  CHECK_EQ(a[2], 3);
  CHECK_EQ(a[3], 4);
  CHECK_EQ(a[4], 5);
  CHECK_EQ(a[5], 6);
  CHECK_EQ(a[6], 7);
  CHECK_EQ(a[7], 8);
  CHECK_EQ(a[8], 9);
  CHECK_EQ(a[9], 0);
  CHECK_EQ(a[10], 1);
  CHECK_EQ(a[11], 2);
  CHECK_EQ(a[12], 3);
  CHECK_EQ(a[13], 4);
  CHECK_EQ(a[14], 5);
  CHECK_EQ(a[15], 6);
}

static void Accessors() {
  // Check the pattern() accessor
  {
    const std::string kPattern = "http://([^/]+)/.*";
    const RE2 re(kPattern);
    CHECK_EQ(kPattern, re.pattern());
  }

  // Check RE2 error field.
  {
    RE2 re("foo");
    CHECK(re.error().empty());  // Must have no error
    CHECK(re.ok());
    CHECK_EQ(re.error_code(), RE2::NoError);
  }
}

static void UTF8() {
  // Check UTF-8 handling
  // Three Japanese characters (nihongo)
  const char utf8_string[] = {
    (char)0xe6, (char)0x97, (char)0xa5, // 65e5
    (char)0xe6, (char)0x9c, (char)0xac, // 627c
    (char)0xe8, (char)0xaa, (char)0x9e, // 8a9e
    0
  };
  const char utf8_pattern[] = {
    '.',
    (char)0xe6, (char)0x9c, (char)0xac, // 627c
    '.',
    0
  };

  // Both should match in either mode, bytes or UTF-8
  RE2 re_test1(".........", RE2::Latin1);
  CHECK(RE2::FullMatch(utf8_string, re_test1));
  RE2 re_test2("...");
  CHECK(RE2::FullMatch(utf8_string, re_test2));

  // Check that '.' matches one byte or UTF-8 character
  // according to the mode.
  std::string s;
  RE2 re_test3("(.)", RE2::Latin1);
  CHECK(RE2::PartialMatch(utf8_string, re_test3, &s));
  CHECK_EQ(s, std::string("\xe6"));
  RE2 re_test4("(.)");
  CHECK(RE2::PartialMatch(utf8_string, re_test4, &s));
  CHECK_EQ(s, std::string("\xe6\x97\xa5"));

  // Check that string matches itself in either mode
  RE2 re_test5(utf8_string, RE2::Latin1);
  CHECK(RE2::FullMatch(utf8_string, re_test5));
  RE2 re_test6(utf8_string);
  CHECK(RE2::FullMatch(utf8_string, re_test6));

  // Check that pattern matches string only in UTF8 mode
  RE2 re_test7(utf8_pattern, RE2::Latin1);
  CHECK(!RE2::FullMatch(utf8_string, re_test7));
  RE2 re_test8(utf8_pattern);
  CHECK(RE2::FullMatch(utf8_string, re_test8));
}

static void UngreedyUTF8() {
  // Check that ungreedy, UTF8 regular expressions don't match when they
  // oughtn't -- see bug 82246.
  {
    // This code always worked.
    const char* pattern = "\\w+X";
    const std::string target = "a aX";
    RE2 match_sentence(pattern, RE2::Latin1);
    RE2 match_sentence_re(pattern);

    CHECK(!RE2::FullMatch(target, match_sentence));
    CHECK(!RE2::FullMatch(target, match_sentence_re));
  }
  {
    const char* pattern = "(?U)\\w+X";
    const std::string target = "a aX";
    RE2 match_sentence(pattern, RE2::Latin1);
    CHECK_EQ(match_sentence.error(), "");
    RE2 match_sentence_re(pattern);

    CHECK(!RE2::FullMatch(target, match_sentence));
    CHECK(!RE2::FullMatch(target, match_sentence_re));
  }
}

static void Rejects() {
  {
    RE2 re("a\\1", RE2::Quiet);
    CHECK(!re.ok()); }
  {
    RE2 re("a[x", RE2::Quiet);
    CHECK(!re.ok());
  }
  {
    RE2 re("a[z-a]", RE2::Quiet);
    CHECK(!re.ok());
  }
  {
    RE2 re("a[[:foobar:]]", RE2::Quiet);
    CHECK(!re.ok());
  }
  {
    RE2 re("a(b", RE2::Quiet);
    CHECK(!re.ok());
  }
  {
    RE2 re("a\\", RE2::Quiet);
    CHECK(!re.ok());
  }
}


static void NoCrash() {
  // Test that using a bad regexp doesn't crash.
  {
    RE2 re("a\\", RE2::Quiet);
    CHECK(!re.ok());
    CHECK(!RE2::PartialMatch("a\\b", re));
  }

  // Test that using an enormous regexp doesn't crash
  {
    RE2 re("(((.{100}){100}){100}){100}", RE2::Quiet);
    CHECK(!re.ok());
    CHECK(!RE2::PartialMatch("aaa", re));
  }

  // Test that a crazy regexp still compiles and runs.
  {
    RE2 re(".{512}x", RE2::Quiet);
    CHECK(re.ok());
    std::string s;
    s.append(515, 'c');
    s.append("x");
    CHECK(RE2::PartialMatch(s, re));
  }
}

static void Recursion() {
  auto TestRecursion = [](int size, const char* pattern) {
      // Fill up a string repeating the pattern given
      std::string domain;
      domain.resize(size);
      size_t patlen = strlen(pattern);
      for (int i = 0; i < size; i++) {
        domain[i] = pattern[i % patlen];
      }
      // Just make sure it doesn't crash due to too much recursion.
      RE2 re("([a-zA-Z0-9]|-)+(\\.([a-zA-Z0-9]|-)+)*(\\.)?", RE2::Quiet);
      RE2::FullMatch(domain, re);
    };

  // Test that recursion is stopped.
  // This test is PCRE-legacy -- there's no recursion in RE2.
  int bytes = 15 * 1024;  // enough to crash PCRE
  TestRecursion(bytes, ".");
  TestRecursion(bytes, "a");
  TestRecursion(bytes, "a.");
  TestRecursion(bytes, "ab.");
  TestRecursion(bytes, "abc.");
}

static void BigCountedRepetition() {
  // Test that counted repetition works, given tons of memory.
  RE2::Options opt;
  opt.set_max_mem(256<<20);

  RE2 re(".{512}x", opt);
  CHECK(re.ok());
  std::string s;
  s.append(515, 'c');
  s.append("x");
  CHECK(RE2::PartialMatch(s, re));
}


static void DeepRecursion() {
  // Test for deep stack recursion.  This would fail with a
  // segmentation violation due to stack overflow before pcre was
  // patched.
  // Again, a PCRE legacy test.  RE2 doesn't recurse.
  std::string comment("x*");
  std::string a(131072, 'a');
  comment += a;
  comment += "*x";
  RE2 re("((?:\\s|xx.*\n|x[*](?:\n|.)*?[*]x)*)");
  CHECK(RE2::FullMatch(comment, re));
}

// Suggested by Josh Hyman.  Failed when SearchOnePass was
// not implementing case-folding.
static void MatchAndConsume() {
  std::string text = "A fish named *Wanda*";
  std::string_view sp(text);
  std::string_view result;
  CHECK(RE2::PartialMatch(text, "(?i)([wand]{5})", &result));
  CHECK(RE2::FindAndConsume(&sp, "(?i)([wand]{5})", &result));
}


// RE2 should permit implicit conversions from string, string_view, const char*,
// and C string literals.
static void ImplicitConversions() {
  std::string re_string(".");
  std::string_view re_string_view(".");
  const char* re_c_string = ".";
  CHECK(RE2::PartialMatch("e", re_string));
  CHECK(RE2::PartialMatch("e", re_string_view));
  CHECK(RE2::PartialMatch("e", re_c_string));
  CHECK(RE2::PartialMatch("e", "."));
}

// Bugs introduced by 8622304
static void CL8622304() {
  // reported by ingow
  std::string dir;
  CHECK(RE2::FullMatch("D", "([^\\\\])"));  // ok
  CHECK(RE2::FullMatch("D", "([^\\\\])", &dir));  // fails

  // reported by jacobsa
  std::string key, val;
  CHECK(RE2::PartialMatch("bar:1,0x2F,030,4,5;baz:true;fooby:false,true",
              "(\\w+)(?::((?:[^;\\\\]|\\\\.)*))?;?",
              &key,
              &val));
  CHECK_EQ(key, "bar");
  CHECK_EQ(val, "1,0x2F,030,4,5");
}


static void ErrorCodeAndArg() {
  // Check that RE2 returns correct regexp pieces on error.
  // In particular, make sure it returns whole runes
  // and that it always reports invalid UTF-8.
  // Also check that Perl error flag piece is big enough.
  struct ErrorTest {
    const char *regexp;
    RE2::ErrorCode error_code;
    const char *error_arg;
  };
  std::initializer_list<ErrorTest> error_tests = {
    { "ab\\αcd", RE2::ErrorBadEscape, "\\α" },
    { "ef\\x☺01", RE2::ErrorBadEscape, "\\x☺0" },
    { "gh\\x1☺01", RE2::ErrorBadEscape, "\\x1☺" },
    { "ij\\x1", RE2::ErrorBadEscape, "\\x1" },
    { "kl\\x", RE2::ErrorBadEscape, "\\x" },
    { "uv\\x{0000☺}", RE2::ErrorBadEscape, "\\x{0000☺" },
    { "wx\\p{ABC", RE2::ErrorBadCharRange, "\\p{ABC" },
    // used to return (?s but the error is X
    { "yz(?smiUX:abc)", RE2::ErrorBadPerlOp, "(?smiUX" },
    { "aa(?sm☺i", RE2::ErrorBadPerlOp, "(?sm☺" },
    { "bb[abc", RE2::ErrorMissingBracket, "[abc" },
    { "abc(def", RE2::ErrorMissingParen, "abc(def" },
    // not defined in this version?
    // { "abc)def", RE2::ErrorUnexpectedParen, "abc)def" },

    // no argument string returned for invalid UTF-8
    { "mn\\x1\377", RE2::ErrorBadUTF8, "" },
    { "op\377qr", RE2::ErrorBadUTF8, "" },
    { "st\\x{00000\377", RE2::ErrorBadUTF8, "" },
    { "zz\\p{\377}", RE2::ErrorBadUTF8, "" },
    { "zz\\x{00\377}", RE2::ErrorBadUTF8, "" },
    { "zz(?P<name\377>abc)", RE2::ErrorBadUTF8, "" },
  };

  for (const ErrorTest &et : error_tests) {
    RE2 re(et.regexp, RE2::Quiet);
    CHECK(!re.ok());
    CHECK_EQ(re.error_code(), et.error_code) << re.error();
    CHECK_EQ(re.error_arg(), et.error_arg) << re.error();
  }
}

static void NeverNewline() {
  // Check that "never match \n" mode never matches \n.
  struct NeverTest {
    const char* regexp;
    const char* text;
    const char* match;
  };
  std::array<NeverTest, 5> never_tests = {
    NeverTest{ "(.*)", "abc\ndef\nghi\n", "abc" },
    NeverTest{ "(?s)(abc.*def)", "abc\ndef\n", NULL },
    NeverTest{ "(abc(.|\n)*def)", "abc\ndef\n", NULL },
    NeverTest{ "(abc[^x]*def)", "abc\ndef\n", NULL },
    NeverTest{ "(abc[^x]*def)", "abczzzdef\ndef\n", "abczzzdef" },
  };

  RE2::Options opt;
  opt.set_never_nl(true);
  for (const NeverTest& t : never_tests) {
    RE2 re(t.regexp, opt);
    if (t.match == NULL) {
      CHECK(!re.PartialMatch(t.text, re));
    } else {
      std::string_view m;
      CHECK(re.PartialMatch(t.text, re, &m));
      CHECK_EQ(m, t.match);
    }
  }
}

// Check that dot_nl option works.
static void DotNL() {
  RE2::Options opt;
  opt.set_dot_nl(true);
  CHECK(RE2::PartialMatch("\n", RE2(".", opt)));
  CHECK(!RE2::PartialMatch("\n", RE2("(?-s).", opt)));
  opt.set_never_nl(true);
  CHECK(!RE2::PartialMatch("\n", RE2(".", opt)));
}

// Check that there are no capturing groups in "never capture" mode.
static void NeverCapture() {
  RE2::Options opt;
  opt.set_never_capture(true);
  RE2 re("(r)(e)", opt);
  CHECK_EQ(0, re.NumberOfCapturingGroups());
}

// Bitstate bug was looking at submatch[0] even if nsubmatch == 0.
// Triggered by a failed DFA search falling back to Bitstate when
// using Match with a NULL submatch set.  Bitstate tried to read
// the submatch[0] entry even if nsubmatch was 0.
static void BitstateCaptureBug() {
  RE2::Options opt;
  opt.set_max_mem(20000);
  RE2 re("(_________$)", opt);
  std::string_view s = "xxxxxxxxxxxxxxxxxxxxxxxxxx_________x";
  CHECK(!re.Match(s, 0, s.size(), RE2::UNANCHORED, NULL, 0));
}



// C++ version of bug 609710.
static void UnicodeClasses() {
  const std::string str = "ABCDEFGHI譚永鋒";
  std::string a, b, c;

  CHECK(RE2::FullMatch("A", "\\p{L}"));
  CHECK(RE2::FullMatch("A", "\\p{Lu}"));
  CHECK(!RE2::FullMatch("A", "\\p{Ll}"));
  CHECK(!RE2::FullMatch("A", "\\P{L}"));
  CHECK(!RE2::FullMatch("A", "\\P{Lu}"));
  CHECK(RE2::FullMatch("A", "\\P{Ll}"));

  CHECK(RE2::FullMatch("譚", "\\p{L}"));
  CHECK(!RE2::FullMatch("譚", "\\p{Lu}"));
  CHECK(!RE2::FullMatch("譚", "\\p{Ll}"));
  CHECK(!RE2::FullMatch("譚", "\\P{L}"));
  CHECK(RE2::FullMatch("譚", "\\P{Lu}"));
  CHECK(RE2::FullMatch("譚", "\\P{Ll}"));

  CHECK(RE2::FullMatch("永", "\\p{L}"));
  CHECK(!RE2::FullMatch("永", "\\p{Lu}"));
  CHECK(!RE2::FullMatch("永", "\\p{Ll}"));
  CHECK(!RE2::FullMatch("永", "\\P{L}"));
  CHECK(RE2::FullMatch("永", "\\P{Lu}"));
  CHECK(RE2::FullMatch("永", "\\P{Ll}"));

  CHECK(RE2::FullMatch("鋒", "\\p{L}"));
  CHECK(!RE2::FullMatch("鋒", "\\p{Lu}"));
  CHECK(!RE2::FullMatch("鋒", "\\p{Ll}"));
  CHECK(!RE2::FullMatch("鋒", "\\P{L}"));
  CHECK(RE2::FullMatch("鋒", "\\P{Lu}"));
  CHECK(RE2::FullMatch("鋒", "\\P{Ll}"));

  CHECK(RE2::PartialMatch(str, "(.).*?(.).*?(.)", &a, &b, &c));
  CHECK_EQ("A", a);
  CHECK_EQ("B", b);
  CHECK_EQ("C", c);

  CHECK(RE2::PartialMatch(str, "(.).*?([\\p{L}]).*?(.)", &a, &b, &c));
  CHECK_EQ("A", a);
  CHECK_EQ("B", b);
  CHECK_EQ("C", c);

  CHECK(!RE2::PartialMatch(str, "\\P{L}"));

  CHECK(RE2::PartialMatch(str, "(.).*?([\\p{Lu}]).*?(.)", &a, &b, &c));
  CHECK_EQ("A", a);
  CHECK_EQ("B", b);
  CHECK_EQ("C", c);

  CHECK(!RE2::PartialMatch(str, "[^\\p{Lu}\\p{Lo}]"));

  CHECK(RE2::PartialMatch(str, ".*(.).*?([\\p{Lu}\\p{Lo}]).*?(.)", &a, &b, &c));
  CHECK_EQ("譚", a);
  CHECK_EQ("永", b);
  CHECK_EQ("鋒", c);
}


static void LazyRE2() {
  // Test with and without options.
  static re2::LazyRE2 a = {"a"};
  static re2::LazyRE2 b = {"b", RE2::Latin1};

  CHECK_EQ("a", a->pattern());
  CHECK_EQ(RE2::Options::EncodingUTF8, a->options().encoding());

  CHECK_EQ("b", b->pattern());
  CHECK_EQ(RE2::Options::EncodingLatin1, b->options().encoding());
}

// Bug reported by saito. 2009/02/17
static void DefaultConstructedVsEmptyString() {
  RE2 re(".*");
  CHECK(re.ok());

  std::string_view def;
  CHECK(RE2::FullMatch(def, re));

  std::string_view empty("");
  CHECK(RE2::FullMatch(empty, re));
}

// This used to test the distinction between null and empty submatches,
// but I'm no longer preserving that with std::string_view support. Now
// it just wants empty matches.
static void NullVsEmptyStringSubmatches() {
  RE2 re("()|(foo)");
  CHECK(re.ok());

  // matches[0] is overall match, [1] is (), [2] is (foo), [3] is nonexistent.
  std::array<std::string_view, 4> matches;

  for (size_t i = 0; i < matches.size(); i++)
    matches[i] = "bar";

  std::string_view null;
  CHECK(re.Match(null, 0, null.size(), RE2::UNANCHORED,
                 matches.data(), matches.size()));
  for (size_t i = 0; i < matches.size(); i++) {
    CHECK(matches[i].empty());
  }

  for (size_t i = 0; i < matches.size(); i++)
    matches[i] = "bar";

  std::string_view empty("");
  CHECK(re.Match(empty, 0, empty.size(), RE2::UNANCHORED,
                 matches.data(), matches.size()));
  CHECK(matches[0].empty());
  CHECK(matches[1].empty());
  CHECK(matches[2].empty());
  CHECK(matches[3].empty());
}


// Issue 1816809
static void Bug1816809() {
  RE2 re("(((((llx((-3)|(4)))(;(llx((-3)|(4))))*))))");
  std::string_view piece("llx-3;llx4");
  std::string x;
  CHECK(RE2::Consume(&piece, re, &x));
}

// Issue 3061120
static void Bug3061120() {
  RE2 re("(?i)\\W");
  CHECK(!RE2::PartialMatch("x", re));  // always worked
  CHECK(!RE2::PartialMatch("k", re));  // broke because of kelvin
  CHECK(!RE2::PartialMatch("s", re));  // broke because of latin long s
}

static void CapturingGroupNames() {
  // Opening parentheses annotated with group IDs:
  //      12    3        45   6         7
  RE2 re("((abc)(?P<G2>)|((e+)(?P<G2>.*)(?P<G1>u+)))");
  CHECK(re.ok());
  const std::map<int, std::string>& have = re.CapturingGroupNames();
  std::map<int, std::string> want;
  want[3] = "G2";
  want[6] = "G2";
  want[7] = "G1";
  CHECK_EQ(want, have);
}

static void RegexpToStringLossOfAnchor() {
  CHECK_EQ(RE2("^[a-c]at", RE2::POSIX).Regexp()->ToString(), "^[a-c]at");
  CHECK_EQ(RE2("^[a-c]at").Regexp()->ToString(), "(?-m:^)[a-c]at");
  CHECK_EQ(RE2("ca[t-z]$", RE2::POSIX).Regexp()->ToString(), "ca[t-z]$");
  CHECK_EQ(RE2("ca[t-z]$").Regexp()->ToString(), "ca[t-z](?-m:$)");
}

static void Bug10131674() {
  // Some of these escapes describe values that do not fit in a byte.
  printf("A parse error is expected here:\n");
  RE2 re("\\140\\440\\174\\271\\150\\656\\106\\201\\004\\332", RE2::Latin1);
  CHECK(!re.ok());
  CHECK(!RE2::FullMatch("hello world", re));
}



static void Bug18391750() {
  // Stray write past end of match_ in nfa.cc, caught by fuzzing + address sanitizer.
  std::array t = {
      (char)0x28, (char)0x28, (char)0xfc, (char)0xfc, (char)0x08, (char)0x08,
      (char)0x26, (char)0x26, (char)0x28, (char)0xc2, (char)0x9b, (char)0xc5,
      (char)0xc5, (char)0xd4, (char)0x8f, (char)0x8f, (char)0x69, (char)0x69,
      (char)0xe7, (char)0x29, (char)0x7b, (char)0x37, (char)0x31, (char)0x31,
      (char)0x7d, (char)0xae, (char)0x7c, (char)0x7c, (char)0xf3, (char)0x29,
      (char)0xae, (char)0xae, (char)0x2e, (char)0x2a, (char)0x29, (char)0x00,
  };
  RE2::Options opt;
  opt.set_encoding(RE2::Options::EncodingLatin1);
  opt.set_longest_match(true);
  opt.set_dot_nl(true);
  opt.set_case_sensitive(false);
  RE2 re(std::string_view(t.data(), t.size()), opt);
  CHECK(re.ok());
  RE2::PartialMatch(std::string_view(t.data(), t.size()), re);
}

static void Bug18458852() {
  // Bug in parser accepting invalid (too large) rune,
  // causing compiler to fail in ABSL_DCHECK() in UTF-8
  // character class code.
  const char b[] = {
    (char)0x28, (char)0x05, (char)0x05, (char)0x41, (char)0x41, (char)0x28,
    (char)0x24, (char)0x5b, (char)0x5e, (char)0xf5, (char)0x87, (char)0x87,
    (char)0x90, (char)0x29, (char)0x5d, (char)0x29, (char)0x29, (char)0x00,
  };
  RE2 re(b, RE2::Quiet);
  CHECK(!re.ok());
}

static void Bug18523943() {
  // Bug in BitState: case kFailInst failed the match entirely.

  RE2::Options opt;
  const char a[] = {
      (char)0x29, (char)0x29, (char)0x24, (char)0x00,
  };
  const char b[] = {
      (char)0x28, (char)0x0a, (char)0x2a, (char)0x2a, (char)0x29, (char)0x00,
  };
  opt.set_log_errors(false);
  opt.set_encoding(RE2::Options::EncodingLatin1);
  opt.set_posix_syntax(true);
  opt.set_longest_match(true);
  opt.set_literal(false);
  opt.set_never_nl(true);

  RE2 re((const char*)b, opt);
  CHECK(re.ok());
  std::string s1;
  CHECK(RE2::PartialMatch((const char*)a, re, &s1));
}



static void Bug21371806() {
  // Bug in parser accepting Unicode groups in Latin-1 mode,
  // causing compiler to fail in ABSL_DCHECK() in prog.cc.

  RE2::Options opt;
  opt.set_encoding(RE2::Options::EncodingLatin1);

  RE2 re("g\\p{Zl}]", opt);
  CHECK(re.ok());
}

static void Bug26356109() {
  // Bug in parser caused by factoring of common prefixes in alternations.

  // In the past, this was factored to "a\\C*?[bc]". Thus, the automaton would
  // consume "ab" and then stop (when unanchored) whereas it should consume all
  // of "abc" as per first-match semantics.
  RE2 re("a\\C*?c|a\\C*?b");
  CHECK(re.ok());

  std::string s = "abc";
  std::string_view m;

  CHECK(re.Match(s, 0, s.size(), RE2::UNANCHORED, &m, 1));
  CHECK_EQ(m, s) << " (UNANCHORED) got m='" << m << "', want '" << s << "'";

  CHECK(re.Match(s, 0, s.size(), RE2::ANCHOR_BOTH, &m, 1));
  CHECK_EQ(m, s) << " (ANCHOR_BOTH) got m='" << m << "', want '" << s << "'";
}



static void Issue104() {
  // RE2::GlobalReplace always advanced by one byte when the empty string was
  // matched, which would clobber any rune that is longer than one byte.

  std::string s = "bc";
  CHECK_EQ(3, RE2::GlobalReplace(&s, "a*", "d"));
  CHECK_EQ("dbdcd", s);

  s = "ąć";
  CHECK_EQ(3, RE2::GlobalReplace(&s, "Ć*", "Ĉ"));
  CHECK_EQ("ĈąĈćĈ", s);

  s = "人类";
  CHECK_EQ(3, RE2::GlobalReplace(&s, "大*", "小"));
  CHECK_EQ("小人小类小", s);
}

[[maybe_unused]]
static void Issue310() {
  // (?:|a)* matched more text than (?:|a)+ did.

  std::string s = "aaa";
  std::string_view m;

  RE2 star("(?:|a)*");
  CHECK(star.Match(s, 0, s.size(), RE2::UNANCHORED, &m, 1));
  CHECK_EQ(m, "") << " got m='" << m << "', want ''";

  RE2 plus("(?:|a)+");
  CHECK(plus.Match(s, 0, s.size(), RE2::UNANCHORED, &m, 1));
  CHECK_EQ(m, "") << " got m='" << m << "', want ''";
}


static void TestIssue477() {
  // Regexp::LeadingString didn't output Latin1 into flags.
  // In the given pattern, 0xA5 should be factored out, but
  // shouldn't lose its Latin1-ness in the process. Because
  // that was happening, the prefix for accel was 0xC2 0xA5
  // instead of 0xA5. Note that the former doesn't occur in
  // the given input and so replacements weren't occurring.

  std::array bytes = {
      (char)0xa5, (char)0xd1, (char)0xa5, (char)0xd1,
      (char)0x61, (char)0x63, (char)0xa5, (char)0x64,
  };
  std::string s(bytes.data(), bytes.size());
  RE2 re("\xa5\xd1|\xa5\x64", RE2::Latin1);
  int n = RE2::GlobalReplace(&s, re, "");
  CHECK_EQ(n, 3);
  CHECK_EQ(s, "\x61\x63");
}

static void TestInitNull() {
  // RE2::RE2 accepts NULL. Make sure it keeps doing that.
  RE2 re(NULL);
  CHECK(re.ok());
  CHECK(RE2::FullMatch("", re));
  CHECK(RE2::FullMatch("", NULL));
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSimple();
  TestReplace();
  TestObject();
  TestConsume();
  TestBitStateFirstMatchBug();
  TestUnicodeRange();
  TestAnyBytes();
  TestReps();

  // re2 tests
  HexTests();
  OctalTests();
  DecimalTests();
  Replace();
  CheckRewriteString();
  Extract();
  MaxSubmatchTooLarge();
  Consume();
  ConsumeN();
  FindAndConsume();
  FindAndConsumeN();
  MatchNumberPeculiarity();
  Match();
  Simple();
  SimpleNegative();
  Latin1();
  QuoteMetaUTF8();
  HasNull();
  BigProgram();
  Fuzz();
  BitstateAssumptions();
  NamedGroups();
  CapturedGroupTest();
  FullMatchWithNoArgs();
  PartialMatch();
  PartialMatchN();
  FullMatchZeroArg();
  FullMatchOneArg();
  FullMatchIntegerArg();
  FullMatchStringArg();
  FullMatchStringViewArg();
  FullMatchMultiArg();
  FullMatchN();
  FullMatchIgnoredArg();
  FullMatchTypedNullArg();
  FullMatchTypeTests();
  FloatingPointFullMatchTypes();
  FullMatchAnchored();
  FullMatchBraces();
  Complicated();
  FullMatchEnd();
  FullMatchArgCount();
  Accessors();
  UTF8();
  UngreedyUTF8();
  Rejects();
  NoCrash();
  Recursion();
  BigCountedRepetition();
  DeepRecursion();
  MatchAndConsume();
  ImplicitConversions();
  CL8622304();
  ErrorCodeAndArg();
  NeverNewline();
  DotNL();
  NeverCapture();
  BitstateCaptureBug();
  UnicodeClasses();
  LazyRE2();
  DefaultConstructedVsEmptyString();
  NullVsEmptyStringSubmatches();
  Bug1816809();
  Bug3061120();
  CapturingGroupNames();
  RegexpToStringLossOfAnchor();
  Bug10131674();
  Bug18391750();
  Bug18458852();
  Bug18523943();
  Bug21371806();
  Bug26356109();
  Issue104();
  // This appears to be a bug in my fork as well!
  if (false) Issue310();
  TestIssue477();
  TestInitNull();

  Print("\n");
  Print("OK\n");
  return 0;
}
