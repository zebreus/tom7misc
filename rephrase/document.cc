#include "document.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "achievements.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "bc.h"
#include "bignum/big.h"
#include "boxes-and-glue.h"
#include "color-util.h"
#include "functional-set.h"
#include "hyphenation.h"
#include "image.h"
#include "svg.h"
#include "utf8.h"
#include "util.h"

static constexpr bool VERBOSE = false;

// Could use actual infinity.
static constexpr double INFINITE_PENALTY = 9999999.0;

// This should be tunable.
static constexpr double HYPHEN_PENALTY = 4.0;

// For expand-contract, in places where we don't want to do any
// expansion or contraction (e.g. kerning points).
static constexpr double EPSILON_COEFFICIENT = 0.0000000005;

#define ATAG(s) AFGCOLOR(160, 160, 200, s)
#define AATTRNAME(s) AFGCOLOR(200, 200, 160, s)
#define AATTRVAL(s) AFGCOLOR(160, 200, 160, s)

DocTree TextDoc(std::string s) {
  DocTree d;
  d.text = std::move(s);
  return d;
}

void DocTree::ClearChildren() {
  children.clear();
}

void DocTree::AddChild(DocTree d) {
  children.push_back(std::make_shared<DocTree>(std::move(d)));
  CHECK(children.back().get() != nullptr);
}

void DocTree::RemoveAttr(const std::string &name) {
  attrs.erase(name);
}

void DocTree::SetStringAttr(const std::string &name, const std::string &value) {
  attrs[name] = AttrVal{.v = {value}};
}

void DocTree::SetIntAttr(const std::string &name, const BigInt &value) {
  attrs[name] = AttrVal{.v = {value}};
}

void DocTree::SetDoubleAttr(const std::string &name, double d) {
  attrs[name] = AttrVal{.v = {d}};
}

void DocTree::SetLayoutAttr(const std::string &name, DocTree dt) {
  attrs[name] = AttrVal{.v = std::make_shared<DocTree>(std::move(dt))};
}

const AttrVal *DocTree::GetAttr(const std::string &name) const {
  const auto it = attrs.find(name);
  if (it == attrs.end()) return nullptr;
  return &it->second;
}

const std::string *DocTree::GetStringAttr(const std::string &name) const {
  if (const AttrVal *a = GetAttr(name)) {
    if (const std::string *s = std::get_if<std::string>(&a->v)) {
      return s;
    } else {
      LOG(FATAL) << "Attribute " << name << " was present, but expected "
        "string type.";
    }
  }
  return nullptr;
}

const double *DocTree::GetDoubleAttr(const std::string &name) const {
  if (const AttrVal *a = GetAttr(name)) {
    if (const double *d = std::get_if<double>(&a->v)) {
      return d;
    } else {
      LOG(FATAL) << "Attribute " << name << " was present, but expected "
        "double type.";
    }
  }
  return nullptr;
}

const bool *DocTree::GetBoolAttr(const std::string &name) const {
  if (const AttrVal *a = GetAttr(name)) {
    if (const bool *b = std::get_if<bool>(&a->v)) {
      return b;
    } else {
      LOG(FATAL) << "Attribute " << name << " was present, but expected "
        "bool type.";
    }
  }
  return nullptr;
}

const BigInt *DocTree::GetIntAttr(const std::string &name) const {
  if (const AttrVal *a = GetAttr(name)) {
    if (const BigInt *b = std::get_if<BigInt>(&a->v)) {
      return b;
    } else {
      LOG(FATAL) << "Attribute " << name << " was present, but expected "
        "int type.";
    }
  }
  return nullptr;
}

const DocTree *DocTree::GetLayoutAttr(const std::string &name) const {
  if (const AttrVal *a = GetAttr(name)) {
    if (const std::shared_ptr<DocTree> *dt =
        std::get_if<std::shared_ptr<DocTree>>(&a->v)) {
      return dt->get();
    } else {
      LOG(FATAL) << "Attribute " << name << " was present, but expected "
        "layout type.";
    }
  }
  return nullptr;
}


static std::string ShortColorLayout(const DocTree &doc) {
  if (doc.IsText()) return std::format(AWHITE("{}"), doc.text);
  else return ATAG("<layout>");
}

std::string AttrValString(const AttrVal &val) {
  if (const std::string *s = std::get_if<std::string>(&val.v)) {
    return std::format("\"{}\"", *s);
  } else if (const uint64_t *u = std::get_if<uint64_t>(&val.v)) {
    return std::format("{}", *u);
  } else if (const double *d = std::get_if<double>(&val.v)) {
    return std::format("{:.17g}", *d);
  } else if (const BigInt *b = std::get_if<BigInt>(&val.v)) {
    return b->ToString();
  } else if (const bool *b = std::get_if<bool>(&val.v)) {
    return b ? "true" : "false";
  } else if (const std::shared_ptr<DocTree> *dt =
             std::get_if<std::shared_ptr<DocTree>>(&val.v)) {
    return ShortColorLayout(**dt);
  } else {
    return "??INVALID??";
  }
}

std::pair<std::string, AttrVal>
ValueToAttrVal(const std::string &field, const bc::Value &val) {
  CHECK(!field.empty());

  auto CheckStripTag = [&field](bc::ObjectFieldType oft) {
      char c = ObjectFieldTypeTag(oft);
      CHECK(field[0] == c) << "Bug: Field tag did not match "
        "type in object? field: " << field;
      return field.substr(1, std::string::npos);
    };

  using map_type = std::unordered_map<std::string, bc::Value *>;

  // Check fields tagged as layout first, since these could also
  // contain strings.
  if (field[0] == ObjectFieldTypeTag(bc::ObjectFieldType::LAYOUT)) {
    std::string f = CheckStripTag(bc::ObjectFieldType::LAYOUT);
    return {f, AttrVal{.v = std::make_shared<DocTree>(ValueToDocTree(&val))}};
  }

  // Otherwise, look at the type of the value.
  if (const std::string *s = std::get_if<std::string>(&val.v)) {
    return {CheckStripTag(bc::ObjectFieldType::STRING), AttrVal{.v = *s}};
  } else if (const uint64_t *u = std::get_if<uint64_t>(&val.v)) {
    // This could either be a boolean or uint64!
    if (field[0] == ObjectFieldTypeTag(bc::ObjectFieldType::BOOL)) {
      return {CheckStripTag(bc::ObjectFieldType::BOOL),
        AttrVal{.v = bool(*u ? true : false)}};
    } else {
      return {CheckStripTag(bc::ObjectFieldType::U64), AttrVal{.v = *u}};
    }
  } else if (const double *d = std::get_if<double>(&val.v)) {
    return {CheckStripTag(bc::ObjectFieldType::FLOAT), AttrVal{.v = *d}};
  } else if (const BigInt *b = std::get_if<BigInt>(&val.v)) {
    return {CheckStripTag(bc::ObjectFieldType::INT), AttrVal{.v = *b}};
  } else if (const map_type *obj = std::get_if<map_type>(&val.v)) {
    (void)obj;
    // Only expecting layout; handled above.
    LOG(FATAL) << "Saw map type that was not layout?";
  } else {
    LOG(FATAL) << "Unsupported attribute type in layout. It must be "
      "BigInt, string, bool, uint64_t, or double. The field was: " << field;
  }
}

