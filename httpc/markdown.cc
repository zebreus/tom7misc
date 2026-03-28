#include "markdown.h"

#include <cctype>
#include <functional>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"

static Markdown::Text ParseText(std::string_view body) {
  // Never empty during the loop.
  std::vector<Markdown::TextPart> ret;

  // Pending plaintext.
  std::string plain;

  auto Emit = [&ret, &plain]() {
      if (!plain.empty()) {
        if (ret.empty()) {
          ret.emplace_back(Markdown::Plain{.text = std::move(plain)});
        } else {
          Markdown::Plain *p = std::get_if<Markdown::Plain>(&ret.back());
          if (p == nullptr) {
            ret.emplace_back(Markdown::Plain{.text = std::move(plain)});
          } else {
            p->text.append(plain);
          }
        }
        plain.clear();
      }
    };

  while (!body.empty()) {
    char c = body[0];
    body.remove_prefix(1);
    switch (c) {
    case '`': {
      size_t end = body.find('`');
      if (end == std::string_view::npos) {
        // Unterminated.
        plain.push_back(c);
        plain.append(body);
        body.remove_prefix(body.size());
      } else {
        Emit();
        ret.emplace_back(Markdown::InlineCode{
            .text = std::string(body.substr(0, end)),
          });
        // And the ending `.
        body.remove_prefix(end + 1);
      }
      break;
    }

    case '[': {
      size_t mid = body.find("](");
      if (mid == std::string_view::npos) {
        // If we don't find the matching middle thing,
        // then just treat this bracket char as plain text.
        plain.push_back(c);
        break;
      }

      // Otherwise, we'll consider this an unclosed link.
      size_t end = body.find(')', mid + 2);

      if (end == std::string_view::npos) {
        plain.push_back(c);
        plain.append(body);
        body.remove_prefix(body.size());
      } else {
        Emit();
        ret.emplace_back(Markdown::URL{
            .text = std::string(body.substr(0, mid)),
            .url = std::string(body.substr(mid + 2, end - (mid + 2))),
          });
        body.remove_prefix(end + 1);
      }

      break;
    }

    case '*':
      if (body.starts_with("*")) {
        body.remove_prefix(1);
        size_t end = body.find("**");
        if (end == std::string_view::npos) {
          plain.append("**");
          plain.append(body);
          body.remove_prefix(body.size());
        } else {
          Emit();
          ret.emplace_back(Markdown::Bold{
              .text = std::string(body.substr(0, end)),
            });
          body.remove_prefix(end + 2);
        }
      } else {
        // Just an asterisk.
        plain.push_back(c);
      }
      break;

    default:
      plain.push_back(c);
    }

  }

  Emit();
  return ret;
}

// On a line whose leading whitespace was trimmed.
// If the line starts with a bullet marker (-, *, +, or NN.) then
// return the remainder of the line's text.
static std::optional<std::string_view>
GetBulletBody(std::string_view trimmed) {
  if (trimmed.starts_with("- ") ||
      trimmed.starts_with("* ") ||
      trimmed.starts_with("+ ")) {
    return {trimmed.substr(2)};
  } else {
    bool had_digits = false;
    while (!trimmed.empty() && std::isdigit(trimmed[0])) {
      had_digits = true;
      trimmed.remove_prefix(1);
    }

    if (had_digits && trimmed.starts_with(". ")) {
      return {trimmed.substr(2)};
    }
  }

  return std::nullopt;
}

