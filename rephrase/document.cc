#include "document.h"

#include <functional>
#include <string>
#include <unordered_set>

#include "base/stringprintf.h"
#include "base/logging.h"
#include "ansi.h"
#include "util.h"

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

Document ValueToDoc(const bc::Value *v) {
  std::unordered_set<const bc::Value *> seen;
  std::function<Document(const bc::Value *)> Rec =
      [&seen, &Rec](const bc::Value *v) -> Document {
        CHECK(!seen.contains(v)) << "Cycle in document! "
          "What is this, some kind of joke!?";
        seen.insert(v);

        using map_type = std::unordered_map<std::string, bc::Value *>;
        using vec_type = std::vector<bc::Value *>;

        Document doc;
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
            doc.children.emplace_back(std::make_shared<Document>(Rec(v)));
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

void DebugPrintDoc(const Document &doc) {
  std::function<void(int, const Document &)> Rec =
    [&Rec](int depth, const Document &doc) {
      if (doc.attrs.empty() && doc.children.empty()) {

        // We should be careful about normalizing whitespace here,
        // since it sometimes has meaning.
        const std::string t = Util::NormalizeWhitespace(doc.text);
        // If it has no attrs or children, it is a text node (even if
        // text is also empty).
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
