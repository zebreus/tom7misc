#ifndef _CC_LIB_TEXT_CODEC_H
#define _CC_LIB_TEXT_CODEC_H

#include <optional>
#include <string>
#include <string_view>

// In python, an encoder takes a sequence of codepoints and returns
// a sequence of bytes.
// In this C++ port, we represent both of these as std::string. A
// sequence of codepoints is expected to be UTF-8 encoded. A sequence
// of bytes is a string containing anything.
struct TextCodec {
  virtual std::optional<std::string> Encode(std::string_view str) const = 0;
  virtual std::optional<std::string> EncodeSloppy(std::string_view str) const = 0;
  virtual std::optional<std::string> Decode(std::string_view bytes) const = 0;
  virtual std::string DecodeSloppy(std::string_view bytes) const = 0;
};

// Singletons
const TextCodec &Latin1();
const TextCodec &Windows1252();
const TextCodec &Windows1251();
const TextCodec &Windows1250();
const TextCodec &Windows1253();
const TextCodec &Windows1254();
const TextCodec &Windows1257();
const TextCodec &ISO8859_2();
const TextCodec &MacRoman();
const TextCodec &CP437();

#endif
