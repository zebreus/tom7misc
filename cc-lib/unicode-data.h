
// This is simple wrapper for parsing UnicodeData.txt.
// For encoding and decoding UTF-8, see utf8.h.

#ifndef _CC_LIB_UNICODE_DATA_H
#define _CC_LIB_UNICODE_DATA_H

#include <memory>
#include <optional>
#include <span>
#include <string_view>

struct UnicodeData {
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
  };

  // Using only the official name, like
  // "PRESENTATION FORM FOR VERTICAL RIGHT WHITE LENTICULAR BRAKCET".
  // Note that some codepoints don't have good names and so they can't really
  // be looked up this way. Like, there are a bunch named "<control>".
  virtual std::optional<CodepointData> GetByName(
      std::string_view name) const = 0;
  virtual std::optional<CodepointData> GetByCodepoint(
      uint32_t codepoint) const = 0;

 protected:
  // Use factory.
  UnicodeData() {}
};

#endif
