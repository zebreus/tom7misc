#include "markdown.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

#define CHECK_SEQ(a, b) do { \
  auto aa = (a); \
  auto bb = (b); \
  CHECK(aa == bb) << "Expected equal strings. String 1:\n" << #a        \
                  << "\nString 2:\n" << #b                              \
                  << "\nValues 1:\n" << aa                              \
                  << "\nValue 2:\n" << bb << "\n";                      \
  } while (false)

static void TestStructural() {
  std::string doc_text =
    "# Header\n"
    "\n"
    "Paragraph one\n"
    "continues here.\n"
    "\n"
    "```cc\n"
    "#include <cstdio>\n"
    "int x = 7;\n"
    "```\n"
    "###  Subheader\n"
    // Note bullets with incorrect indentation.
    // We want E as a child of A and sibling of B.
    "- Root A\n"
    "  - Child B\n"
    "    - C\n"
    "    - D\n"
    " - Sibling E\n"
    "- Root A2\n";

  Markdown::Document doc = Markdown::Parse(doc_text);
  if (doc.size() != 6) {
    for (const Markdown::Section &sec : doc) {
      Print("----\n");
      Print("{}\n", Markdown::ToMarkdown({sec}));
    }
  }

  CHECK(doc.size() == 6) << doc.size();

  const Markdown::Heading *h1 =
    std::get_if<Markdown::Heading>(&doc[0]);
  CHECK(h1 != nullptr);
  CHECK(h1->level == 0);
  CHECK_SEQ(h1->text, "Header");

  CHECK(std::get_if<Markdown::Paragraph>(&doc[1]) != nullptr);

  Markdown::Code *c = std::get_if<Markdown::Code>(&doc[2]);
  CHECK(c != nullptr);
  CHECK_SEQ(c->type, "cc");
  CHECK_SEQ(c->body,
            "#include <cstdio>\n"
            "int x = 7;");

  Markdown::Heading *h3 = std::get_if<Markdown::Heading>(&doc[3]);
  CHECK(h3 != nullptr);
  CHECK(h3->level == 2);
  CHECK_SEQ(h3->text, "Subheader");

  {
    Markdown::Bullet *b = std::get_if<Markdown::Bullet>(&doc[4]);
    CHECK(b != nullptr);
    CHECK(b->children.size() == 2);
    CHECK_SEQ(Markdown::ToMarkdown(b->text), "Root A");
    Markdown::Bullet *c0 = &b->children[0];
    Markdown::Bullet *c1 = &b->children[1];
    CHECK_SEQ(Markdown::ToMarkdown(c0->text), "Child B");
    CHECK_SEQ(Markdown::ToMarkdown(c1->text), "Sibling E");
    CHECK(c0->children.size() == 2);
    CHECK(c1->children.empty());
  }

  {
    Markdown::Bullet *b = std::get_if<Markdown::Bullet>(&doc[5]);
    CHECK(b != nullptr);
    CHECK(b->children.size() == 0);
    CHECK_SEQ(Markdown::ToMarkdown(b->text), "Root A2");
  }
}

static void TestText() {
  auto doc = Markdown::Parse("Some **bold text** and `inline code` "
                              "and [a link](http://url.com).");
  CHECK(doc.size() == 1);
  const Markdown::Paragraph *p1 = std::get_if<Markdown::Paragraph>(&doc[0]);
  CHECK(p1 != nullptr);

  CHECK(p1->text.size() == 7);

  #define IS(t, arg) [&]{                        \
      const t *x = std::get_if<t>(&arg);         \
      CHECK(x != nullptr) << #t << ", " << #arg; \
      return *x;                                 \
    }()

  CHECK(IS(Markdown::Plain, p1->text[0]).text == "Some ");
  CHECK(IS(Markdown::Bold, p1->text[1]).text == "bold text");
  CHECK(IS(Markdown::Plain, p1->text[2]).text == " and ");
  CHECK(IS(Markdown::InlineCode, p1->text[3]).text == "inline code");
  CHECK(IS(Markdown::Plain, p1->text[4]).text == " and ");
  const Markdown::URL *url = std::get_if<Markdown::URL>(&p1->text[5]);
  CHECK(url != nullptr);
  CHECK_SEQ(url->text, "a link");
  CHECK_SEQ(url->url, "http://url.com");

  CHECK(IS(Markdown::Plain, p1->text[6]).text == ".");
}

static void TestUnclosed() {
  for (const std::string_view unclosed : {
      "No **bold",
      "No **",
      "No `code",
      "No `",
      "No [link](found",
      "No [link](",
      // Here the bracket is actually ignored, not unclosed.
      // But it still should result in plain text.
      "No [link",
      "No [link",
    }) {
    Markdown::Document doc = Markdown::Parse(unclosed);
    Markdown::Paragraph *p = std::get_if<Markdown::Paragraph>(&doc[0]);
    CHECK(p != nullptr);
    CHECK(p->text.size() == 1);
    CHECK_SEQ(IS(Markdown::Plain, p->text[0]).text, unclosed);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestStructural();
  TestText();
  TestUnclosed();

  Print("OK\n");
  return 0;
}