Markdown::Document Markdown::Parse(std::string_view s) {
  std::vector<Section> ret;

  // Pending structural elements. These don't have their bodies
  // parsed yet (since e.g. the bolding syntax can span across lines).

  // This one is multi-line but doesn't need special post-processing.
  using PendingCode = Code;

  struct PendingParagraph {
    std::string body;
  };

  struct PendingBullet {
    // Leading whitespace in the current stack, so that we can
    // match this up when we see another bullet.
    int indentation = 0;
    std::string body;
    std::vector<PendingBullet> children;
  };

  std::function<Bullet(PendingBullet *pb)> ConvertBullets =
    [&ConvertBullets](PendingBullet *pb) {
      Bullet bullet;
      bullet.text = ParseText(pb->body);
      bullet.children.reserve(pb->children.size());
      for (PendingBullet &ch : pb->children) {
        bullet.children.push_back(ConvertBullets(&ch));
      }
      return bullet;
    };

  // Walk down the tree of bullet points (only the rightmost nodes)
  // to see what the parent of a new bullet point with the given
  // indentation should be.
  auto GetParent = [](PendingBullet *tree, int indent) {
      for (;;) {
        if (tree->children.empty()) {
          return tree;
        } else {
          PendingBullet *rt = &tree->children.back();
          if (indent <= rt->indentation) {
            return tree;
          }

          // Otherwise it's deeper, so we step into it.
          tree = &tree->children.back();
        }
      }
    };


  using Pending = std::variant<PendingParagraph, PendingBullet, PendingCode>;

  // The current section that we're building up. The default is a
  // paragraph, where an empty paragraph basically acts as a unit
  // element.
  Pending cur = PendingParagraph{};

  auto EmitCur = [&]() {
      // Don't emit blank paragraphs.
      if (PendingParagraph *p = std::get_if<PendingParagraph>(&cur)) {
        if (p->body.empty()) {
          return;
        }

        ret.emplace_back(Paragraph{.text = ParseText(p->body)});
      } else if (PendingBullet *b = std::get_if<PendingBullet>(&cur)) {
        ret.emplace_back(ConvertBullets(b));
      } else if (Code *c = std::get_if<Code>(&cur)) {
        ret.emplace_back(std::move(*c));
      }

      cur = PendingParagraph{};
    };

  // We go line by line, because headings, bullet points, and
  // paragraphs are all essentially line-structured.
  std::vector<std::string> lines = Util::SplitToLines(s);

  for (std::string_view line : lines) {
    // In a code block, we behave differently.
    if (Code *c = std::get_if<Code>(&cur)) {

      std::string_view line_no_trail = line;
      while (!line_no_trail.empty() &&
             (line_no_trail.back() == ' ' || line_no_trail.back() == '\t'))
        line_no_trail.remove_suffix(1);

      // Print("In code block: [{}]\n", line_no_trail);

      if (line_no_trail == "```") {
        // This ends the code block.
        while (!c->body.empty() && c->body.back() == '\n')
          c->body.pop_back();

        EmitCur();

      } else {
        // Keep trailing whitespace here, though.
        c->body.append(line);
        c->body.push_back('\n');
      }
      continue;
    }

    int leading_whitespace = 0;
    std::string_view trimmed = line;
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) {
      leading_whitespace += (trimmed[0] == '\t') ? 4 : 1;
      trimmed.remove_prefix(1);
    }

    if (trimmed.empty()) {
      EmitCur();
      continue;
    }

    // Heading must begin in column 0.
    if (trimmed[0] == '#' && leading_whitespace == 0) {
      trimmed.remove_prefix(1);

      int depth = 0;
      while (!trimmed.empty() && trimmed[0] == '#') {
        depth++;
        trimmed.remove_prefix(1);
      }

      // Must have whitespace after ####.
      bool had_whitespace = false;
      while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) {
        had_whitespace = true;
        trimmed.remove_prefix(1);
      }

      if (had_whitespace) {
        EmitCur();
        ret.push_back(Heading{.level = depth, .text = std::string(trimmed)});
        continue;
      }

      // Otherwise, fall through (and this will be handled as
      // plain text below).
    }

    if (line.starts_with("```") && leading_whitespace == 0) {
      line.remove_prefix(3);
      Util::RemoveOuterWhitespace(&line);
      EmitCur();
      cur = Code{.type = std::string(line), .body = ""};
      continue;
    }

    if (std::optional<std::string_view> bullet_body =
        GetBulletBody(trimmed)) {

      std::string_view rest = bullet_body.value();
      PendingBullet *root = std::get_if<PendingBullet>(&cur);

      // If we aren't in a bullet list, or this new bullet matches the
      // root depth, start a new top-level list.
      if (root == nullptr || leading_whitespace <= root->indentation) {
        EmitCur();
        cur = PendingBullet{
          .indentation = leading_whitespace,
          .body = std::string(rest)
        };
      } else {
        // Find where this new bullet point belongs in the tree.
        PendingBullet *parent = GetParent(root, leading_whitespace);

        parent->children.push_back(PendingBullet{
          .indentation = leading_whitespace,
          .body = std::string(rest)
        });
      }
      continue;
    }

    // Otherwise, it doesn't start with any particular syntax, so
    // it is continuing the current bullet or paragraph.

    if (PendingBullet *pb = std::get_if<PendingBullet>(&cur)) {
      // This goes on the deepest bullet.
      while (!pb->children.empty()) {
        pb = &pb->children.back();
      }
      if (!pb->body.empty())
        pb->body.push_back(' ');
      pb->body.append(trimmed);

    } else if (PendingParagraph *pp = std::get_if<PendingParagraph>(&cur)) {
      // Newline treated as space.
      if (!pp->body.empty())
        pp->body.push_back(' ');
      pp->body.append(trimmed);

    } else {
      LOG(FATAL) << "Invalid state: Should be pending code (handled above) "
        "or bullet or paragraph.";
    }
  }

  EmitCur();
  return ret;
}

