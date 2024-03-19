
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
#include "hyphenation.h"

struct DocTree;

struct AttrVal {
  using t = std::variant<
    BigInt,
    std::string,
    uint64_t,
    bool,
    double,
    std::shared_ptr<DocTree>
    >;
  t v;
};

struct DocTree {
  std::unordered_map<std::string, AttrVal> attrs;
  std::string text;
  std::vector<std::shared_ptr<DocTree>> children;

  // No attributes or children means it's a text node. Could be empty.
  bool IsText() const;
  // An empty node is a text node whose text is blank.
  bool IsEmpty() const;
  // A group is a real node that has no attributes, but has children.
  bool IsGroup() const;

  // Not allowed on text nodes.
  // Return a pointer to the named attribute, or nullptr if not present.
  const AttrVal *GetAttr(const std::string &) const;
  // Return a pointer to the named attribute if it is the right type.
  // If it is the wrong type, abort with an error message.
  // If it is not present, return nullptr.
  const std::string *GetStringAttr(const std::string &) const;
  const double *GetDoubleAttr(const std::string &) const;
  const bool *GetBoolAttr(const std::string &) const;
  const DocTree *GetLayoutAttr(const std::string &) const;

  // Simply overwriting the attribute if already present.
  void SetStringAttr(const std::string &name, const std::string &value);
  void SetDoubleAttr(const std::string &name, double d);
  void SetLayoutAttr(const std::string &name, DocTree t);

  void RemoveAttr(const std::string &name);

  void ClearChildren();
  void AddChild(DocTree doc);
};

std::string AttrValString(const AttrVal &val);
// Convert the field name (stripping type tags) and value.
std::pair<std::string, AttrVal>
ValueToAttrVal(const std::string &field, const bc::Value &val);
std::pair<std::string, bc::Value *>
AttrValToValue(std::vector<bc::Value *> *heap,
               const std::string &field, const AttrVal &val);

// Copies the value, converting it to DocTree format.
DocTree ValueToDocTree(const bc::Value *v);
// Copies the DocTree back into the execution heap, and returns a
// pointer to it.
bc::Value *DocTreeToValue(std::vector<bc::Value *> *heap, const DocTree &doc);

// Like Util::NormalizeWhitespace, but don't remove surrounding whitespace.
// (We don't want a node with just " " to become empty!)
// We need to figure out something more rational here.
std::string NormalizeWhitespace(const std::string &s);

void DebugPrintDocTree(const DocTree &doc);

DocTree JoinDocs(std::vector<DocTree> v);

// This is the document context as we're rendering.
// I tried to keep this somewhat separated from PDF itself, so that I
// can make other backends (e.g. a PNG backend for slide images). But
// this is basically PDF::Font.
struct Font {
  virtual ~Font() = default;
  virtual std::string Name() const;

  virtual std::optional<double>
  GetKerning(int codepoint1, int codepoint2) const;

  // Get the width of the codepoint when the font is at 1pt. You can
  // multiply by the font size to get the width at that size.
  virtual double CharWidth(int codepoint) const;

  // The width of the string at 1pt.
  virtual double GetKernedWidth(const std::string &text) const;
};

struct Document {
  virtual ~Document() = default;

  struct TextProps {
    std::string font_family;
    double font_size = 12.0;
    bool font_bold = false;
    bool font_italic = false;
  };

  // Each style is optional, but there should be at least one non-null.
  struct FontFamily {
    const Font *regular = nullptr;
    const Font *bold = nullptr;
    const Font *italic = nullptr;
    const Font *bold_italic = nullptr;
  };

  // Load a font from a file and insert it in the fonts map.
  // Returns a font identifier if successful, or else the empty
  // string. This just just a single face, not a font family.
  virtual std::string LoadFontFile(const std::string &filename);

  // Look up a font by its name (Font::Name; not family name).
  const Font *GetFontByName(const std::string &font_name);

  // These are independent of font size.
  void RegisterFont(const TextProps &props, const Font *f);
  const Font *GetDescribedFont(const TextProps &props);

  DocTree GetBoxes(const DocTree &doc);

  // Pack boxes to lines.
  DocTree PackBoxes(double width, const DocTree &doc);

  std::vector<DocTree>
  BoxifyText(const Font *font, double font_size, std::string_view text);

  std::unordered_map<std::string, FontFamily> font_families;

  // All loaded fonts.
  std::unordered_map<std::string, std::unique_ptr<Font>> fonts;
  Hyphenation hyphenation;

 private:

  DocTree PackBoxesOld(double width, const DocTree &doc);
};

#endif
