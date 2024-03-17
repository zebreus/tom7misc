#include "document.h"

#include <functional>
#include <string>

#include "base/stringprintf.h"
#include "base/logging.h"
#include "ansi.h"
#include "util.h"
#include "bytecode.h"
#include "utf.h"
#include "functional-set.h"

// Could use actual infinity.
static constexpr double INFINITE_PENALTY = 9999999.0;

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

void DocTree::SetDoubleAttr(const std::string &name, double d) {
  attrs[name] = AttrVal{.v = {d}};
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


std::string AttrValString(const AttrVal &val) {
  if (const std::string *s = std::get_if<std::string>(&val.v)) {
    return StringPrintf("\"%s\"", s->c_str());
  } else if (const uint64_t *u = std::get_if<uint64_t>(&val.v)) {
    return StringPrintf("%llu", *u);
  } else if (const double *d = std::get_if<double>(&val.v)) {
    return StringPrintf("%.17g", *d);
  } else if (const BigInt *b = std::get_if<BigInt>(&val.v)) {
    return b->ToString();
  } else {
    return "??INVALID??";
  }
}

std::pair<std::string, AttrVal>
ValueToAttrVal(const std::string &field, const bc::Value &val) {
  auto CheckStripTag = [&field](bc::ObjectFieldType oft) {
      char c = ObjectFieldTypeTag(oft);
      CHECK(!field.empty() && field[0] == c) << "Bug: Field tag did not match "
        "type in object? field: " << field;
      return field.substr(1, std::string::npos);
    };

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
      CHECK(!seen.Contains(v)) << "Cycle in document! "
        "What is this, some kind of joke!?";
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

#define ATAG(s) AFGCOLOR(160, 160, 200, s)
#define AATTRNAME(s) AFGCOLOR(200, 200, 160, s)
#define AATTRVAL(s) AFGCOLOR(160, 200, 160, s)

void DebugPrintDocTree(const DocTree &doc) {
  std::function<void(int, const DocTree &)> Rec =
    [&Rec](int depth, const DocTree &doc) {
      if (doc.IsText()) {
        // We should be careful about normalizing whitespace here,
        // since it sometimes has meaning.
        const std::string t = Util::NormalizeWhitespace(doc.text);
        printf("%s" AWHITE("%s") "\n",
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

static std::string_view NextWord(std::string_view &text) {
  auto pos = text.find(' ');
  if (pos == std::string_view::npos) {
    std::string_view ret = text;
    text.remove_prefix(ret.size());
    return ret;
  }

  std::string_view ret = text.substr(0, pos);
  text.remove_prefix(pos);
  while (!text.empty() && text[0] == ' ') text.remove_prefix(1);
  return ret;
}

DocTree TextDoc(const std::string &s) {
  DocTree d;
  d.text = s;
  return d;
}

static std::vector<DocTree>
BoxifyText(const Font *font, double font_size, std::string_view text) {
  static constexpr bool VERBOSE = false;
  std::vector<DocTree> out;

  const double space_width = font->CharWidth(' ');

  // Work a word at a time.
  while (!text.empty()) {
    std::string_view word = NextWord(text);
    if (word.empty()) continue;

    // TODO: Here's where we would implement hyphenation.

    // Then to turn the word into boxes, read successive codepoints
    // from it.
    uint32_t prev = ' ';
    std::string chunk;
    double chunk_width = 0.0;
    for (const uint32_t codepoint : UTF8Codepoints(word)) {
      double char_width = font->CharWidth(codepoint);
      std::optional<double> kern = font->GetKerning(prev, codepoint);
      if (kern.has_value()) {
        // If there is a kerning pair, make an unbreakable box to
        // contain the chunk so far.
        DocTree d;
        d.SetStringAttr("display", "box");
        d.SetDoubleAttr("width", chunk_width * font_size);
        d.SetDoubleAttr("height", font_size);
        // PERF The font is normalized onto every chunk, which may be kinda
        // expensive.
        d.SetDoubleAttr("font-size", font_size);
        d.SetStringAttr("font-name", font->Name());

        // Since this is inside a word, we disable breaks here by setting
        // the break penalty "infinite".
        d.SetDoubleAttr("glue-break-penalty", INFINITE_PENALTY);

        d.AddChild(TextDoc(chunk));
        out.push_back(std::move(d));

        chunk = Util::EncodeUTF8(codepoint);
        chunk_width = char_width;

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

    CHECK(!chunk.empty()) << "We only break on kerning pairs, so the chunk "
      "is never empty for non-empty words! Word: [" << word << "]";

    // The final chunk, which is frequently the entire word, allows a break
    // after it.
    DocTree d;
    d.SetStringAttr("display", "box");
    d.SetDoubleAttr("width", chunk_width * font_size);
    d.SetDoubleAttr("height", font_size);
    d.SetDoubleAttr("font-size", font_size);
    d.SetStringAttr("font-name", font->Name());

    // No penalty to break between words. We could use some heuristics to
    // penalize certain breaks in the future. (For example, breaking after
    // a comma or semicolon seems better.)
    d.SetDoubleAttr("glue-break-penalty", 0.0);
    d.SetDoubleAttr("glue-ideal", space_width * font_size);
    // Relative penalty for contracting.
    d.SetDoubleAttr("glue-contract", 4.0);

    d.AddChild(TextDoc(chunk));
    out.push_back(std::move(d));
  }

  return out;
}

const Font *Document::GetDescribedFont(const TextProps &props) {
  LOG(FATAL) << "The abstract base class of Document does not understand "
    "fonts on its own!";
}

// Like Util::NormalizeWhitespace, but don't remove surrounding whitespace.
// (We don't want a node with just " " to become empty!)
// We need to figure out something more rational here.
static std::string NormalizeWhitespace(const std::string &s) {
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

        const Font *font = GetDescribedFont(props);

        std::string normtext = NormalizeWhitespace(doc.text);

        std::vector<DocTree> boxes =
          BoxifyText(font, props.font_size, doc.text);
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
              props.font_face = *f;
            }

            if (const double *d = doc.GetDoubleAttr("font-size")) {
              props.font_size = *d;
            }

            if (const bool *b = doc.GetBoolAttr("font-bold")) {
              props.font_bold = *b;
            }

            if (const bool *b = doc.GetBoolAttr("font-italic")) {
              props.font_italic = *b;
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
  props.font_face = "times";
  Rec(props, doc);
  return JoinDocs(out);
}

namespace {
struct Box {
  // Request from input.
  double width = 0.0;
  // From text, or from box's native height.
  double height = 0.0;
  double glue_ideal = 0.0;
  // Derived from penalty. Later, we should actually use softer
  // penalties.
  bool cannot_break = false;

  // Set during layout.
  double pad_right = 0.0;

  // TODO: other metrics copied from attributes, like glue!
  const DocTree *node = nullptr;
};
}

DocTree Document::PackBoxes(double width, const DocTree &doc) {
  static constexpr bool VERBOSE = false;
  if (doc.IsEmpty()) return doc;
  CHECK(!doc.IsText()) <<
    "PackBoxes wants a node that has only box children. Got text: " <<
    doc.text;

  std::vector<DocTree> out;

  auto GetBox = [](const DocTree &doc) -> Box {
      const double *width = doc.GetDoubleAttr("width");
      CHECK(width != nullptr) << "In PackBoxes, encountered a top-level box "
        "that has no width. This probably means that you didn't do GetBoxes "
        "or you messed up the boxes after that, or there's a bug in "
        "GetBoxes (could have been anyone?)";

      Box b;
      b.width = *width;
      if (const double *height = doc.GetDoubleAttr("height")) {
        b.height = *height;
      }
      if (const double *glue = doc.GetDoubleAttr("glue-ideal")) {
        b.glue_ideal = *glue;
      }
      if (const double *pen = doc.GetDoubleAttr("glue-break-penalty")) {
        if (*pen > 0.0) b.cannot_break = true;
      }
      b.node = &doc;
      return b;
    };

  std::vector<Box> boxes = [&]() {
      std::vector<Box> boxes;
      if (const std::string *display = doc.GetStringAttr("display")) {
        if (*display == "box") {
          // Just one box.
          boxes.push_back(GetBox(doc));
          return boxes;
        }
      }

      // Then we expect the direct children (if any) to be the boxes.
      // XXX perhaps we should be warning if there are attributes
      // here, as they will be dropped?
      for (const auto &child : doc.children) {
        const std::string *display = child->GetStringAttr("display");
        CHECK(display != nullptr) << "In PackBoxes, expected a series "
          "of boxes. Probably need to call GetBoxes first?";
        boxes.push_back(GetBox(*child));
      }
      return boxes;
    }();

  // Now pack.
  // Simple first-fit algorithm so we can test the end-to-end.
  std::vector<Box> current_line;
  double current_line_max_height = 0.0;
  auto EmitLine = [width, &out, &current_line, &current_line_max_height]() {
      // out.push_back(TextDoc("EmitLine"));
      if (current_line.empty()) return;

      DocTree line;
      line.SetStringAttr("display", "box");
      // Or the actual width?
      line.SetDoubleAttr("width", width);
      line.SetDoubleAttr("height", current_line_max_height);
      for (const Box &box : current_line) {
        // Same box, but extend the width to consume the actually used
        // glue.
        DocTree d = *box.node;
        d.SetDoubleAttr("width", box.width + box.pad_right);
        d.SetDoubleAttr("height", box.height);
        d.RemoveAttr("glue-contract");
        d.RemoveAttr("glue-break-penalty");
        d.RemoveAttr("glue-ideal");
        line.AddChild(d);
      }
      out.push_back(std::move(line));
      current_line.clear();
      current_line_max_height = 0.0;
    };

  double current_width = 0.0;
  double current_postwidth = 0.0;
  bool cannot_break = false;
  for (const Box &box : boxes) {

    if (VERBOSE) {
      printf("line: %d boxes, width %.3g. %s"
             "post %.3g. this box %.3g, target %.3g\n",
             (int)current_line.size(), current_width,
             cannot_break ? ARED("NOBRK") " " : "",
             current_postwidth,
             box.width, width);
    }

    if (cannot_break ||
        current_width + current_postwidth + box.width <= width) {
      // Take the box.

      // This means the previous box gets is glue turned into padding.
      if (!current_line.empty()) {
        current_line.back().pad_right = current_postwidth;
        current_width += current_postwidth;
      } else {
        CHECK(current_postwidth == 0.0);
      }

      current_line.push_back(box);
      current_line_max_height =
        std::max(current_line_max_height, box.height);
      current_width += box.width;
      cannot_break = box.cannot_break;
      current_postwidth = box.glue_ideal;

    } else if (current_line.empty()) {
      LOG(FATAL) << "This case is not handled yet. I think we just don't "
        "add the empty line?";

    } else {
      // Break.
      EmitLine();

      current_line = {box};
      current_width = box.width;
      current_postwidth = box.glue_ideal;
      current_line_max_height = box.height;
      cannot_break = box.cannot_break;
    }
  }

  // The line usually still has something on it.
  EmitLine();

  // out.push_back(TextDoc("hi :)"));

  return JoinDocs(out);
}
