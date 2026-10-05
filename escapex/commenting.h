
#ifndef _ESCAPE_COMMENTING_H
#define _ESCAPE_COMMENTING_H

#include <string_view>

#include "player.h"
#include "level.h"

struct CommentScreen {
  static void Comment(Player *plr, const Level *l, std::string_view md5,
                      bool cookmode = false);
};

#endif
