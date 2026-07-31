
#ifndef _TOWARD_AIGER_H
#define _TOWARD_AIGER_H

#include <optional>
#include <string_view>
#include <string>

#include "prop.h"

// Generates the ASCII AIGER format.
std::string ToAIGER(const Prop &prop);

// Parses the ASCII AIGER format.
std::optional<Prop> FromAIGER(std::string_view content);

#endif


