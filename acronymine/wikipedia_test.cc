
#include "wikipedia.h"

#include <memory>
#include <string>

#include "base/logging.h"

using namespace std;

static void Parse() {
  std::unique_ptr<Wikipedia> wiki(Wikipedia::Create("fake-wikipedia.xml"));
  auto ao1 = wiki->Next();
  CHECK(ao1.has_value());
  auto ao2 = wiki->Next();
  CHECK(ao2.has_value());
  CHECK(!wiki->Next().has_value());
}

static void TestRemoveTags() {
  std::unique_ptr<Wikipedia> wiki(Wikipedia::Create("fake-wikipedia.xml"));

  {
    const string s =
      wiki->RemoveTags("check it <ref>out</ref> dood");
    CHECK_EQ("check it  dood", s) << s;
  }

  {
    const string s =
      wiki->RemoveTags("This<ref>ignore this</ref> stuff<ref>and this</ref> left");
    CHECK_EQ("This stuff left", s) << s;
  }

  {
    const string s =
      wiki->RemoveTags("A<ref name=\"first\">ignore this</ref> B<ref>and this</ref> C<ref group=\"gg\" >delete</ref>");
    CHECK_EQ("A B C", s) << s;
  }

  {
    const string s =
      wiki->RemoveTags("Z<ref name=a/> X<ref name=\"b\"/> W<ref/>");
    CHECK_EQ("Z X W", s) << s;
  }

  {
    const string s =
      wiki->RemoveTags("This is a <!--\n"
                       "multi-line\n"
                       "comment -->bye");
    CHECK_EQ("This is a bye", s) << s;
  }
}

static void TestReplaceEntities() {
  std::unique_ptr<Wikipedia> wiki(Wikipedia::Create("fake-wikipedia.xml"));

  {
    string s = wiki->ReplaceEntities(
        "There &quot;are&quot; 1&ndash;2 small dogs&ndash;cats");
    CHECK_EQ("There \"are\" 1-2 small dogs-cats", s) << s;
  }
}

static void TestRemoveWikilinks() {
  std::unique_ptr<Wikipedia> wiki(Wikipedia::Create("fake-wikipedia.xml"));

  {
    string s =
      "Consult the {{infobox test|title=TestRemoveWikilinks}} box "
      "{{minipage|{{inside}}}} ok?";
    wiki->RemoveWikilinks(&s);
    CHECK_EQ("Consult the  box  ok?", s) << s;
  }

  {
    string s =
      "A [[Category:Tests]] category and "
      "a [[File:Image.jpg]] and "
      "an [[Image:test.png|This is a [[picture]] of the test.]].";
    wiki->RemoveWikilinks(&s);
    CHECK_EQ("A  category and a  and an .", s) << s;
  }
}


int main(int argc, char **argv) {
  Parse();
  TestReplaceEntities();
  TestRemoveTags();
  TestRemoveWikilinks();
  printf("OK\n");
  return 0;
}
