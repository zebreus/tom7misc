
#ifndef _REPHRASE_DOCUMENT_H
#define _REPHRASE_DOCUMENT_H

#include <string>
#include <variant>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

#include "bignum/big.h"
#include "bytecode.h"

struct AttrVal {
  using t = std::variant<
    BigInt,
    std::string,
    uint64_t,
    double
    >;
  t v;
};

struct DocTree {
  std::unordered_map<std::string, AttrVal> attrs;
  std::string text;
  std::vector<std::shared_ptr<DocTree>> children;

  // Not allowed on text nodes.
  // Return a pointer to the named attribute, or nullptr if not present.
  const AttrVal *GetAttr(const std::string &) const;
  // Return a pointer to the named attribute if it is the right type.
  // If it is the wrong type, abort with an error message.
  // If it is not present, return nullptr.
  const std::string *GetStringAttr(const std::string &) const;
  const double *GetDoubleAttr(const std::string &) const;
  const bool *GetBoolAttr(const std::string &) const;

  // Simply overwriting the attribute if already present.
  void SetStringAttr(const std::string &name, const std::string &value);
  void SetDoubleAttr(const std::string &name, double d);

  void AddChild(DocTree doc);
};

std::string AttrValString(const AttrVal &val);
AttrVal ConvertAttrVal(const std::string &field, const bc::Value &val);

// Copies the value, converting it to DocTree format.
DocTree ValueToDocTree(const bc::Value *v);
// Copies the DocTree back into the execution heap, and returns a
// pointer to it.
bc::Value *DocTreeToValue(std::vector<bc::Value *> *heap, const DocTree &doc);

void DebugPrintDocTree(const DocTree &doc);

DocTree GetBoxes(const DocTree &doc);

bool IsText(const DocTree &doc);

DocTree JoinDocs(std::vector<DocTree> v);

// This is the document context as we're rendering.
// I tried to keep this somewhat separated from PDF itself, so that I
// can make other backends (e.g. a PNG backend for slide images). But
// this is basically PDF::Font.
struct Font {
  virtual ~Font() = default;
  virtual std::string Name() const;

  virtual std::optional<double> GetKerning(int codepoint1, int codepoint2) const;

  // Get the width of the codepoint when the font is at 1pt. You can
  // multiply by the font size to get the width at that size.
  virtual double CharWidth(int codepoint) const;

  // The width of the string at 1pt.
  virtual double GetKernedWidth(const std::string &text) const;
};

struct Document {
  virtual ~Document() = default;

  // All loaded fonts.
  std::unordered_map<std::string, std::unique_ptr<Font>> fonts;

  struct TextProps {
    std::string font_face;
    double font_size = 12.0;
    bool font_bold = false;
    bool font_italic = false;
  };

  // This is independent of font size.
  virtual const Font *GetDescribedFont(const TextProps &props);

  DocTree GetBoxes(const DocTree &doc);
};

#endif
