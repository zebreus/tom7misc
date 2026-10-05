
#ifndef _ESCAPE_UPLOAD_H
#define _ESCAPE_UPLOAD_H

#include <string_view>

#include "drawable.h"
#include "player.h"

/* Upload a level and its solution to the server. */

enum class UploadResult { OK, FAIL, };

struct Upload : public Drawable {
  static Upload *Create();
  virtual ~Upload();

  virtual UploadResult Up(Player *p,
                          std::string_view file, std::string_view desc) = 0;

  void Draw() override = 0;
  void ScreenResize() override = 0;
};

#endif
