
#include "nfa.h"
#include <string>

using namespace std;

// XXX test more than compilation!

using ByteNFA = NFA<256>;
using ByteRegEx = RegEx<256>;

static bool Matches(const ByteNFA &nfa, const string &s) {
  nfa.CheckValidity();
  NFAMatcher<256> matcher(nfa);
  for (int i = 0; i < (int)s.size(); i++) {
    unsigned char c = s[i];
    matcher.Advance(c);
  }
  return matcher.IsMatching();
}

static void TestSimpleEnfa() {
  NFA<257> enfa;
  enfa.start_states = {0};
  NFA<257>::Node node0;
  NFA<257>::Node node1;
  node1.accepting = true;
  node0.next_idx = {{256, {1}}};
  node0.accepting = false;
  enfa.nodes.push_back(node0);
  enfa.nodes.push_back(node1);
  enfa.CheckValidity();
  ByteNFA nfa = RemoveEpsilon<256>(enfa);
  nfa.CheckValidity();

  CHECK(nfa.nodes[0].accepting);
  CHECK(nfa.nodes[1].accepting);
  // Should match empty string, then.
  CHECK(Matches(nfa, ""));
  CHECK(!Matches(nfa, "z"));
}

static void TestRegEx() {
  auto ConvertLiteral = [](const std::string &s) {
      // printf("Convert %s\n", s.c_str());
      auto enfa = ByteRegEx::LiteralString(s);
      enfa.CheckValidity();
      auto nfa = RemoveEpsilon<256>(enfa);
      nfa.CheckValidity();
      return nfa;
    };
  CHECK(Matches(ConvertLiteral("hello"), "hello"));
  CHECK(!Matches(ConvertLiteral("hello"), "hell"));
  CHECK(!Matches(ConvertLiteral("hello"), "ello"));
  CHECK(!Matches(ConvertLiteral("hello"), ""));
  CHECK(!Matches(ConvertLiteral(""), "h"));
  CHECK(Matches(ConvertLiteral(""), ""));

  CHECK(Matches(ConvertLiteral("\xFF"), "\xFF"));

  auto hello = ByteRegEx::LiteralString("hello");
  auto world = ByteRegEx::LiteralString("world");
  auto empty = ByteRegEx::LiteralString("");

  // accepts only "helloworld"
# define CHECKHW(enfa) do {               \
    ByteNFA nfa = RemoveEpsilon<256>(enfa);       \
    CHECK(Matches(nfa, "helloworld"));            \
    CHECK(!Matches(nfa, "hello"));                \
    CHECK(!Matches(nfa, "world"));                \
    CHECK(!Matches(nfa, ""));                     \
    CHECK(!Matches(nfa, "helloworldhello"));      \
    CHECK(!Matches(nfa, "hellohelloworld"));      \
    CHECK(!Matches(nfa, "helloworldworld"));      \
    CHECK(!Matches(nfa, "helloworl"));            \
  } while (0)

  {
    auto helloworld = ByteRegEx::Concat(hello, world);
    CHECKHW(helloworld);
  }

  {
    CHECKHW(ByteRegEx::Concat(empty, ByteRegEx::Concat(hello, world)));
    CHECKHW(ByteRegEx::Concat(hello, ByteRegEx::Concat(empty, world)));
    CHECKHW(ByteRegEx::Concat(hello, ByteRegEx::Concat(world, empty)));
    CHECKHW(ByteRegEx::Concat(ByteRegEx::Concat(hello, world), empty));
    CHECKHW(ByteRegEx::Concat(ByteRegEx::Concat(hello, empty), world));
    CHECKHW(ByteRegEx::Concat(ByteRegEx::Concat(empty, hello), world));
  }


  // accepts "hello" or "world"
# define CHECKHOW(enfa) do {                      \
    ByteNFA nfa = RemoveEpsilon<256>(enfa);       \
    CHECK(!Matches(nfa, "helloworld"));           \
    CHECK(Matches(nfa, "hello"));                 \
    CHECK(Matches(nfa, "world"));                 \
    CHECK(!Matches(nfa, ""));                     \
    CHECK(!Matches(nfa, "helloworldhello"));      \
    CHECK(!Matches(nfa, "hellohelloworld"));      \
    CHECK(!Matches(nfa, "helloworldworld"));      \
    CHECK(!Matches(nfa, "helloworl"));            \
  } while (0)

  {
    auto hello_or_world = ByteRegEx::Or(hello, world);
    CHECKHOW(hello_or_world);
    auto world_or_hello = ByteRegEx::Or(world, hello);
    CHECKHOW(world_or_hello);
  }

  {
    CHECKHOW(ByteRegEx::Concat(empty, ByteRegEx::Or(hello, world)));
    CHECKHOW(ByteRegEx::Or(hello, ByteRegEx::Concat(empty, world)));
    CHECKHOW(ByteRegEx::Or(hello, ByteRegEx::Concat(world, empty)));
    CHECKHOW(ByteRegEx::Concat(ByteRegEx::Or(hello, world), empty));
    CHECKHOW(ByteRegEx::Or(ByteRegEx::Concat(hello, empty), world));
    CHECKHOW(ByteRegEx::Or(ByteRegEx::Concat(empty, hello), world));
  }


  // accepts zero or more "hello" or "world"