DocTree ValueToDocTree(const bc::Value *v) {
  using FS = FunctionalSet<const bc::Value *>;
  // Passing a functional set of seen pointers. We do allow shared nodes
  // within an input tree; we just don't want cycles.
  std::function<DocTree(FS seen, const bc::Value *)> Rec =
    [&Rec](FS seen, const bc::Value *v) -> DocTree {
      if (seen.Contains(v)) {
        Achievements::Get().Achieve("Cycle Correct",
                                    "A cycle in the document?!");
        LOG(FATAL) << "Cycle in document! "
          "What is this, some kind of joke!?";
      }
      seen = seen.Insert(v);

      using map_type = std::unordered_map<std::string, bc::Value *>;
      using vec_type = std::vector<bc::Value *>;

      DocTree doc;
      if (const std::string *s = std::get_if<std::string>(&v->v)) {
        doc.text = *s;
        return doc;
      } else if (const map_type *obj = std::get_if<map_type>(&v->v)) {
        auto ait = obj->find(bc::NODE_ATTRS_LABEL);
        CHECK(ait != obj->end()) << "Nodes always have an attribute object, "
          "even if it is empty.";
        const map_type *aobj = std::get_if<map_type>(&ait->second->v);
        CHECK(aobj != nullptr) << "In a node, the attribute field is always "
          "an object, even if it is empty.";

        auto cit = obj->find(bc::NODE_CHILDREN_LABEL);
        CHECK(cit != obj->end()) << "Nodes always have a children vector, "
          "even if it is empty.";
        const vec_type *cvec = std::get_if<vec_type>(&cit->second->v);
        CHECK(cvec != nullptr) << "In a node, the children field is always "
          "a vector.";

        for (const auto &[k, v] : *aobj) {
          const auto [key, val] = ValueToAttrVal(k, *v);
          doc.attrs[key] = val;
        }

        for (const bc::Value *v : *cvec) {
          doc.AddChild(Rec(seen, v));
        }

        return doc;
      } else {
        LOG(FATAL) << "Bug: Layout values should be represented as either "
          "strings or maps (objects).";
        return doc;
      }
    };

  return Rec(FS(), v);
}


// Calls "new Value" with the args; stores in heap.
template<typename... Args>
static bc::Value *NewValue(std::vector<bc::Value *> *heap, Args&&... args) {
  auto t{std::forward<Args>(args)...};
  bc::Value *v = new bc::Value{.v = std::move(t)};
  heap->push_back(v);
  return v;
}

static bc::Value *NewString(std::vector<bc::Value *> *heap,
                            std::string s) {
  return NewValue(heap, std::move(s));
}

static bc::Value *NewU64(std::vector<bc::Value *> *heap, uint64_t u) {
  return NewValue(heap, u);
}

static bc::Value *NewDouble(std::vector<bc::Value *> *heap, double d) {
  return NewValue(heap, d);
}

static bc::Value *NewInt(std::vector<bc::Value *> *heap, BigInt b) {
  return NewValue(heap, std::move(b));
}

static bc::Value *NewObj(std::vector<bc::Value *> *heap,
                         std::unordered_map<std::string, bc::Value *> m) {
  return NewValue(heap, std::move(m));
}

static bc::Value *NewVec(std::vector<bc::Value *> *heap,
                         std::vector<bc::Value *> v) {
  return NewValue(heap, std::move(v));
}


std::pair<std::string, bc::Value *>
AttrValToValue(std::vector<bc::Value *> *heap,
               const std::string &field,
               const AttrVal &val) {
  auto MakeField = [&field](const bc::ObjectFieldType oft) {
      return std::format("{:c}{}",
                         ObjectFieldTypeTag(oft), field);
    };
  if (const std::string *s = std::get_if<std::string>(&val.v)) {
    return {MakeField(bc::ObjectFieldType::STRING), NewString(heap, *s)};
  } else if (const uint64_t *u = std::get_if<uint64_t>(&val.v)) {
    return {MakeField(bc::ObjectFieldType::U64), NewU64(heap, *u)};
  } else if (const double *d = std::get_if<double>(&val.v)) {
    return {MakeField(bc::ObjectFieldType::FLOAT), NewDouble(heap, *d)};
  } else if (const BigInt *b = std::get_if<BigInt>(&val.v)) {
    return {MakeField(bc::ObjectFieldType::INT), NewInt(heap, *b)};
  } else if (const bool *b = std::get_if<bool>(&val.v)) {
    return {MakeField(bc::ObjectFieldType::BOOL), NewU64(heap, *b ? 1 : 0)};
  } else if (const std::shared_ptr<DocTree> *dt =
             std::get_if<std::shared_ptr<DocTree>>(&val.v)) {
    return {MakeField(bc::ObjectFieldType::LAYOUT),
            DocTreeToValue(heap, **dt)};
  } else {
    LOG(FATAL) << "Invalid AttrVal when converting to Value";
  }
}


bc::Value *DocTreeToValue(std::vector<bc::Value *> *heap, const DocTree &doc) {
  std::function<bc::Value *(const DocTree &doc)> Rec =
    [heap, &Rec](const DocTree &doc) -> bc::Value * {
      if (doc.IsText()) {
        // Text is represented as a string.
        return NewString(heap, doc.text);
      } else {
        std::unordered_map<std::string, bc::Value *> attrs;

        // Should this be by construction/allowlist?
        for (const auto &[key, val] : doc.attrs) {
          const auto &[k, v] = AttrValToValue(heap, key, val);
          attrs[k] = v;
        }

        std::vector<bc::Value *> children;
        children.reserve(doc.children.size());
        for (const std::shared_ptr<DocTree> &c : doc.children) {
          CHECK(c.get() != nullptr) << doc.children.size() << " and " <<
            children.size();
          children.push_back(Rec(*c));
        }

        std::unordered_map<std::string, bc::Value *> node = {
          {bc::NODE_ATTRS_LABEL, NewObj(heap, std::move(attrs))},
          {bc::NODE_CHILDREN_LABEL, NewVec(heap, std::move(children))},
        };

        return NewObj(heap, std::move(node));
      }
    };
  return Rec(doc);
}

static std::string Pad(int depth) {
  return std::string(depth, ' ');
}

