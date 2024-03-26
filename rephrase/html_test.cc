
#include "html.h"

#include <cstdio>
#include <vector>
#include <string>

#include "ansi.h"
#include "base/logging.h"

static constexpr bool VERBOSE = false;

static std::vector<HTMLNode> Parse(const std::string &s) {
  std::string err;
  std::vector<HTMLNode> nodes = HTML::Parse(s, &err);
  CHECK(err.empty()) << "Failed: " << err;
  if (VERBOSE)
    HTML::DebugPrint(nodes);
  return nodes;
}

static void FailParse(const std::string &s) {
  std::string err;
  std::vector<HTMLNode> nodes = HTML::Parse(s, &err);
  if (err.empty()) {
    printf(AWHITE("Expected failure, but parsed (input)") ":\n");
    printf("%s\n", s.c_str());
    printf(AWHITE("To") ":\n");
    HTML::DebugPrint(nodes);
    LOG(FATAL) << "Unexpected success.";
  }
}

static void TestSimple() {
  {
    std::vector<HTMLNode> nodes = Parse("Yes");
    CHECK(nodes.size() == 1);
    CHECK(!nodes[0].is_tag);
    CHECK(nodes[0].str == "Yes");
    CHECK(nodes[0].attrs.empty());
    CHECK(nodes[0].children.empty());
  }

  {
    std::vector<HTMLNode> nodes = Parse("a <b>bold</b> feast");
    CHECK(nodes.size() == 3);
    CHECK(!nodes[0].is_tag);
    CHECK(nodes[0].str == "a ");
    CHECK(nodes[1].is_tag);
    CHECK(nodes[1].str == "b");
    CHECK(nodes[1].children.size() == 1);
    CHECK(!nodes[2].is_tag);
    CHECK(nodes[2].str == " feast");
  }

  {
    std::vector<HTMLNode> nodes = Parse("a <img src=\"x.png\"> c");
    CHECK(nodes.size() == 3);
    CHECK(nodes[1].is_tag);
    CHECK(nodes[1].str == "img");
    CHECK(nodes[1].attrs.size() == 1);
    CHECK(nodes[1].attrs["src"] == "x.png");
    CHECK(nodes[1].children.empty());
  }


  {
    std::vector<HTMLNode> nodes =
      Parse("The <span class=\"c\"><span class=\"d1\">nested</span> "
            "one</span>");
    CHECK(nodes.size() == 2);
    CHECK(!nodes[0].is_tag);
    CHECK(nodes[0].str == "The ");
    CHECK(nodes[1].is_tag);
    CHECK(nodes[1].str == "span");
    CHECK(nodes[1].attrs["class"] == "c");
    CHECK(nodes[1].children.size() == 2);
    const HTMLNode &c1 = nodes[1].children[0];
    const HTMLNode &c2 = nodes[1].children[1];
    CHECK(c1.is_tag);
    CHECK(c1.str == "span");
    CHECK(!c2.is_tag);
    CHECK(c2.str == " one");
  }

  FailParse("<b>");

  FailParse("Unmatched\n<P> tag");

  FailParse("Didier Rémy, Jérôme Vouillon. \"Objective ML: An "
            "effective object-oriented extension to ML\". In "
            "Theory And Practice of Objects Systems, 4(1). 1998. "
            "pp. 27–50. <img src=\"img0.png\">\n\n<P>"
            "<img src=\"img1.png\">Didier Rémy, Jérôme Vouillon. "
            "<img src=\"img2.png\">\"Objective ML: An effective "
            "object-oriented extension to ML\". "
            "<img src=\"img3.png\"><span class=\"c0\">In Theory "
            "And Practice of Objects Systems</span>, 4(1). <img "
            "src=\"img4.png\">1998. <img src=\"img5.png\">pp. "
            "27–50. <img src=\"img6.png\">");

}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSimple();

  return 0;
}