# define CHECKSHOW(enfa) do {                      \
    ByteNFA nfa = RemoveEpsilon<256>(enfa);       \
    CHECK(Matches(nfa, "helloworld"));            \
    CHECK(Matches(nfa, "hello"));                 \
    CHECK(Matches(nfa, "world"));                 \
    CHECK(Matches(nfa, "worldworld"));            \
    CHECK(Matches(nfa, "hellohello"));            \
    CHECK(Matches(nfa, ""));                      \
    CHECK(Matches(nfa, "helloworldhello"));       \
    CHECK(Matches(nfa, "hellohelloworld"));       \
    CHECK(Matches(nfa, "helloworldworld"));       \
    CHECK(!Matches(nfa, "helloworl"));            \
    CHECK(!Matches(nfa, "helloworldhell"));       \
    CHECK(!Matches(nfa, "worldworldh"));          \
    CHECK(!Matches(nfa, "hellorld"));             \
  } while (0)


  {
    auto s_hello_or_world = ByteRegEx::Star(ByteRegEx::Or(hello, world));
    CHECKSHOW(s_hello_or_world);
    auto s_world_or_hello = ByteRegEx::Star(ByteRegEx::Or(world, hello));
    CHECKSHOW(s_world_or_hello);
  }

  {
    CHECKSHOW(ByteRegEx::Star(
                  ByteRegEx::Concat(empty, ByteRegEx::Or(hello, world))));
    CHECKSHOW(ByteRegEx::Star(
                  ByteRegEx::Or(hello, ByteRegEx::Concat(empty, world))));
    CHECKSHOW(ByteRegEx::Star(
                  ByteRegEx::Or(hello, ByteRegEx::Concat(world, empty))));
    CHECKSHOW(ByteRegEx::Star(
                  ByteRegEx::Concat(ByteRegEx::Or(hello, world), empty)));
    CHECKSHOW(ByteRegEx::Star(
                  ByteRegEx::Or(ByteRegEx::Concat(hello, empty), world)));
    CHECKSHOW(ByteRegEx::Star(
                  ByteRegEx::Or(ByteRegEx::Concat(empty, hello), world)));
  }

  // accepts one or more "hello" or "world"
# define CHECKPHOW(enfa) do {                      \
    ByteNFA nfa = RemoveEpsilon<256>(enfa);       \
    CHECK(Matches(nfa, "helloworld"));            \
    CHECK(Matches(nfa, "hello"));                 \
    CHECK(Matches(nfa, "world"));                 \
    CHECK(Matches(nfa, "worldworld"));            \
    CHECK(Matches(nfa, "hellohello"));            \
    CHECK(!Matches(nfa, ""));                     \
    CHECK(Matches(nfa, "helloworldhello"));       \
    CHECK(Matches(nfa, "hellohelloworld"));       \
    CHECK(Matches(nfa, "helloworldworld"));       \
    CHECK(!Matches(nfa, "helloworl"));            \
    CHECK(!Matches(nfa, "helloworldhell"));       \
    CHECK(!Matches(nfa, "worldworldh"));          \
    CHECK(!Matches(nfa, "hellorld"));             \
  } while (0)


  {
    auto p_hello_or_world = ByteRegEx::Plus(ByteRegEx::Or(hello, world));
    CHECKPHOW(p_hello_or_world);
    auto p_world_or_hello = ByteRegEx::Plus(ByteRegEx::Or(world, hello));
    CHECKPHOW(p_world_or_hello);
  }

  {
    CHECKPHOW(ByteRegEx::Plus(
                  ByteRegEx::Concat(empty, ByteRegEx::Or(hello, world))));
    CHECKPHOW(ByteRegEx::Plus(
                  ByteRegEx::Or(hello, ByteRegEx::Concat(empty, world))));
    CHECKPHOW(ByteRegEx::Plus(
                  ByteRegEx::Or(hello, ByteRegEx::Concat(world, empty))));
    CHECKPHOW(ByteRegEx::Plus(
                  ByteRegEx::Concat(ByteRegEx::Or(hello, world), empty)));
    CHECKPHOW(ByteRegEx::Plus(
                  ByteRegEx::Or(ByteRegEx::Concat(hello, empty), world)));
    CHECKPHOW(ByteRegEx::Plus(
                  ByteRegEx::Or(ByteRegEx::Concat(empty, hello), world)));
  }

  CHECK(!Matches(RemoveEpsilon<256>(ByteRegEx::Void()), ""));
  CHECK(Matches(RemoveEpsilon<256>(ByteRegEx::Empty()), ""));
}