bool DocTree::IsText() const {
  // If it has no attrs or children, it is a text node (even if
  // text is also empty).
  return attrs.empty() && children.empty();
}

bool DocTree::IsEmpty() const {
  return text.empty() && attrs.empty() && children.empty();
}

bool DocTree::IsGroup() const {
  return attrs.empty() && !children.empty();
}

void DebugPrintDocTree(const DocTree &doc) {
  std::function<void(int, const DocTree &)> Rec =
    [&Rec](int depth, const DocTree &doc) {
      if (doc.IsText()) {
        // We should be careful about normalizing whitespace here,
        // since it sometimes has meaning.
        const std::string t = Util::NormalizeWhitespace(doc.text);
        Print("{}" ABGCOLOR(30, 30, 30, AFGCOLOR(255, 255, 255, "{}")) "\n",
              Pad(depth),
              t);
      } else {
        // Then it is a regular node.
        if (!doc.text.empty()) {
          Print(ARED("INVALID TEXT \"") "{}" ARED("\""),
                doc.text);
        }

        Print("{}" ATAG("<"), Pad(depth));
        bool first = true;
        for (const auto &[k, v] : doc.attrs) {
          if (!first) Print(", ");
          Print(AATTRNAME("{}") " = " AATTRVAL("{}"),
                k, AttrValString(v));
          first = false;
        }
        Print(ATAG(">") "\n");
        for (const auto &child : doc.children) {
          Rec(depth + 2, *child);
        }
        Print("{}" ATAG("</>") "\n", Pad(depth));
      }
    };
  Rec(0, doc);
}

std::string DocText(const DocTree &doc) {
  std::function<std::string(const DocTree &)> Rec =
    [&Rec](const DocTree &doc) -> std::string {
      if (doc.IsText()) {
        return doc.text;
      } else {
        std::string ret;
        for (const auto &child : doc.children) {
          ret += Rec(*child);
        }
        return ret;
      }
    };
  return Rec(doc);
}


DocTree JoinDocs(std::vector<DocTree> docs) {
  if (docs.empty()) {
    return DocTree();
  }

  if (docs.size() == 1) {
    return std::move(docs[0]);
  }

  DocTree doc;
  doc.children.reserve(docs.size());
  for (DocTree &d : docs) {
    doc.AddChild(std::move(d));
  }
  docs.clear();
  return doc;
}

// has space before, word, has space after
static std::tuple<bool, std::string_view, bool>
NextWord(std::string_view &text) {
  bool space_before = false;
  while (!text.empty() && text[0] == ' ') {
    space_before = true;
    text.remove_prefix(1);
  }

  auto pos = text.find(' ');
  if (pos == std::string_view::npos) {
    // No more spaces. Eat until the end of the string.
    std::string_view word = text;
    text.remove_prefix(word.size());
    return std::make_tuple(space_before, word, false);
  }

  bool space_after = false;
  std::string_view word = text.substr(0, pos);
  text.remove_prefix(pos);
  while (!text.empty() && text[0] == ' ') {
    space_after = true;
    text.remove_prefix(1);
  }
  return std::make_tuple(space_before, word, space_after);
}

