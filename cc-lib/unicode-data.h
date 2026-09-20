
// This is simple wrapper for parsing UnicodeData.txt.
// For encoding and decoding UTF-8, see utf8.h.

#ifndef _CC_LIB_UNICODE_DATA_H
#define _CC_LIB_UNICODE_DATA_H

#include <memory>
#include <optional>
#include <span>
#include <string_view>

struct UnicodeData {
  // These are exactly from the unicode spec.
  enum GeneralCategory : uint8_t {
    Lu,  // Uppercase_Letter: an uppercase letter
    Ll,  // Lowercase_Letter: a lowercase letter
    Lt,  // Titlecase_Letter: single character digraph with first part uppercase
    Lm,  // Modifier_Letter: a modifier letter
    Lo,  // Other_Letter: other letters, including syllables and ideographs
    Mn,  // Nonspacing_Mark: a nonspacing combining mark (zero advance width)
    Mc,  // Spacing_Mark: a spacing combining mark (positive advance width)
    Me,  // Enclosing_Mark: an enclosing combining mark
    Nd,  // Decimal_Number: a decimal digit
    Nl,  // Letter_Number: a letterlike numeric character
    No,  // Other_Number: a numeric character of other type
    Pc,  // Connector_Punctuation: a connecting punctuation mark, like a tie
    Pd,  // Dash_Punctuation: a dash or hyphen punctuation mark
    Ps,  // Open_Punctuation: an opening punctuation mark (of a pair)
    Pe,  // Close_Punctuation: a closing punctuation mark (of a pair)
    Pi,  // Initial_Punctuation: an initial quotation mark
    Pf,  // Final_Punctuation: a final quotation mark
    Po,  // Other_Punctuation: a punctuation mark of other type
    Sm,  // Math_Symbol: a symbol of mathematical use
    Sc,  // Currency_Symbol: a currency sign
    Sk,  // Modifier_Symbol: a non-letterlike modifier symbol
    So,  // Other_Symbol: a symbol of other type
    Zs,  // Space_Separator: a space character (of various non-zero widths)
    Zl,  // Line_Separator: U+2028 LINE SEPARATOR only
    Zp,  // Paragraph_Separator: U+2029 PARAGRAPH SEPARATOR only
    Cc,  // Control: a C0 or C1 control code
    Cf,  // Format: a format control character
    Cs,  // Surrogate: a surrogate code point
    Co,  // Private_Use: a private-use character
    Cn,  // Unassigned: a reserved unassigned code point or a noncharacter
  };

  virtual ~UnicodeData() {}
  // e.g. FromContent(Util::ReadFile("UnicodeData.txt"));
  // or   FromContent(ZIP::UnCCZ(Util::ReadFile("unicode-data.ccz")));
  static std::unique_ptr<UnicodeData> FromContent(std::string_view contents);
  static std::unique_ptr<UnicodeData> FromContent(
      std::span<const uint8_t> contents);

  // More data may be added in the future.
  struct CodepointData {
    uint32_t codepoint = 0;
    std::string_view name;
    GeneralCategory category;
  };

  // Using only the official name, like
  // "PRESENTATION FORM FOR VERTICAL RIGHT WHITE LENTICULAR BRAKCET".
  // Note that some codepoints don't have good names and so they can't really
  // be looked up this way. Like, there are a bunch named "<control>".
  virtual std::optional<CodepointData> GetByName(
      std::string_view name) const = 0;
  virtual std::optional<CodepointData> GetByCodepoint(
      uint32_t codepoint) const = 0;

  // Predicates from the spec; the union of multiple categories.
  static bool IsLetter(GeneralCategory c);
  static bool IsCasedLetter(GeneralCategory c);
  static bool IsMark(GeneralCategory c);
  static bool IsNumber(GeneralCategory c);
  static bool IsPunctuation(GeneralCategory c);
  static bool IsSymbol(GeneralCategory c);
  static bool IsOther(GeneralCategory c);
  static bool IsSeparator(GeneralCategory c);

 protected:
  // Use factory.
  UnicodeData() {}
};

#endif
