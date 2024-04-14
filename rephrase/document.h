
#ifndef _REPHRASE_DOCUMENT_H
#define _REPHRASE_DOCUMENT_H

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>
#include <map>

#include "bignum/big.h"
#include "bytecode.h"
#include "hyphenation.h"
#include "image.h"
#include "boxes-and-glue.h"

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
  const BigInt *GetIntAttr(const std::string &) const;
  const DocTree *GetLayoutAttr(const std::string &) const;

  // Simply overwriting the attribute if already present.
  void SetStringAttr(const std::string &name, const std::string &value);
  void SetIntAttr(const std::string &name, const BigInt &value);
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
std::string DocText(const DocTree &doc);

DocTree JoinDocs(std::vector<DocTree> v);
DocTree TextDoc(std::string s);

// This is the document context as we're rendering.
// I tried to keep this somewhat separated from PDF itself, so that I
// can make other backends (e.g. a PNG backend for slide images). But
// this is basically PDF::Font.
struct Font {
  Font();
  virtual ~Font();
  virtual std::string Name() const;

  virtual std::optional<double>
  GetKerning(int codepoint1, int codepoint2) const;

  // Get the width of the codepoint when the font is at 1pt. You can
  // multiply by the font size to get the width at that size.
  virtual double CharWidth(int codepoint) const;
};

struct Page {
  Page();
  virtual ~Page();
  Page(double width, double height) : page_width(width), page_height(height) {}

  double Height() const { return page_height; }
  double Width() const { return page_width; }

  virtual void DrawText(const Font *font,
                        const std::string &text, double size,
                        double x, double y,
                        uint32_t color);

  virtual void DrawImage(double x, double y,
                         double width, double height,
                         const ImageRGBA &image);

  virtual void DrawRect(double x, double y,
                        double width, double height,
                        double border_width, uint32_t color_fill,
                        uint32_t color_border);

  virtual void DrawVideo(double x, double y,
                         double width, double height,
                         const std::string &src,
                         bool loop);

  // TODO: Line drawing commands, etc.

 protected:
  double page_width = 0.0;
  double page_height = 0.0;
};

struct Document {
  Document();
  virtual ~Document();

  static double PointToPixel(double pt) { return pt; }
  static double PixelToPoint(double px) { return px; }

  // Describing one of the variants of a font family.
  struct FontDescription {
    std::string font_family;
    bool font_bold = false;
    bool font_italic = false;
  };

  static std::string FontDescriptionString(const FontDescription &fd);
  // static std::string TextPropsString(const TextProps &p);

  struct TextProps {
    FontDescription desc;
    double font_size = 12.0;
    double line_spacing = 0.0;
    uint32_t font_color = 0x000000FF;
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

  // Set document metadata (title, creation time, etc.).
  // The fields accepted depend on the format.
  virtual void SetDocumentInfoStrings(
      const std::unordered_map<std::string, std::string> &info);

  // TODO: Perhaps this is what should be overridden, but we
  // only use string fields in PDF.
  void SetDocumentInfo(
      const std::unordered_map<std::string, AttrVal> &attrs);

  virtual void SetPageInfo(
      int page_idx, int frame_idx,
      const std::unordered_map<std::string, AttrVal> &attrs);

  // Loads an image in a format that's supported by ImageRGBA
  // (PNG, JPEG are best). Inserts it in the map and returns a
  // unique handle to it. If the file can't be loaded, returns
  // the empty string.
  std::string LoadImageFile(const std::string &filename);

  // Look up a font by its name (Font::Name; not family name).
  const Font *GetFontByName(const std::string &font_name);
  virtual const Font *GetDefaultFont();

  // Look up an image by its handle.
  const ImageRGBA *GetImageByName(const std::string &name);

  // These are independent of font size.
  void RegisterFont(const FontDescription &desc, const Font *f);
  const Font *GetDescribedFont(const FontDescription &desc);

  uint32_t IntToColor(const char *what, const BigInt &b);

  DocTree GetBoxes(const DocTree &doc);

  // Pack boxes to lines. Returns the document and the total badness.
  enum class Algorithm {
    BEST,
    FIRST,
  };
  std::pair<DocTree, double> PackBoxes(Algorithm algo,
                                       BoxesAndGlue::Justification just,
                                       double orphan_threshold,
                                       double width,
                                       const DocTree &doc);

  std::vector<DocTree>
  BoxifyText(const TextProps &props, std::string_view text);

  std::unordered_map<std::string, FontFamily> font_families;

  // All loaded fonts.
  std::unordered_map<std::string, std::unique_ptr<Font>> fonts;
  Hyphenation hyphenation;

  virtual void GenerateOutput(
      std::string_view filename_base,
      const std::map<int, std::map<int, DocTree>> &pages);

  struct Transform {
    double dx = 0.0, dy = 0.0;
    double sx = 1.0, sy = 1.0;
  };

  struct Context {
    const Font *font = nullptr;
    double font_size = 12.0;
    uint32_t color = 0x000000FF;
  };

 protected:
  void PlaceStickersRec(Context context,
                        Transform transform,
                        const DocTree &doc,
                        Page *page);

 private:
  // All loaded images.
  int image_counter = 0;
  std::unordered_map<std::string, std::unique_ptr<ImageRGBA>> images;

  DocTree PackBoxesOld(double width, const DocTree &doc);
};

#endif
