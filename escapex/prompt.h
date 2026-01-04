#ifndef _PROMPT_H
#define _PROMPT_H

#include <string>
#include <string_view>

#include "drawable.h"

struct Prompt {
  std::string title;
  std::string input;
  std::string explanation;

  Drawable *below;

  /* XXX should probably be just this static method,
     and perhaps in util */
  static std::string Ask(Drawable *b,
                         std::string_view title,
                         std::string_view def = "");

  static Prompt *Create();

  std::string select();

  virtual ~Prompt();
};

#endif