static void TestRegExParse() {
# define CHECK_MATCH(re, s) do {             \
    auto er = Parse(re);                     \
    er.CheckValidity();                      \
    auto r = RemoveEpsilon<256>(er);         \
    r.CheckValidity();                       \
    string ss = s;                           \
    CHECK(Matches(r, ss)) << r.DebugString() \
                          << "\nregex \"" << re << "\" should match \""  \
                          << ss << "\"." << "\n(string size: "        \
                          << ss.size() << ")";                        \
  } while (0)

# define CHECK_NO_MATCH(re, s) do {          \
    auto er = Parse(re);                     \
    er.CheckValidity();                      \
    /* er.DebugPrint(); */                   \
    auto r = RemoveEpsilon<256>(er);         \
    r.CheckValidity();                       \
    /* r.DebugPrint(); */                    \
    string ss = s;                           \
    CHECK(!Matches(r, ss)) << r.DebugString() \
                           << "\nregex \"" << re << "\" should not match \"" \
                           << ss << "\"." << "\n(string size: " \
                           << ss.size() << ")"; \
  } while (0)

  CHECK_MATCH("abc", "abc");
  CHECK_NO_MATCH("abc", "");
  CHECK_MATCH("", "");
  CHECK_NO_MATCH("", "abc");

  CHECK_MATCH("abc|def", "abc");
  CHECK_MATCH("abc|def", "def");
  CHECK_NO_MATCH("abc|def", "abd");
  CHECK_NO_MATCH("abc|def", "");

  CHECK_MATCH("|a", "");
  CHECK_MATCH("|a", "a");
  CHECK_MATCH("a|", "");
  CHECK_MATCH("a|", "a");

  CHECK_MATCH("a*", "");
  CHECK_MATCH("a*", "aaa");
  CHECK_NO_MATCH("a*", "aaab");
  CHECK_MATCH("ba*", "b");
  CHECK_MATCH("ba*", "baaaaa");
  CHECK_NO_MATCH("ba*", "bbaaaaa");
  CHECK_NO_MATCH("ba*", "");

  CHECK_MATCH("(a)(b)", "ab");
  CHECK_NO_MATCH("(a)(b)", "a");
  CHECK_NO_MATCH("(a)(b)", "b");
  CHECK_NO_MATCH("(a)(b)", "a)");
  CHECK_NO_MATCH("(a)(b)", "a(");
  CHECK_NO_MATCH("(a)(b)", ")b");
  CHECK_NO_MATCH("(a)(b)", "(b");

  CHECK_MATCH("(abc)*", "");
  CHECK_MATCH("(abc)*", "abcabc");
  CHECK_MATCH("(abc)*", "abc");
  CHECK_NO_MATCH("(abc)*", "cab");
  CHECK_NO_MATCH("(abc)*", "abca");

  CHECK_MATCH("(a|b|c)*", "acab");
  CHECK_MATCH("(a|b|c)*", "bacca");
  CHECK_MATCH("(a|b|c)*", "a");
  CHECK_MATCH("(a|b|c)*", "b");
  CHECK_MATCH("(a|b|c)*", "c");
  CHECK_MATCH("(a|b|c)*", "");
  CHECK_NO_MATCH("(a|b|c)*", "d");

  CHECK_MATCH("(abc)+", "abcabc");
  CHECK_MATCH("(abc)+", "abc");
  CHECK_NO_MATCH("(abc)+", "");
  CHECK_NO_MATCH("(abc)+", "cab");
  CHECK_NO_MATCH("(abc)+", "abca");

  CHECK_MATCH("(abc)?", "abc");
  CHECK_MATCH("(abc)?", "");
  CHECK_NO_MATCH("(abc)?", "abcabc");
  CHECK_NO_MATCH("(abc)?", "ab");

  CHECK_MATCH("\\a", "a");

  CHECK_MATCH("\\)", ")");
  CHECK_NO_MATCH("\\)", "");
  CHECK_MATCH("\\)a", ")a");
  CHECK_NO_MATCH("\\)a", "a");

  CHECK_MATCH("\\(", "(");
  CHECK_NO_MATCH("\\(", "");
  CHECK_MATCH("\\(a", "(a");
  CHECK_NO_MATCH("\\(a", "a");

  CHECK_MATCH("\\\\", "\\");
  CHECK_NO_MATCH("\\\\", "");
  CHECK_MATCH("\\\\a", "\\a");
  CHECK_NO_MATCH("\\\\a", "a");

  CHECK_MATCH("(\\))", ")");
  CHECK_NO_MATCH("(\\))", "");
  CHECK_MATCH("(\\)a)", ")a");
  CHECK_NO_MATCH("(\\)a)", "a");

  CHECK_MATCH("(\\()", "(");
  CHECK_NO_MATCH("(\\()", "");
  CHECK_MATCH("(\\(a)", "(a");
  CHECK_NO_MATCH("(\\(a)", "a");

  CHECK_MATCH(".", "a");
  CHECK_NO_MATCH(".", "");
  CHECK_NO_MATCH(".", "ab");
  CHECK_MATCH(".", " ");
  CHECK_MATCH(".", "\n");
  CHECK_MATCH(".", "\xFF");

  CHECK_MATCH(".*", "");
  CHECK_MATCH(".*", "anything :)");

  CHECK_MATCH("a.*z", "analyz");
  CHECK_NO_MATCH("a.*z", "analyze");
  CHECK_NO_MATCH("a.*z", "banalyz");

  CHECK_MATCH("a.?b", "ab");
  CHECK_MATCH("a.?b", "acb");
  CHECK_MATCH("a.?b", "abb");
  CHECK_MATCH("a.?b", "aab");
  CHECK_NO_MATCH("a.?b", "accb");
  CHECK_NO_MATCH("a.?b", "abc");

  CHECK_MATCH("[a]", "a");
  CHECK_NO_MATCH("[a]", "b");
  CHECK_MATCH("[a-z]", "b");
  CHECK_MATCH("[a-z]", "a");
  CHECK_MATCH("[a-z]", "z");
  CHECK_NO_MATCH("[a-z]", "Z");
  CHECK_NO_MATCH("[a-z]", "ab");
  CHECK_NO_MATCH("[a-z]", "");

  CHECK_MATCH("[^a]", "b");
  CHECK_NO_MATCH("[^a]", "");
  CHECK_NO_MATCH("[^a]", "a");

  CHECK_MATCH("[^a-z]", "0");
  CHECK_NO_MATCH("[^a-z]", "");
  CHECK_NO_MATCH("[^a-z]", "a");
  CHECK_MATCH("[^a-z]", "\n");
  CHECK_MATCH("[^a-z]", "^");

  CHECK_MATCH("[-a]", "-");
  CHECK_MATCH("[-a]", "a");
  CHECK_NO_MATCH("[-a]", "b");

  CHECK_NO_MATCH("[^-a]", "-");
  CHECK_NO_MATCH("[^-a]", "a");
  CHECK_MATCH("[^-a]", "b");
  CHECK_MATCH("[^-a]", "^");

  CHECK_MATCH("[A-Za-z_]+", "An_Identifier");
  CHECK_NO_MATCH("[A-Za-z_]+", "not an identifier");

  CHECK_MATCH("[a-z][A-Z]", "yO");
  CHECK_NO_MATCH("[a-z][A-Z]", "No");

  CHECK_MATCH("[\\]]*", "]]]");
  CHECK_MATCH("[\\]a]*", "a]]aa]a");
  CHECK_NO_MATCH("[\\]a]*", "\\a]]aa]a");
  CHECK_MATCH("[\\[]*", "[[");
  CHECK_MATCH("[\\[a]*", "a[aa[a");
  CHECK_NO_MATCH("[\\[a]*", "\\a[aa[a");
  CHECK_MATCH("[a\\-z]*", "a-z");
  CHECK_NO_MATCH("[a\\-z]*", "b");
  CHECK_MATCH("[\\^z]*", "^z");
  CHECK_NO_MATCH("[\\^z]*", "\\z");
  CHECK_MATCH("[\\\\z]*", "z\\");

  CHECK_MATCH("[)(]*", "())(()()");
  CHECK_MATCH("([()])*", "())(()()");
  CHECK_MATCH("([)(])*", "())(()()");

  CHECK_MATCH("(\\[\\])*", "[][][]");
  CHECK_NO_MATCH("(\\[\\])*", "[][[]]");
  CHECK_MATCH("(a|\\||b)*", "ab||");
  CHECK_MATCH("(a|\\||b)+", "|");

  {
    string email = "[a-z][a-z0-9_]*@[a-z][-a-z0-9]*\\.(com|net|org)";
    CHECK_MATCH(email, "the_beatles@music.com");
    CHECK_MATCH(email, "bond007@spy.net");
    CHECK_MATCH(email, "bond007@spy.net");
    CHECK_MATCH(email, "a@z.org");
    CHECK_NO_MATCH(email, "regular @ expressions.org");
    CHECK_NO_MATCH(email, "regularexpression");
  }
}

int main(int argc, char **argv) {
  TestSimpleEnfa();
  TestRegEx();
  TestRegExParse();

  printf("OK\n");
  return 0;
}
