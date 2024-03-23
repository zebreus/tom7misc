#include "document.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <functional>
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
#include "bignum/big.h"
#include "ansi.h"
#include "hyphenation.h"
#include "image.h"
#include "util.h"
#include "bytecode.h"
#include "utf.h"
#include "functional-set.h"
#include "boxes-and-glue.h"
#include "base/stringprintf.h"
#include "base/logging.h"

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
  // printf("Added child (now %d)\n", (int)children.size());
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
  if (doc.IsText()) return StringPrintf(AWHITE("%s"), doc.text.c_str());
  else return ATAG("<layout>");
}

std::string AttrValString(const AttrVal &val) {
  if (const std::string *s = std::get_if<std::string>(&val.v)) {
    return StringPrintf("\"%s\"", s->c_str());
  } else if (const uint64_t *u = std::get_if<uint64_t>(&val.v)) {
    return StringPrintf("%llu", *u);
  } else if (const double *d = std::get_if<double>(&val.v)) {
    return StringPrintf("%.17g", *d);
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
  bc::Value *v = new bc::Value{.v = std::forward<Args>(args)...};
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
      return StringPrintf("%c%s",
                          ObjectFieldTypeTag(oft), field.c_str());
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
        printf("%s" ABGCOLOR(30, 30, 30, AFGCOLOR(255, 255, 255, "%s")) "\n",
               Pad(depth).c_str(),
               t.c_str());
      } else {
        // Then it is a regular node.
        if (!doc.text.empty()) {
          printf(ARED("INVALID TEXT \"") "%s" ARED("\""),
                 doc.text.c_str());
        }

        printf("%s" ATAG("<"), Pad(depth).c_str());
        bool first = true;
        for (const auto &[k, v] : doc.attrs) {
          if (!first) printf(", ");
          printf(AATTRNAME("%s") " = " AATTRVAL("%s"),
                 k.c_str(), AttrValString(v).c_str());
          first = false;
        }
        printf(ATAG(">") "\n");
        for (const auto &child : doc.children) {
          Rec(depth + 2, *child);
        }
        printf("%s" ATAG("</>") "\n", Pad(depth).c_str());
      }
    };
  Rec(0, doc);
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
        printf(AGREY("[%s]") "\n", hypart.c_str());
      }
      auto codepoints = UTF8Codepoints(hypart);
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
          printf("[%c] -> [%c] %s%s\n",
                 prev, codepoint,
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

          chunk = Util::EncodeUTF8(codepoint);
          chunk_width = char_width;
          if (break_for_kern) {
            CHECK(kern.has_value());
            // Typically negative.
            chunk_width += kern.value();
          }

          if (VERBOSE) {
            printf("Kerned '%c'+'%c'; chunk now '%s'\n", prev, codepoint,
                   chunk.c_str());
          }
        } else {
          // Extend the chunk.
          chunk += Util::EncodeUTF8(codepoint);
          chunk_width += char_width;
          if (VERBOSE) {
            printf("Added '%c'; chunk now '%s'\n", codepoint, chunk.c_str());
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
    printf(ABGCOLOR(255, 0, 0, AFGCOLOR(255, 255, 255, " ==== Boxified ==== "))
           "\n");

    for (const DocTree &x : out) {
      DebugPrintDocTree(x);
    }

    printf(ABGCOLOR(255, 0, 0, AFGCOLOR(255, 255, 255, " ================== "))
           "\n");
  }

  return out;
}

std::string Document::LoadFontFile(const std::string &filename) {
  LOG(FATAL) << "(LoadFontFile) The abstract base class of Document does "
    "not understand fonts on its own!";
}

void Document::SetDocumentInfoStrings(
    const std::unordered_map<std::string, std::string> &info) {
  // Base class does nothing with this.
}

void Document::SetDocumentInfo(
    const std::unordered_map<std::string, AttrVal> &info) {
  std::unordered_map<std::string, std::string> m;
  for (const auto &[k, v] : info) {
    if (const std::string *s = std::get_if<std::string>(&v.v)) {
      m[k] = *s;
    }
  }
  SetDocumentInfoStrings(m);
}


