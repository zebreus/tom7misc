
#ifndef _CC_LIB_CHARDATA_H
#define _CC_LIB_CHARDATA_H

#include <string>
#include <string_view>

// XXX merge with fix-encoding?

std::string RestoreByteA0(std::string_view text);
std::string ReplaceLossySequences(std::string_view str);
std::string RemoveBOM(std::string_view text);
std::string FixSurrogates(std::string_view text);
std::string FixLineBreaks(std::string_view text);
std::string FixLatinLigatures(std::string_view text);
std::string UncurlQuotes(std::string_view text);
std::string RemoveTerminalEscapes(std::string_view text);
std::string DecodeInconsistentUTF8(std::string_view text);
std::string RemoveControlChars(std::string_view text);

#endif