std::vector<DocTree>
Document::BoxifyText(const TextProps &props,
                     std::string_view text) {
  static constexpr bool VERBOSE = false;

  std::vector<DocTree> out;

  if (VERBOSE) {
    Print("Boxify text with desc " ABLUE("{}") "\n",
          FontDescriptionString(props.desc));
  }

  const Font *font = GetDescribedFont(props.desc);

  const double space_width = font->CharWidth(' ');
  const double hyphen_width = font->CharWidth('-');

  auto IsWhitespace = [](std::string_view s) {
      // Because of the way we normalize whitespace, the only
      // case where the word is whitespace will be a single
      // space.
      return s.size() == 1 && s[0] == ' ';
  };

  auto ApplyTextProps = [props, font](DocTree *doc) {
      // PERF can leave them off if default
      doc->SetDoubleAttr("font-size", props.font_size);
      doc->SetStringAttr("font-name", font->Name());
      doc->SetIntAttr("font-color", BigInt(props.font_color));
    };

  // Work a word at a time.
  while (!text.empty()) {
    const auto &[space_before, word_in, space_after] = NextWord(text);
    if (word_in.empty()) {
      CHECK(!space_after) << "Bug: The empty word should "
        "cause the space to be one?";
      if (space_before) {
        // This happens if the doctree node only contains space.
        // We preserve it for now, although there are situations
        // where a natural document doesn't want a space. Maybe
        // better to drop those elsewhere.
        // word = "_";
      } else {
        continue;
      }
    }

    // Split off punctuation leading or following the word.
    const auto &[prefix, word, suffix] =
      Hyphenation::SplitPunctuation(word_in);

    // TODO: Detect actual hyphens in the word, which would
    // allow breaks after them too!
    std::vector<std::string> hyphen_parts;
    if (!word.empty()) {
      hyphen_parts = [&]() -> std::vector<std::string> {
        for (int i = 0; i < (int)word.size(); i++) {
          if (!std::isalpha(word[i])) return {std::string(word)};
        }
        return hyphenation.Hyphenate(word);
      }();
    }

    // Now we put the prefix and suffix back on the parts, including
    // the rare "space before" obligation. It may be that there were
    // no parts, (even rarer!) but then we'll create one.
    if (hyphen_parts.empty() &&
        (space_before || !prefix.empty() || !suffix.empty())) {
      hyphen_parts.push_back("");
    }

    if (!prefix.empty()) {
      hyphen_parts[0] = std::string(prefix) + hyphen_parts[0];
    }
    if (!suffix.empty()) {
      hyphen_parts.back() = hyphen_parts.back() + std::string(suffix);
    }
    if (space_before) {
      hyphen_parts[0] = " " + hyphen_parts[0];
    }

    // Now turn the word into boxes. We read successive codepoints
    // from each hyphenated piece.

    // Then to turn the word into boxes, read successive codepoints
    // from it.
    uint32_t prev = ' ';
    std::string chunk;
    double chunk_width = 0.0;
    for (int hyidx = 0; hyidx < (int)hyphen_parts.size(); hyidx++) {
      const std::string &hypart = hyphen_parts[hyidx];
      if (VERBOSE) {
        Print(AGREY("[{}]") "\n", hypart);
      }
      auto codepoints = UTF8::Decoder(hypart);
      for (auto it = codepoints.begin(); it != codepoints.end(); ++it) {
        const uint32_t codepoint = *it;
        double char_width = font->CharWidth(codepoint);
        std::optional<double> kern = font->GetKerning(prev, codepoint);

        // Do we break before this character?
        const bool break_for_kern = kern.has_value();
        // First character of a hyphen part, except the first one.
        const bool break_for_hyphen = [&]() {
            if (hyidx == 0) return false;
            return it == codepoints.begin();
          }();

        if (VERBOSE) {
          Print("[{}] -> [{}] {:.3f} width{}{}\n",
                UTF8::Encode(prev),
                UTF8::Encode(codepoint),
                char_width,
                break_for_kern ? " " AGREEN("kern") : "",
                break_for_hyphen ? " " APURPLE("hyph") : "");
        }

        if (break_for_kern || break_for_hyphen) {
          // If there is a kerning pair, make an unbreakable box to
          // contain the chunk so far.
          DocTree d;
          d.SetStringAttr("display", "box");
          d.SetDoubleAttr("width", chunk_width * props.font_size);
          d.SetDoubleAttr("height", props.font_size + props.line_spacing);
          // PERF The font is normalized onto every chunk, which may be kinda
          // expensive.
          ApplyTextProps(&d);

          if (break_for_hyphen) {
            // If breaking for hyphen, then we have a penalty,
            d.SetDoubleAttr("glue-break-penalty", HYPHEN_PENALTY);
            // XXX this should get text props too?
            d.SetLayoutAttr("glue-break-insert", TextDoc("-"));
            // Not including the kerning, since we are breaking between
            // the two characters.
            d.SetDoubleAttr("glue-break-extra-width", hyphen_width);
          } else {
            // Since this is inside a word, we disable breaks here by setting
            // the break penalty "infinite".
            d.SetDoubleAttr("glue-break-penalty", INFINITE_PENALTY);
          }

          // Either way, since this is inside a word, we set the
          // glue coefficients infinitesimally small so that we
          // don't apportion glue here unless forced.
          d.SetDoubleAttr("glue-expand", EPSILON_COEFFICIENT);
          d.SetDoubleAttr("glue-contract", EPSILON_COEFFICIENT);

          d.AddChild(TextDoc(chunk));
          out.push_back(std::move(d));

          chunk = UTF8::Encode(codepoint);
          chunk_width = char_width;
          if (break_for_kern) {
            CHECK(kern.has_value());
            // Typically negative.
            chunk_width += kern.value();
          }

          if (VERBOSE) {
            Print("Kerned '{}'+'{}'; chunk now '{}'\n",
                  UTF8::Encode(prev),
                  UTF8::Encode(codepoint),
                  chunk);
          }
        } else {
          // Extend the chunk.
          chunk += UTF8::Encode(codepoint);
          chunk_width += char_width;
          if (VERBOSE) {
            Print("Added '{}'; chunk now '{}'\n",
                  UTF8::Encode(codepoint),
                  chunk);
          }
        }
        prev = codepoint;
      }
    }

    CHECK(!chunk.empty()) << "We only break on kerning pairs, so the chunk "
      "is never empty for non-empty words! Word: [" << word << "]";

    // The final chunk, which is frequently the entire word, allows a break
    // after it.
    DocTree d;
    d.SetStringAttr("display", "box");
    d.SetDoubleAttr("width", chunk_width * props.font_size);
    d.SetDoubleAttr("height", props.font_size + props.line_spacing);
    ApplyTextProps(&d);

    if (space_after || IsWhitespace(word)) {
      // No penalty to break between words. We could use some heuristics to
      // penalize certain breaks in the future. (For example, breaking after
      // a comma or semicolon seems better.)
      d.SetDoubleAttr("glue-break-penalty", 0.0);
      d.SetDoubleAttr("glue-ideal", space_width * props.font_size);
      // Relative penalty for contracting.
      d.SetDoubleAttr("glue-contract", 4.0);
    } else {
      d.SetDoubleAttr("glue-break-penalty", INFINITE_PENALTY);
      d.SetDoubleAttr("glue-ideal", 0.0);
      // Since there's no space, treat it like it's inside a word for
      // the purposes of glue allocation.
      d.SetDoubleAttr("glue-expand", EPSILON_COEFFICIENT);
      d.SetDoubleAttr("glue-contract", EPSILON_COEFFICIENT);
    }

    d.AddChild(TextDoc(chunk));
    out.push_back(std::move(d));
  }

  if (VERBOSE) {
    Print(ABGCOLOR(255, 0, 0, AFGCOLOR(255, 255, 255, " ==== Boxified ==== "))
          "\n");

    for (const DocTree &x : out) {
      DebugPrintDocTree(x);
    }

    Print(ABGCOLOR(255, 0, 0, AFGCOLOR(255, 255, 255, " ================== "))
          "\n");
  }

  return out;
}

std::string Document::LoadFontFile(std::string_view filename) {
  LOG(FATAL) << "(LoadFontFile) The abstract base class of Document does "
    "not understand fonts on its own!";
  return "";
}

void Document::GenerateOutput(
    std::string_view filename,
    const std::map<int, std::map<int, DocTree>> &pages) {
  LOG(FATAL) << "(LoadFontFile) The abstract base class of Document does "
    "not know how to make output on its own!";
}

void Document::SetDocumentInfoStrings(
    const std::unordered_map<std::string, std::string> &info) {
  // Base class does nothing with this.
}

void Document::SetDocumentInfo(
    const std::unordered_map<std::string, AttrVal> &info) {
  std::unordered_map<std::string, std::string> m;
  for (const auto &[k, v] : info) {
    if (k == "width") {
      const double *w = std::get_if<double>(&v.v);
      CHECK(w != nullptr) << "Document info 'width' must be a double "
        "(in points).";
      width = *w;
    } else if (k == "height") {
      const double *h = std::get_if<double>(&v.v);
      CHECK(h != nullptr) << "Document info 'height' must be a double "
        "(in points).";
      height = *h;
    } else if (const std::string *s = std::get_if<std::string>(&v.v)) {
      m[k] = *s;
    }
  }
  SetDocumentInfoStrings(m);
}

std::string Document::AddImage(std::unique_ptr<ImageRGBA> img) {
  LOG(FATAL) << "AddImage in base document is not available.";
}

std::string Document::AddImage(std::unique_ptr<SVG::Doc> img) {
  LOG(FATAL) << "AddImage in base document is not available.";
}

void Document::SetPageInfo(
    int page_idx, int frame_idx,
    const std::unordered_map<std::string, AttrVal> &attrs) {
  // Base document ignores it.
}

std::string Document::LoadImageFile(std::string_view filename) {
  if (Util::EndsWith(filename, ".svg")) {
    std::string error;
    std::optional<SVG::Doc> odoc = SVG::Parse(
        Util::ReadFile(filename), &error);
    if (!odoc.has_value()) {
      Print(stderr, "Error loading {}: {}\n", filename, error);
      return "";
    }

    return AddImage(std::make_unique<SVG::Doc>(std::move(odoc.value())));
  }

  // Otherwise, any raster format supported by ImageRGBA.
  std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(filename));
  if (img.get() == nullptr)
    return "";

  return AddImage(std::move(img));
}

