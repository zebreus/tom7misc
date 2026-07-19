
#ifndef _TOWARD_BANNED_H
#define _TOWARD_BANNED_H

#include <unordered_set>
#include <string>

// Font filenames that don't load for mysterious reasons.
// We have plenty of fonts so we just skip these.
const std::unordered_set<std::string> &BannedFonts();

#endif