std::string Document::LoadImageFile(const std::string &filename) {
  std::unique_ptr<ImageRGBA> img(ImageRGBA::Load(filename));
  if (img.get() == nullptr)
    return "";

  std::string key = StringPrintf("img%d", image_counter);
  image_counter++;
  CHECK(!images.contains(key));
  images[key] = std::move(img);
  return key;
}

const Font *Document::GetFontByName(const std::string &font_name) {
  const auto it = fonts.find(font_name);
  CHECK(it != fonts.end()) << "Unknown font name " << font_name;
  CHECK(it->second.get() != nullptr) << "Null font pointer??";
  return it->second.get();
}

const ImageRGBA *Document::GetImageByName(const std::string &name) {
  const auto it = images.find(name);
  CHECK(it != images.end() && it->second.get() != nullptr) << "Unknown "
    "image handle " << name;
  return it->second.get();
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

const Font *Document::GetDescribedFont(const FontDescription &desc) {
  const auto it = font_families.find(desc.font_family);
  if (it == font_families.end()) {
    LOG(FATAL) << "Unknown font family: " << desc.font_family;
  }
  const FontFamily &family = it->second;

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
          if (*display == "box") {
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
              auto co = bc->ToInt();
              CHECK(co.has_value() && co.value() >= 0 &&
                    co.value() <= int64_t{0xFFFFFFFF}) << "Color is out "
                "of range. Must be in [0, 0xFFFFFFFF]: " << bc->ToString();
              props.font_color = (uint32_t)co.value();
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

  // Get default text props somehow?
  TextProps props;
  props.desc.font_family = "times";
  Rec(props, doc);
  return JoinDocs(out);
}

std::pair<DocTree, double>
Document::PackBoxes(double line_width, const DocTree &doc) {
  static constexpr bool VERBOSE = false;
  using BoxIn = BoxesAndGlue::BoxIn;
  using BoxOut = BoxesAndGlue::BoxOut;

  if (doc.IsEmpty()) return {doc, 0.0};
  CHECK(!doc.IsText()) <<
    "pack-boxes wants a node that has only box children. Got text: " <<
    doc.text;

  enum class Algorithm {
    BEST,
    FIRST,
  };
  Algorithm algorithm = Algorithm::BEST;
  if (const std::string *algo = doc.GetStringAttr("algorithm")) {
    if (*algo == "best") {
      algorithm = Algorithm::BEST;
    } else if (*algo == "first") {
      algorithm = Algorithm::FIRST;
    } else {
      LOG(FATAL) << "pack-boxes algorithm attr unknown: " << *algo;
    }
  }

  std::vector<BoxIn> children;

  auto GetBox = [](const DocTree &doc) -> BoxIn {
      const double *width = doc.GetDoubleAttr("width");
      CHECK(width != nullptr) << "In pack-boxes, encountered a top-level box "
        "that has no width. This probably means that you didn't do get-boxes "
        "or you messed up the boxes after that, or there's a bug in "
        "get-boxes (could have been anyone?)";

      BoxIn b;
      b.width = *width;
      b.data = (void*)&doc;
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


  std::vector<std::vector<BoxesAndGlue::BoxOut>> lines;
  switch (algorithm) {
  case Algorithm::FIRST: {
    // Allows hyphens.
    static constexpr double MAX_BREAK_PENALTY = 200.0;
    lines = BoxesAndGlue::PackBoxesFirst(
        line_width, boxes, MAX_BREAK_PENALTY);
    break;
  }
  case Algorithm::BEST:
    lines = BoxesAndGlue::PackBoxes(line_width, boxes);
    break;
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
      doc->RemoveAttr("glue-contract");
      doc->RemoveAttr("glue-break-penalty");
      doc->RemoveAttr("glue-break-insert");
      doc->RemoveAttr("glue-ideal");
    };

  std::vector<DocTree> lines_out;
  for (std::vector<BoxesAndGlue::BoxOut> &box_line : lines) {
    DocTree line;
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
            printf(ABGCOLOR(255, 255, 0, "HYPHEN:") "\n");
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

  return {JoinDocs(lines_out), total_badness};
}
