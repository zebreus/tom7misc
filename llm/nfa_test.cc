
#include "nfa.h"
#include <string>

using namespace std;

// XXX test more than compilation!

using ByteNFA = NFA<256>;
using ByteRegEx = RegEx<256>;

static bool Matches(const ByteNFA &nfa, string s) {
  nfa.CheckValidity();
  NFAMatcher<256> matcher(nfa);
  for (int i = 0; i < (int)s.size(); i++) {
    char c = s[i];
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

  ByteNFA nfa = RemoveEpsilon<256>(enfa);

  CHECK(nfa.nodes[0].accepting);
  CHECK(nfa.nodes[1].accepting);
  // Should match empty string, then.
  CHECK(Matches(nfa, ""));
  CHECK(!Matches(nfa, "z"));
}

static void TestRegEx() {
  auto ConvertLiteral = [](const std::string &s) {
      printf("Convert %s\n", s.c_str());
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
    CHECK(Matches(r, s)) << "regex \"" << re "\" should match \"" \
                         << s << "\".";                           \
  } while (0)

# define CHECK_NO_MATCH(re, s) do {          \
    auto er = Parse(re);                     \
    er.CheckValidity();                      \
    /* er.DebugPrint(); */                   \
    auto r = RemoveEpsilon<256>(er);         \
    r.CheckValidity();                       \
    /* r.DebugPrint(); */                    \
    CHECK(!Matches(r, s)) << "regex \"" << re "\" should not match \"" \
                          << s << "\".";                               \
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
}

int main(int argc, char **argv) {
  TestSimpleEnfa();
  TestRegEx();
  TestRegExParse();

  printf("OK\n");
  return 0;
}
