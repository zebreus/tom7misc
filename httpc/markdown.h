
// XXX intended for cc-lib but still experimental

#ifndef _CC_LIB_MARKDOWN_H
#define _CC_LIB_MARKDOWN_H

#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Markdown is a simple document tree. Unlike HTML, most structures do
// not nest.
struct Markdown {

  // Text is a series of text parts: Plain text, a URL, `code`, **bold**.
  // Nested formatting is flattened for simplicity (e.g. you cannot have
  // bold inside link text, or code within a header, or code within
  // a bullet point).
  struct Plain {
    std::string text;
  };

  struct URL {
    std::string text;
    std::string url;
  };

  // e.g. setting off a `keyword` in text.
  struct InlineCode {
    std::string text;
  };

  struct Bold {
    std::string text;
  };

  using TextPart = std::variant<Plain, URL, InlineCode, Bold>;
  using Text = std::vector<TextPart>;


  // And then a document is a series of sections: A paragraph, bullet,
  // code block, or heading.
  struct Paragraph {
    Text text;
  };

  // This is the only element that can nest.
  struct Bullet {
    Text text;
    std::vector<Bullet> children;
  };

  // ``` section.
  struct Code {
    // The string after ``` which specifies the mode for syntax
    // highlighting or whatever.
    std::string type;

    // Preformatted text with meaningful space. No leading or
    // trailing newline.
    std::string body;
  };

  // Heading labeling the sections that follow.
  struct Heading {
    // Level 0 is the "biggest" heading.
    int level = 0;
    std::string text;
  };

  using Section = std::variant<Paragraph, Bullet, Code, Heading>;
  using Document = std::vector<Section>;

  // Parse markdown. Liberal, accepting any input and trying to
  // make sense of it. Output will only have valid UTF-8.
  static Document Parse(std::string_view s);

  // Recompute markdown syntax for the text.
  static std::string ToMarkdown(const Document &doc);
  static std::string ToMarkdown(const Text &text);
};

#endif