std::string Markdown::ToMarkdown(const Text &text) {
  std::string ret;
  for (const TextPart &part : text) {
    if (const Plain *p = std::get_if<Plain>(&part)) {
      ret.append(p->text);
    } else if (const Bold *b = std::get_if<Bold>(&part)) {
      AppendFormat(&ret, "**{}**", b->text);
    } else if (const URL *u = std::get_if<URL>(&part)) {
      AppendFormat(&ret, "[{}]({})", u->text, u->url);
    } else if (const InlineCode *c = std::get_if<InlineCode>(&part)) {
      AppendFormat(&ret, "`{}`", c->text);
    } else {
      LOG(FATAL) << "Bad variant?";
    }
  }
  return ret;
}


static void WriteBullet(const Markdown::Bullet &b, int indent,
                        std::string *ret) {
  ret->append(indent * 2, ' ');
  AppendFormat(ret, "* {}\n", Markdown::ToMarkdown(b.text));

  for (const Markdown::Bullet &child : b.children) {
    WriteBullet(child, indent + 1, ret);
  }
}


std::string Markdown::ToMarkdown(const Document &doc) {
  std::string ret;

  for (const Section &sec : doc) {
    if (const Paragraph *p = std::get_if<Paragraph>(&sec)) {
      AppendFormat(&ret, "{}\n\n", ToMarkdown(p->text));

    } else if (const Heading *h = std::get_if<Heading>(&sec)) {
      ret.append(h->level + 1, '#');
      AppendFormat(&ret, " {}\n\n", h->text);

    } else if (const Code *c = std::get_if<Code>(&sec)) {
      std::string_view nl = "";
      if (c->body.empty() || c->body.back() != '\n') nl = "\n";
      AppendFormat(&ret, "```{}\n"
                   "{}{}"
                   "```\n\n",
                   c->type,
                   c->body, nl);

    } else if (const Bullet *b = std::get_if<Bullet>(&sec)) {
      WriteBullet(*b, 0, &ret);
      ret.push_back('\n');

    } else {
      LOG(FATAL) << "Bad section variant?";
    }
  }

  // Clean up trailing newlines.
  while (!ret.empty() && ret.back() == '\n') {
    ret.pop_back();
  }
  if (!ret.empty()) {
    ret.push_back('\n');
  }

  return ret;
}