std::string Document::NextImageHandle() {
  return std::format("img{}", image_counter++);
}

void Document::AddImageWithHandle(std::string name,
                                  std::unique_ptr<Image> img) {
  CHECK(!images.contains(name)) << "Duplicate image handle: " << name;
  images[std::move(name)] = std::move(img);
}


const Image *Document::GetImageByName(std::string_view name) {
  const auto it = images.find(std::string(name));
  CHECK(it != images.end() && it->second.get() != nullptr) << "Unknown "
    "image handle " << name;
  return it->second.get();
}

const Font *Document::GetFontByName(std::string_view font_name) {
  const auto it = fonts.find(std::string(font_name));
  if (it == fonts.end()) {
    Print("Registered fonts:\n");
    for (const auto &[font_name, _] : fonts) {
      Print("  {}\n", font_name);
    }
    LOG(FATAL) << "Unknown font name " << font_name;
  }
  CHECK(it->second.get() != nullptr) << "Null font pointer??";
  return it->second.get();
}

std::string Document::FontDescriptionString(const FontDescription &fd) {
  return std::format("{{ font_family = {}, font_bold = {}, font_italic = {} }}",
                     fd.font_family,
                     fd.font_bold ? "true" : "false",
                     fd.font_italic ? "true" : "false");
}

void Document::RegisterFont(const FontDescription &desc, const Font *f) {
  FontFamily &family = font_families[desc.font_family];
  if (desc.font_bold && desc.font_italic) {
    family.bold_italic = f;
  } else if (desc.font_bold) {
    family.bold = f;
  } else if (desc.font_italic) {
    family.italic = f;
  } else {
    family.regular = f;
  }
}

const Font *Document::GetDefaultFont() {
  LOG(FATAL) << "No default font for base Document class.";
  return nullptr;
}

uint32_t Document::IntToColor(const char *what, const BigInt &b) {
  auto co = b.ToInt();
  CHECK(co.has_value() && co.value() >= 0 &&
        co.value() <= int64_t{0xFFFFFFFF}) << "(" << what <<
    ") Color is out of range. Must be in [0, 0xFFFFFFFF]: " <<
    b.ToString();
  return (uint32_t)co.value();
}

const Font *Document::GetDescribedFont(const FontDescription &desc) {
  const auto it = font_families.find(desc.font_family);
  if (it == font_families.end()) {
    LOG(FATAL) << "Unknown font family: " << desc.font_family;
  }
  const FontFamily &family = it->second;

  if (VERBOSE) {
    Print("Get described font: " APURPLE("{}") "\n",
          FontDescriptionString(desc));
  }

  if (desc.font_bold && desc.font_italic && family.bold_italic)
    return family.bold_italic;

  if (desc.font_italic && family.italic)
    return family.italic;

  if (desc.font_bold && family.bold)
    return family.bold;

  if (family.regular)
    return family.regular;

  // Then this is a weird case where we only have bold/italic/bold-italic,
  // and we weren't asking for that! Just return something...
  if (family.italic)
    return family.italic;
  if (family.bold)
    return family.bold;
  if (family.bold_italic)
    return family.bold_italic;
  LOG(FATAL) << "Font family has no faces?";
  return nullptr;
}

std::string NormalizeWhitespace(const std::string &s) {
  std::string ret;
  ret.reserve(s.size());
  bool skip_ws = false;
  for (char c : s) {
    switch (c) {
    case ' ':
    case '\n':
    case '\r':
    case '\t':
      if (skip_ws) continue;
      ret += ' ';
      skip_ws = true;
      break;
    default:
      skip_ws = false;
      ret.push_back(c);
    }
  }
  return ret;
}

// Converts layout (spans with style) into boxes that have
// definite size and glue.
DocTree Document::GetBoxes(const DocTree &doc) {
  std::vector<DocTree> out;

  std::function<void(TextProps props, const DocTree &)> Rec =
    [this, &out, &Rec](TextProps props, const DocTree &doc) {

      if (doc.IsText()) {

        std::string normtext = NormalizeWhitespace(doc.text);

        std::vector<DocTree> boxes =
          BoxifyText(props, doc.text);
        for (DocTree &d : boxes)
          out.push_back(std::move(d));
        boxes.clear();

      } else {
        if (const std::string *display = doc.GetStringAttr("display")) {
          if (*display == "box" || *display == "sticker") {
            // This is already a box with a fixed size, so we just copy it.
            out.push_back(doc);
            return;
          } else if (*display == "span") {
            if (const std::string *f = doc.GetStringAttr("font-face")) {
              props.desc.font_family = *f;
            }

            if (const double *d = doc.GetDoubleAttr("font-size")) {
              props.font_size = *d;
            }

            if (const bool *b = doc.GetBoolAttr("font-bold")) {
              props.desc.font_bold = *b;
            }

            if (const bool *b = doc.GetBoolAttr("font-italic")) {
              props.desc.font_italic = *b;
            }

            if (const BigInt *bc = doc.GetIntAttr("font-color")) {
              props.font_color = IntToColor("font-color", *bc);
            }

            if (const double *h = doc.GetDoubleAttr("line-spacing")) {
              props.line_spacing = *h;
            }

          } else {
            LOG(FATAL) << "Unknown display: " << *display;
          }
        }

        // Process children.
        for (const std::shared_ptr<DocTree> &child : doc.children) {
          Rec(props, *child);
        }
      }
    };

  // DebugPrintDocTree(doc);

  // Get default text props somehow?
  TextProps props;
  props.desc.font_family = "times";
  Rec(props, doc);
  return JoinDocs(std::move(out));
}

// Lower penalties are better.
static constexpr ColorUtil::Gradient BEST_WORST{
  /*  GradRGB(0.00f, 0xFFFFFF),
  GradRGB(0.01f, 0xFFFF00),
  GradRGB(0.05f, 0x00FF00),
  GradRGB(0.50f, 0x2222FF),
  GradRGB(1.00f, 0xFF0000),*/

  GradRGB(0.00f, 0x88FFAA),
  GradRGB(0.05f, 0x00FF00),
  GradRGB(0.70f, 0x660000),
  GradRGB(1.0f,  0xFF0000),
};


