
#ifndef _TOWARD_VERILOG_H
#define _TOWARD_VERILOG_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "prop.h"

// Parse a very small subset of verilog (output of ABC optimization)
// to a proposition, or returns nullopt.
std::optional<Prop> FromVerilog(const World &world, std::string_view content,
                                const std::vector<std::string> &inputs = {});

#endif
