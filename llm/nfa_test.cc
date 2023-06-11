
#include "nfa.h"
#include <string>

using namespace std;

// XXX test more than compilation!

using ByteNFA = NFA<256>;
using ByteRegEx = RegEx<256>;

static bool Matches(const ByteNFA &nfa, string s) {
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
      return RemoveEpsilon<256>(ByteRegEx::LiteralString(s));
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

}

int main(int argc, char **argv) {
  TestSimpleEnfa();
  TestRegEx();

  printf("OK\n");
  return 0;
}
