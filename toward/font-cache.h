
#ifndef _TOWARD_FONT_CACHE
#define _TOWARD_FONT_CACHE

#include <string_view>

#include "letters.h"

struct FontCache {
  // Singleton.
  // There's just some set of fonts that are available; they
  // are loaded lazily.
  // Returns nullptr if the font is not known.
  static const Letters *Get(std::string_view font_name);
};


#endif