std::pair<std::vector<DocTree>, double>
Document::PackBoxes(Algorithm algo,
                    BoxesAndGlue::Justification just,
                    double orphan_threshold,
                    double line_width,
                    const DocTree &doc) {
  static constexpr bool VERBOSE = false;
  using BoxIn = BoxesAndGlue::BoxIn;
  using BoxOut = BoxesAndGlue::BoxOut;

  if (doc.IsEmpty()) return {{}, 0.0};
  CHECK(!doc.IsText()) <<
    "pack-boxes wants a node that has only box children. Got text: " <<
    doc.text;

  std::vector<BoxIn> children;

  auto GetBox = [](const DocTree &doc) -> BoxIn {
      const double *width = doc.GetDoubleAttr("width");
      if (width == nullptr) {
        Print("\n\nErroneous doc:\n");
        DebugPrintDocTree(doc);
        LOG(FATAL) << "In pack-boxes, encountered a top-level box "
          "that has no width. This probably means that you didn't do get-boxes "
          "or you messed up the boxes after that, or there's a bug in "
          "get-boxes (could have been anyone?)";
      }

      BoxIn b;
      b.width = *width;
      b.data = const_cast<void*>((const void*)&doc);
      if (const double *bw = doc.GetDoubleAttr("glue-break-extra-width")) {
        b.glue_break_extra_width = *bw;
      }

      if (const double *glue = doc.GetDoubleAttr("glue-ideal")) {
        b.glue_ideal = *glue;
      }

      if (const double *pen = doc.GetDoubleAttr("glue-break-penalty")) {
        b.glue_break_penalty = *pen;
      }

      if (const double *e = doc.GetDoubleAttr("glue-expand")) {
        b.glue_expand = *e;
      }

      if (const double *c = doc.GetDoubleAttr("glue-contract")) {
        b.glue_contract = *c;
      }

      return b;
    };

  std::vector<BoxIn> boxes = [&]() {
      std::vector<BoxIn> boxes;
      if (const std::string *display = doc.GetStringAttr("display")) {
        if (*display == "box") {
          // Just one box.
          boxes.push_back(GetBox(doc));
          return boxes;
        }
      }

      // Then we expect the direct children (if any) to be the boxes.
      // XXX perhaps we should be warning if there are attributes
      // (on doc) here, as they will be dropped?
      for (const auto &child : doc.children) {
        const std::string *display = child->GetStringAttr("display");
        CHECK(display != nullptr) << "In PackBoxes, expected a series "
          "of boxes. Probably need to call GetBoxes first?";
        boxes.push_back(GetBox(*child));
      }
      return boxes;
    }();

  for (int i = 0; i < (int)boxes.size(); i++) {
    boxes[i].parent_idx = i - 1;
    boxes[i].edge_penalty = 0.0;
  }

  // Penalize orphans, if enabled.
  if (orphan_threshold > 0.0 && !boxes.empty()) {
    // static constexpr
    const bool is_multiline = [line_width, &boxes]() {
        double total_width = 0.0;
        for (const BoxIn &box : boxes) {
          total_width += box.width + box.glue_ideal;
        }
        return total_width > line_width;
      }();
    if (is_multiline) {
      // Working from the back of the text, the amount of
      // width that would (heuristically) be on the last line
      // if our last break was here.
      double orphan_width = boxes.back().width;
      for (int i = (int)boxes.size() - 2; i >= 0; i--) {
        // Breaking after this box would mean leaving the
        // last two lines like this
        //
        // blah blah blah blah blah blah blah blah [this_token]
        // [later tokens]     |       ... empty ...
        //              ^     ^
        //       orphan w   orphan threshold
        double space_to_thresh = orphan_threshold - orphan_width;
        if (space_to_thresh <= 0.0) {
          // At this point there will be no more orphan penalties
          // (assuming positive widths, etc.)
          break;
        }

        // penalize proportional to space_to_thresh.
        boxes[i].glue_break_penalty +=
          std::pow(space_to_thresh, 1.5);

        orphan_width += boxes[i].width + boxes[i].glue_ideal;
      }
    }
  }

  std::vector<std::vector<BoxesAndGlue::BoxOut>> lines;
  switch (algo) {
  case Algorithm::FIRST: {
    // Allows hyphens.
    static constexpr double MAX_BREAK_PENALTY = 200.0;
    lines = BoxesAndGlue::PackBoxesFirst(
        line_width, boxes, MAX_BREAK_PENALTY, just);
    break;
  }
  case Algorithm::BEST: {

    std::unique_ptr<BoxesAndGlue::Table> table;
    lines = BoxesAndGlue::PackBoxes(line_width, boxes, just, &table);
    static constexpr bool SAVE_TABLE = false;
    static int image_num = 0;
    if (SAVE_TABLE && image_num == 0) {
      int wordlen = 0;
      std::vector<std::string> words;
      for (const BoxesAndGlue::BoxIn &box : boxes) {
        const DocTree *doc = (const DocTree*)box.data;
        std::string w = DocText(*doc);
        wordlen = std::max((int)w.size(), wordlen);
        words.push_back(w);
      }


      // We actually render this transposed.
      int num_words = table->Width();
      int num_before = table->Height();
      constexpr int FONT_SIZE = 18;
      constexpr int CELL_SIZE = 21;
      constexpr int MARGIN_SIZE = 2;
      int WORD_COL = wordlen * FONT_SIZE + 4;
      ImageRGBA img(WORD_COL + num_before * CELL_SIZE, num_words * CELL_SIZE);
      Print("Table size {} x {}\n", num_before, num_words);

      // Get all values so we can compute rank.
      std::vector<double> values;
      for (int y = 0; y < num_words; y++) {
        for (int x = 0; x < num_before; x++) {
          if (auto co = table->GetCell(x, y)) {
            const auto &[p, s, b] = co.value();
            values.push_back(p);
          }
        }
      }
      CHECK(!values.empty());

      {
        for (double v : values) CHECK(std::isfinite(v));
        std::sort(values.begin(), values.end());
        auto it = std::unique(values.begin(), values.end());
        values.erase(it, values.end());
      }

      double minv = values[0];
      double maxv = values.back();
      double range = maxv - minv;

      // Get a value's rank as a number in [0, 1].
      auto Rank = [&values](double v) {
          const int idx = std::lower_bound(values.begin(),
                                           values.end(),
                                           v) - values.begin();
          return std::clamp(idx / (double)values.size(), 0.0, 1.0);
        };

      auto MapValue = [&Rank, minv, range](double v) {
          (void)minv;
          (void)range;
          // return (v - minv) / range;
          return Rank(v);
        };

      for (int y = 0; y < num_words; y++) {
        img.BlendText2x32(0, y * CELL_SIZE + (CELL_SIZE - FONT_SIZE) / 2,
                          0xFFFFFFFF,
                          words[y]);

        for (int x = 0; x < num_before; x++) {
          // Note transposition.
          if (auto co = table->GetCell(y, x)) {
            const auto &[p, s, b] = co.value();
            double f = MapValue(p);

            uint32_t color = ColorUtil::LinearGradient32(
                BEST_WORST, f);

            img.BlendRect32(WORD_COL + x * CELL_SIZE, y * CELL_SIZE,
                            CELL_SIZE, CELL_SIZE,
                            color);
            if (b) {
              img.BlendRect32(WORD_COL + x * CELL_SIZE + MARGIN_SIZE + 1,
                              y * CELL_SIZE + MARGIN_SIZE + 1,
                              CELL_SIZE - ((MARGIN_SIZE + 1) * 2),
                              CELL_SIZE - ((MARGIN_SIZE + 1) * 2),
                              0x00000055);
              img.BlendBox32(WORD_COL + x * CELL_SIZE + MARGIN_SIZE,
                             y * CELL_SIZE + MARGIN_SIZE,
                             CELL_SIZE - (MARGIN_SIZE * 2),
                             CELL_SIZE - (MARGIN_SIZE * 2),
                             0x00000055, {0x00000044});
              /*
              img.BlendBox32(x * CELL_SIZE, y * CELL_SIZE,
                             CELL_SIZE, CELL_SIZE,
                             0x00000055, {0x00000022});
              */
            }
          }
        }
      }

      // Draw path.

      // position of the starting word in this line
      int start_word = 0;
      int before = 0;
      int word_idx = 0;
      while (word_idx < num_words) {
        auto co = table->GetCell(word_idx, before);
        CHECK(co.has_value());
        const auto &[p, s, b] = co.value();
        // Print("At {},{} we have {}\n", dx, dy, b ? "BREAK" : "NO");
        if (b) {
          int sx = 0;
          int sy = start_word;
          int dx = before;
          int dy = word_idx;
          // Draw the diagonal.
          img.BlendThickLine32(WORD_COL + sx * CELL_SIZE + CELL_SIZE * 0.5f,
                               sy * CELL_SIZE + CELL_SIZE * 0.5f,
                               WORD_COL + dx * CELL_SIZE + CELL_SIZE * 0.5f,
                               dy * CELL_SIZE + CELL_SIZE * 0.5f,
                               2.5f,
                               0x000000AA);
          img.BlendThickLine32(WORD_COL + dx * CELL_SIZE + CELL_SIZE * 0.5f,
                               dy * CELL_SIZE + CELL_SIZE * 0.5f,
                               WORD_COL + CELL_SIZE * 0.5f,
                               (dy + 1) * CELL_SIZE + CELL_SIZE * 0.5f,
                               2.5f,
                               0x000022777);
          before = 0;
          word_idx++;
          start_word = word_idx;
        } else {
          before++;
          word_idx++;
        }
      }

      image_num++;
      img.Save(std::format("boxes-and-glue-{}.png", image_num));
    }
    break;
  }
  }

  double total_badness = 0.0;
  for (const std::vector<BoxOut> &box_line : lines) {
    for (const BoxOut &box : box_line) {
      // There can be negative badness, but this is mostly used
      // to force line breaks and stuff like that. Here we
      // report badness as only the positive penalties.
      if (box.penalty_here > 0.0) {
        total_badness += box.penalty_here;
      }
    }
  }

  // Now put the boxes on lines in doctree format.

  // Remove attributes that are consumed by this algorithm.
  auto CleanAttrs = [](DocTree *doc) {
      doc->RemoveAttr("glue-expand");
      doc->RemoveAttr("glue-break-extra-width");
      doc->RemoveAttr("glue-contract");
      doc->RemoveAttr("glue-break-penalty");
      doc->RemoveAttr("glue-break-insert");
      doc->RemoveAttr("glue-ideal");
    };

  std::vector<DocTree> lines_out;
  for (std::vector<BoxesAndGlue::BoxOut> &box_line : lines) {
    DocTree line;
    if (!box_line.empty() && box_line[0].left_padding != 0.0) {
      DocTree space;
      space.SetStringAttr("display", "box");
      space.SetDoubleAttr("width", box_line[0].left_padding);
      line.AddChild(space);
    }
    for (int i = 0; i < (int)box_line.size(); i++) {
      BoxesAndGlue::BoxOut &box = box_line[i];

      // The box's user data points to the original doctree node.
      // Copy the node from the input, but we set its width to
      // include glue, and possibly add hyphens.
      DocTree d = *(const DocTree*)box.box->data;

      std::optional<DocTree> insertion;
      if (box.did_break) {
        if (const DocTree *insert = d.GetLayoutAttr("glue-break-insert")) {
          // Copy the word node so that we have its text properties, etc.
          DocTree ins = d;
          CleanAttrs(&ins);
          ins.SetDoubleAttr("width", box.box->glue_break_extra_width);
          // And the hyphen (typically text) will be its one child.
          ins.ClearChildren();
          ins.AddChild(*insert);
          insertion = {std::move(ins)};
          if (VERBOSE) {
            Print(ABGCOLOR(255, 255, 0, "HYPHEN:") "\n");
            DebugPrintDocTree(insertion.value());
          }
        }

        // Accounted for extra width above. No glue when breaking.
        d.SetDoubleAttr("width", box.box->width);
      } else {
        d.SetDoubleAttr("width", box.box->width + box.actual_glue);
      }

      CleanAttrs(&d);
      line.AddChild(d);
      if (insertion.has_value()) {
        line.AddChild(insertion.value());
      }
    }

    line.SetStringAttr("display", "box");
    line.SetDoubleAttr("width", line_width);

    lines_out.push_back(std::move(line));
  }

  return {std::move(lines_out), total_badness};
}

