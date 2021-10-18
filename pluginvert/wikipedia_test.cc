
#include "wikipedia.h"

#include <memory>
#include <string>

#include "base/logging.h"

using namespace std;

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
      wiki->RemoveTags("A<ref name=\"first\">ignore this</ref> B<ref>and this</ref> C");
    CHECK_EQ("A B C", s) << s;
  }

  // TODO: Isn't there like <ref name=\"first\" /> ?
}


int main(int argc, char **argv) {
  TestRemoveTags();
  printf("OK\n");
  return 0;
}
