#include "document.h"

#include <functional>
#include <string>
#include <unordered_set>

#include "base/stringprintf.h"
#include "base/logging.h"
#include "ansi.h"
#include "util.h"
#include "bytecode.h"

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

static bool TRUE = true;
static bool FALSE = false;
const bool *DocTree::GetBoolAttr(const std::string &name) const {
  if (const AttrVal *a = GetAttr(name)) {
    if (const uint64_t *u = std::get_if<uint64_t>(&a->v)) {
      // The attrval doesn't actually store a bool, so point to some
      // singletons.
      return *u ? &TRUE : &FALSE;
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

AttrVal ConvertAttrVal(const std::string &field, const bc::Value &val) {
  if (const std::string *s = std::get_if<std::string>(&val.v)) {
    return AttrVal{.v = *s};
  } else if (const uint64_t *u = std::get_if<uint64_t>(&val.v)) {
    return AttrVal{.v = *u};
  } else if (const double *d = std::get_if<double>(&val.v)) {
    return AttrVal{.v = *d};
  } else if (const BigInt *b = std::get_if<BigInt>(&val.v)) {
    return AttrVal{.v = *b};
  } else {
    LOG(FATAL) << "Unsupported attribute type in layout. It must be "
      "BigInt, string, uint64_t, or double. The field was: " << field;
  }
}

DocTree ValueToDocTree(const bc::Value *v) {
  std::unordered_set<const bc::Value *> seen;
  std::function<DocTree(const bc::Value *)> Rec =
      [&seen, &Rec](const bc::Value *v) -> DocTree {
        CHECK(!seen.contains(v)) << "Cycle in document! "
          "What is this, some kind of joke!?";
        seen.insert(v);

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
            doc.attrs[k] = ConvertAttrVal(k, *v);
          }

          for (const bc::Value *v : *cvec) {
            doc.children.emplace_back(std::make_shared<DocTree>(Rec(v)));
          }

          return doc;
        } else {
          LOG(FATAL) << "Bug: Layout values should be represented as either "
            "strings or maps (objects).";
          return doc;
        }
      };
  return Rec(v);
}

static std::string Pad(int depth) {
  return std::string(depth, ' ');
}

bool IsText(const DocTree &doc) {
  // If it has no attrs or children, it is a text node (even if
  // text is also empty).
  return doc.attrs.empty() && doc.children.empty();
}

void DebugPrintDocTree(const DocTree &doc) {
  std::function<void(int, const DocTree &)> Rec =
    [&Rec](int depth, const DocTree &doc) {
      if (IsText(doc)) {
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

        printf("%s" ABLUE("<"), Pad(depth).c_str());
        bool first = true;
        for (const auto &[k, v] : doc.attrs) {
          if (!first) printf(", ");
          printf(AYELLOW("%s") " = " APURPLE("%s"),
                 k.c_str(), AttrValString(v).c_str());
          first = false;
        }
        printf(ABLUE(">") "\n");
        for (const auto &child : doc.children) {
          Rec(depth + 2, *child);
        }
        printf("%s" ABLUE("</>") "\n", Pad(depth).c_str());
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
  doc.children.resize(docs.size());
  for (DocTree &d : docs) {
    doc.children.emplace_back(std::make_shared<DocTree>(std::move(d)));
  }
  docs.clear();
  return doc;
}

  #if 0
  // Kerns
  // Each substring has additional space after it (can be negative).
  // The final space does nothing and can take any value.
  //
  // Spacing is relative: It is simply scaled by the font size.
  //
  // Spacing here is nominally positive (larger values mean more space)
  // and in point units like everything else.
  std::vector<std::pair<std::string, float>>
  KernText(const Font *font, const std::string &text) const;
#endif

static std::vector<DocTree>
BoxifyText(const Font *font, double font_size, const std::string &text) {
  LOG(FATAL) << "Unimplemented";
  return {};
}

const Font *Document::GetDescribedFont(const TextProps &props) {
  LOG(FATAL) << "The abstract base class of Document does not understand "
    "fonts on its own!";
}

// Converts layout (spans with style) into boxes that have
// definite size and glue.
DocTree Document::GetBoxes(const DocTree &doc) {
  std::vector<DocTree> out;

  std::function<void(TextProps props, const DocTree &)> Rec =
    [this, &out, &Rec](TextProps props, const DocTree &doc) {
        if (IsText(doc)) {

          const Font *font = GetDescribedFont(props);

          std::vector<DocTree> boxes =
            BoxifyText(font, props.font_size, doc.text);
          for (DocTree &d : boxes) out.push_back(std::move(d));
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

  // Get default text props somehow.
  TextProps props;
  Rec(props, doc);
  return JoinDocs(out);
}