using Transform = Document::Transform;

static inline Transform Translate(Transform t, double dx, double dy) {
  t.dx += dx;
  t.dy += dy;
  return t;
}

void Document::PlaceStickersRec(Context context,
                                Transform transform,
                                const DocTree &doc,
                                Page *page) {
  static constexpr bool DEBUG_STICKERS = false;

  if (DEBUG_STICKERS) {
    Print(AWHITE("PlaceStickersRec") ":\n");
    DebugPrintDocTree(doc);
    Print("\n");
  }

  if (doc.IsText()) {
    // Place the text with the current transform.
    page->DrawText(context.font,
                   doc.text,
                   context.font_size,
                   transform.dx, transform.dy,
                   context.color);
    return;
  }

  if (doc.IsEmpty())
    return;

  if (doc.IsGroup()) {
    for (const std::shared_ptr<DocTree> &child : doc.children) {
      PlaceStickersRec(context, transform, *child, page);
    }
    return;
  }

  auto Error = [&doc]() -> const char * {
      Print(ARED("In this document tree") ":\n");
      DebugPrintDocTree(doc);
      return "\n";
    };

  // Otherwise, the node should be a sticker.
  const std::string *display = doc.GetStringAttr("display");
  CHECK(display != nullptr) << Error() << "Any non-group node has "
    "to have a display when rendering the page.";

  CHECK(*display == "sticker") << Error() << "At this point "
    "(PlaceStickersRec) everything should be stickers. Got node "
    "with display=" << *display;

  const double *x = doc.GetDoubleAttr("x");
  const double *y = doc.GetDoubleAttr("y");
  CHECK(x != nullptr && y != nullptr) << Error() << "In PlaceStickersRec: "
    "every sticker should have its final x= and y= coordinates.";

  if (DEBUG_STICKERS) {
    const double *w = doc.GetDoubleAttr("width");
    const double *h = doc.GetDoubleAttr("height");
    double ww = w == nullptr ? 1.0 : *w;
    double hh = h == nullptr ? 1.0 : *h;
    Transform ct = Translate(transform, *x, *y);
    page->DrawRect(ct.dx, ct.dy, ww, hh,
                   1.0, 0xFF000033,
                   0xFF0000FF);
  }

  if (const double *w = doc.GetDoubleAttr("rect-width")) {
    const double *h = doc.GetDoubleAttr("rect-height");
    CHECK(w != nullptr && h != nullptr) << "sticker with rect "
      "must have both width and height.";

    if (const BigInt *fill = doc.GetIntAttr("fill-color")) {
      uint32_t color = IntToColor("fill-color", *fill);
      Transform ct = Translate(transform, *x, *y);
      page->DrawRect(ct.dx, ct.dy, *w, *h,
                     0.0, color, 0x00000000);
    }

    if (const BigInt *stroke = doc.GetIntAttr("stroke-color")) {
      uint32_t color = IntToColor("stroke-color", *stroke);
      const double *t = doc.GetDoubleAttr("line-width");
      CHECK(t != nullptr) << "rect sticker with stroke requires width, height, "
        "and linewidth.";
      Transform ct = Translate(transform, *x, *y);
      page->DrawRect(ct.dx, ct.dy, *w, *h,
                     *t, 0x00000000, color);
    }
  }

  if (const double *x1 = doc.GetDoubleAttr("line-x1")) {
    const double *y1 = doc.GetDoubleAttr("line-y1");
    const double *t = doc.GetDoubleAttr("line-width");
    const BigInt *stroke = doc.GetIntAttr("stroke-color");
    CHECK(x1 != nullptr && y1 != nullptr && t != nullptr &&
          stroke != nullptr) << "sticker with line "
      "must have line-x1, line-y1, line-width, and stroke-color.";
    uint32_t color = IntToColor("stroke-color", *stroke);
    Transform ct0 = Translate(transform, *x, *y);
    Transform ct1 = Translate(transform, *x1, *y1);
    page->DrawLine(ct0.dx, ct0.dy, ct1.dx, ct1.dy, *t, color);
  }

  // XXX This is incomplete, and maybe we should just recommend
  // SVG for anything but the most trivial vector stuff?
  if (const double *x1 = doc.GetDoubleAttr("polygon")) {
    const double *t = doc.GetDoubleAttr("line-width");
    const BigInt *stroke = doc.GetIntAttr("stroke-color");
    const BigInt *fill = doc.GetIntAttr("fill-color");

    // line-width and stroke-color should be optional,
    // fill-color too (but you should have at least one?)
    uint32_t stroke_color = IntToColor("stroke-color", *stroke);
    uint32_t fill_color = IntToColor("fill-color", *fill);

    Transform ct0 = Translate(transform, *x, *y);
    LOG(FATAL) << "Need to parse the polygon, transform its "
      "vertices, and then render.";
    // page->DrawLine(ct0.dx, ct0.dy, ct1.dx, ct1.dy, *t, color);
  }

  if (const std::string *img = doc.GetStringAttr("img")) {
    const double *width = doc.GetDoubleAttr("img-width");
    const double *height = doc.GetDoubleAttr("img-height");
    CHECK(width != nullptr && height != nullptr) << "An img=\"\" on a "
      "sticker also requires img-width=\"\" and img-height\"\" (doubles).";
    const Image *image = GetImageByName(*img);
    Transform ct = Translate(transform, *x, *y);
    if (image == nullptr) {
      Print(ARED("Missing image: ") "{}\n", *img);
    } else {
      page->DrawImage(image, ct.dx, ct.dy, *width, *height);
    }
  }

  if (const std::string *video = doc.GetStringAttr("video")) {
    const double *width = doc.GetDoubleAttr("video-width");
    const double *height = doc.GetDoubleAttr("video-height");
    const bool *loop = doc.GetBoolAttr("video-loop");
    CHECK(width != nullptr && height != nullptr) << "A video=\"\" on a "
      "sticker also requires video-width=\"\" and video-height\"\" (doubles).";
    Transform ct = Translate(transform, *x, *y);
    page->DrawVideo(ct.dx, ct.dy, *width, *height, *video,
                    (loop != nullptr) && *loop);
  }

  if (const std::string *font_name = doc.GetStringAttr("font-name")) {
    const Font *f = GetFontByName(*font_name);
    if (f == nullptr) {
      Print(ARED("Missing font: ") "{}\n", *font_name);
    } else {
      context.font = f;
    }
  }

  if (const double *font_size = doc.GetDoubleAttr("font-size")) {
    context.font_size = *font_size;
  }

  if (const BigInt *bc = doc.GetIntAttr("font-color")) {
    context.color = IntToColor("font-color", *bc);
  }

  // XXX scaling

  for (const std::shared_ptr<DocTree> &child : doc.children) {
    Transform ct = Translate(transform, *x, *y);
    PlaceStickersRec(context, ct, *child, page);
  }
}

std::string Font::Name() const {
  LOG(FATAL) << "Font base class does not have font names.";
  return "";
}


std::optional<double>
Font::GetKerning(int codepoint1, int codepoint2) const {
  return std::nullopt;
}

double Font::CharWidth(int codepoint) const {
  LOG(FATAL) << "Font base class cannot produce widths.";
  return 0.0;
}

void Page::DrawText(const Font *font,
                    std::string_view text, double size,
                    double x, double y,
                    uint32_t color) {
}

void Page::DrawImage(const Image *image,
                     double x, double y,
                     double width, double height) {
}

void Page::DrawVideo(double x, double y,
                     double width, double height,
                     std::string_view src,
                     bool loop) {
}

void Page::DrawRect(double x, double y,
                    double width, double height,
                    double border_width, uint32_t color_fill,
                    uint32_t color_border) {

}

void Page::DrawLine(double x0, double y0,
                    double x1, double y1,
                    double line_width,
                    uint32_t stroke_color) {

}


Page::Page() {}
Font::Font() {}
Image::Image() {}
Document::Document(std::string_view program_dir) : program_dir(program_dir),
                                                   hyphenation(program_dir) {}

Page::~Page() {}
Font::~Font() {}
Image::~Image() {}
Document::~Document() {}

