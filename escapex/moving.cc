#include "moving.h"

#include <cstdlib>
#include <cassert>

#include "disamb.h"
#include "level-base.h"
#include "level.h"
#include "aevent.h"
#include "ptrlist.h"
#include "base/print.h"

using AList = PtrList<AEvent>;

// TODO: Make a lexical distinction between the macros that do something
// that affects the level, and those that are purely animation.

// TODO: instead of comparing enti against -1, use B_PLAYER.

// TODO: Migrate these macros to functions and templates as much as
// possible. This approach is better than the old move.h, but it still
// sucks to have to know what variables are in scope and avoid certain
// symbols inside the lambdas...

  /* is the tile bombable? */
static bool Bombable(int t) {
  switch (t) {
    /* some level of danger */
  case T_EXIT:
  case T_SLEEPINGDOOR:
    /* useful */
  case T_LASER:

    /* obvious */
  case T_BROKEN:
  case T_GREY:
  case T_RED:
  case T_GREEN:

    /* a soft metal. ;) */
  case T_GOLD:

  case T_NS:
  case T_WE:
  case T_NW:
  case T_NE:
  case T_SW:
  case T_SE:
  case T_NSWE:
  case T_TRANSPONDER:
  case T_BUTTON:
  case T_BLIGHT:
  case T_GLIGHT:
  case T_RLIGHT:
  case T_REMOTE:

    /* ?? sure? */
  case T_BLUE:
    /* don't want walls made of this
       ugly thing */
  case T_STOP:

    /* but doesn't count as picking it up */
  case T_HEARTFRAMER:

    /* ?? easier */
  case T_PANEL:
  case T_RPANEL:
  case T_GPANEL:
  case T_BPANEL:

    return true;

  case T_FLOOR:

    /* obvious */
  case T_HOLE:
  case T_ELECTRIC:
  case T_BLACK:
  case T_ROUGH:

    /* for symmetry with holes.
       maybe could become holes,
       but that is just more
       complicated */
  case T_TRAP1:
  case T_TRAP2:


    /* useful for level designers */
  case T_LEFT:
  case T_RIGHT:
  case T_UP:
  case T_DOWN:

    /* Seems sturdy */
  case T_TRANSPORT:
  case T_ON:
  case T_OFF:
  case T_1:
  case T_0:

    /* made of metal */
  case T_STEEL:
  case T_BSTEEL:
  case T_RSTEEL:
  case T_GSTEEL:

  case T_LR:
  case T_UD:

  case T_SPHERE:
  case T_BSPHERE:
  case T_RSPHERE:
  case T_GSPHERE:

    /* shouldn't bomb the floorlike things,
       so also their 'up' counterparts */
  case T_BUP:
  case T_BDOWN:
  case T_GUP:
  case T_GDOWN:
  case T_RUP:
  case T_RDOWN:

    return false;
  }

  assert(!"illegal tile");
  return false;
}

static Tile RealPanel(int f) {
  if (f & TF_RPANELH) {
    if (f & TF_RPANELL) return T_RPANEL;
    else return T_GPANEL;
  } else {
    if (f & TF_RPANELL) return T_BPANEL;
    else return T_PANEL;
  }
}

static bool HasFramers(const Level &level) {
  for (int i = 0; i < level.Width() * level.Height(); i++) {
    if (level.tiles[i] == T_HEARTFRAMER) return true;
  }
  return false;
}


// TODO: No need to wrap *calls* to these macros in if (ANIMATING) ...

#define AFFECT2016(x, y) do {                   \
    (void)ctx->affect((x), (y), level, etail);   \
  } while (0)

#define AFFECTI2016(i) do {                     \
    (void)ctx->affecti((i), level, etail);       \
  } while (0)

// This new version of the macro looks more like a function call.
// It requires the following to be in scope:
//     etail   (XXX docs)
//     ctx, which is a Disamb* or NullDisamb*.
//     ANIMATING (a compile-time bool, indicating whether animation is
//                turned on).
// It should be used like this:
//
// PUSHMOVE2016(bombsplosion, ([&](bomsplosion_t *e) {
//   e->x = x;
//   e->y = y;
// }));
//
// Note the need for parentheses around the lambda argument if it
// contains a comma outside of parentheses. It also seems to fail to
// parse (or somehow parses wrong?) if there are ternary operators
// inside the lambda. (??)
//
// No code is executed (lambda is dead) if ANIMATING is false. Scope
// of fn should be as expected and the macro should behave like a
// normal statement.
#define PUSHMOVE2016(type, fn) do {             \
    if (ANIMATING) {                            \
      (fn)([&]() -> type ## _t * {              \
        AEvent *a = new AEvent;                 \
        *etail = new AList(a, nullptr);         \
        etail = &((*etail)->next);              \
        a->serial = ctx->Serial();              \
        a->t = tag_ ## type;                    \
        return &(a->u. type );                  \
      }());                                     \
    }                                           \
  } while (0)

// Expected in scope: ctx, ANIMATING
// fn is a lambda that takes no args. It is executed whether animation
// is on or not!
#define AFFECTENT2016(entid, fn) do {                                   \
    const int affectent_ei = (entid);                                   \
    if (ANIMATING) {                                                    \
      if (affectent_ei == -1) ctx->preaffectplayer(level, etail);       \
      else ctx->preaffectbot(affectent_ei, level, etail);               \
    }                                                                   \
    (fn)();                                                             \
    if (ANIMATING) {                                                    \
      if (affectent_ei == -1) ctx->postaffectplayer();                  \
      else ctx->postaffectbot(affectent_ei);                            \
    }                                                                   \
  } while (0)

#define BOTEXPLODE2016(botidx) do {                                 \
    if (ANIMATING) {                                                \
      const int botexplode_botidx = (botidx);                       \
      PUSHMOVE2016(botexplode, [&](botexplode_t *e) {               \
          level->Where(level->boti[botexplode_botidx], e->x, e->y); \
        });                                                         \
    }                                                               \
  } while (0)

#define WALKEDEX2016(d, ex, ey, ei, push) do {                          \
    if (ANIMATING) {                                                    \
      const dir walked_d = (d);                                         \
      const int walked_ex = (ex), walked_ey = (ey);                     \
      const int walked_ei = (ei);                                       \
      const bool walked_push = (push);                                  \
      const int walked_under = level->TileAt(walked_ex, walked_ey);     \
      const bot walked_entt =                                           \
        (walked_ei == -1) ? B_PLAYER : level->bott[walked_ei];          \
      const int walked_data =                                           \
        (walked_ei == -1) ? 0 : level->bota[walked_ei];                 \
      PUSHMOVE2016(walk, ([&](walk_t *e) {                              \
        e->srcx = walked_ex;                                            \
        e->srcy = walked_ey;                                            \
        e->d = walked_d;                                                \
        e->pushing = walked_push;                                       \
        e->whatunder = walked_under;                                    \
        e->entt = walked_entt;                                          \
        e->data = walked_data;                                          \
      }));                                                              \
    }                                                                   \
  } while (0)

#define WALKED2016(d, push) WALKEDEX2016(d, entx, enty, enti, push)

#define TELEPORTOUT2016(ei, xx, yy) do {                                \
    if (ANIMATING) {                                                    \
      const int teleportout_ei = (ei);                                  \
      const int teleportout_x = (xx);                                   \
      const int teleportout_y = (yy);                                   \
      const bot teleportout_entt =                                      \
        (teleportout_ei == -1) ?                                        \
        B_PLAYER : level->bott[teleportout_ei];                         \
      PUSHMOVE2016(teleportout, ([&](teleportout_t *e) {                \
        e->x = teleportout_x;                                           \
        e->y = teleportout_y;                                           \
        e->entt = teleportout_entt;                                     \
      }));                                                              \
    }                                                                   \
  } while (0)

#define TELEPORTIN2016(ei, xx, yy) do {                                 \
    if (ANIMATING) {                                                    \
      const int teleportin_ei = (ei);                                   \
      const int teleportin_x = (xx);                                    \
      const int teleportin_y = (yy);                                    \
      const bot teleportout_entt =                                      \
        (teleportin_ei == -1) ?                                         \
        B_PLAYER : level->bott[teleportin_ei];                          \
      PUSHMOVE2016(teleportin, [&](teleportin_t *e) {                   \
        e->x = teleportin_x;                                            \
        e->y = teleportin_y;                                            \
        e->entt = teleportout_entt;                                     \
      });                                                               \
    }                                                                   \
  } while (0)

#define TRAP2016(xx, yy, tt) do {               \
    if (ANIMATING) {                            \
      const int trap_xx = (xx), trap_yy = (yy); \
      const int trap_tt = (tt);                 \
      PUSHMOVE2016(trap, ([&](trap_t *e) {      \
        e->x = trap_xx;                         \
        e->y = trap_yy;                         \
        e->whatold = trap_tt;                   \
      }));                                      \
    }                                           \
  } while (0)

#define TOGGLE2016(xx, yy, t, d) do {                   \
    if (ANIMATING) {                                    \
      const int toggle_x = (xx), toggle_y = (yy);       \
      const int toggle_wh = (t);                        \
      const int toggle_delay = (d);                     \
      PUSHMOVE2016(toggle, ([&](toggle_t *e) {          \
        e->x = toggle_x;                                \
        e->y = toggle_y;                                \
        e->whatold = toggle_wh;                         \
        e->delay = toggle_delay;                        \
      }));                                              \
    }                                                   \
  } while (0)

#define BUTTON2016(xx, yy, t) do {                \
    if (ANIMATING) {                              \
      const int button_x = (xx), button_y = (yy); \
      const int button_wh = (t);                  \
      PUSHMOVE2016(button, ([&](button_t *e) {    \
        e->x = button_x;                          \
        e->y = button_y;                          \
        e->whatold = button_wh;                   \
      }));                                        \
    }                                             \
  } while (0)

#define LITEWIRE2016(xx, yy, wh, pd, cc) do {           \
    if (ANIMATING) {                                    \
      const int lw_xx = (xx), lw_yy = (yy);             \
      const int lw_wh = (wh), lw_pd = (pd);             \
      const int lw_cc = (cc);                           \
      PUSHMOVE2016(litewire, ([&](litewire_t *e) {      \
        e->x = lw_xx;                                   \
        e->y = lw_yy;                                   \
        e->what = lw_wh;                                \
        e->dir = lw_pd;                                 \
        e->count = lw_cc;                               \
      }));                                              \
    }                                                   \
  } while (0)

# define PUSHED2016(d, w, x, y, u, z, h) do {           \
    if (ANIMATING) {                                    \
      const dir pushed_d = (d);                         \
      const int pushed_w = (w);                         \
      const int pushed_x = (x), pushed_y = (y);         \
      const int pushed_u = (u);                         \
      const bool pushed_z = (z), pushed_h = (h);        \
      PUSHMOVE2016(push, ([&](push_t *e) {              \
        e->srcx = pushed_x;                             \
        e->srcy = pushed_y;                             \
        e->d = pushed_d;                                \
        e->under = pushed_u;                            \
        e->what = pushed_w;                             \
        e->zap = pushed_z;                              \
        e->hole = pushed_h;                             \
      }));                                              \
    }                                                   \
  } while (0)


// Helper functions -- these actually have an effect on the level state,
// in addition to animating.

// Modifies the in-scope entity position variables, as well as updating
// the permanent location. XXX maybe this is a bad idea?
#define SETENTPOS2016(xx, yy) do {          \
    entx = xx;                              \
    enty = yy;                              \
    level->SetEntPos(enti, entx, enty);     \
  } while (0)

// Note that this macro has the side-effect of actually doing the
// tile swap!
#define SWAPO2016(idx) do {                   \
    const int swapo_idx = (idx);              \
    PUSHMOVE2016(swap, ([&](swap_t *e) {      \
      int xx, yy;                             \
      level->Where(swapo_idx, xx, yy);        \
      e->x = xx;                              \
      e->y = yy;                              \
      e->was = level->TileAt(xx, yy);         \
      e->now = level->OTileAt(xx, yy);        \
    }));                                      \
    level->SwapO(swapo_idx);                  \
  } while (0)

// Swap tiles t1 and t2 throughout the level foreground.
#define SWAPTILES2016(t1, t2, d) do {                 \
    const int st_t1 = (t1), st_t2 = (t2), st_d = (d); \
    for (int y = 0; y < level->Height(); y++) {       \
      for (int x = 0; x < level->Width(); x++) {      \
        const int t = level->TileAt(x, y);            \
        if (t == st_t1) {                             \
          AFFECT2016(x, y);                           \
          TOGGLE2016(x, y, t, st_d);                  \
          level->SetTile(x, y, st_t2);                \
        } else if (t == st_t2) {                      \
          AFFECT2016(x, y);                           \
          TOGGLE2016(x, y, t, st_d);                  \
          level->SetTile(x, y, st_t1);                \
        }                                             \
      }                                               \
    }                                                 \
  } while (0)

/* after stepping off a tile, deactivate a panel
   if there was one there. */
/* nb: only for regular panels */
#define CHECKLEAVEPANEL2016(xx, yy) do {                                \
    const int checkleavepanel_xx = (xx);                                \
    const int checkleavepanel_yy = (yy);                                \
    if (level->TileAt(checkleavepanel_xx, checkleavepanel_yy) ==        \
        T_PANEL) {                                                      \
      AFFECTI2016(                                                      \
          level->DestAt(checkleavepanel_xx, checkleavepanel_yy));       \
      SWAPO2016(level->DestAt(checkleavepanel_xx, checkleavepanel_yy)); \
    }                                                                   \
  } while (0)

#define CHECKTRAP2016(xx, yy) do {                                      \
    const int checktrap_xx = (xx), checktrap_yy = (yy);                 \
    if (level->TileAt(checktrap_xx, checktrap_yy) == T_TRAP1) {         \
      AFFECT2016(checktrap_xx, checktrap_yy);                           \
      level->SetTile(checktrap_xx, checktrap_yy, T_HOLE);               \
      TRAP2016(checktrap_xx, checktrap_yy, T_TRAP1);                    \
    } else if (level->TileAt(checktrap_xx, checktrap_yy) == T_TRAP2) {  \
      AFFECT2016(checktrap_xx, checktrap_yy);                           \
      level->SetTile(checktrap_xx, checktrap_yy, T_TRAP1);              \
      TRAP2016(checktrap_xx, checktrap_yy, T_TRAP2);                    \
    }                                                                   \
  } while (0)

/* actions on the player stepping off of a tile */
/* generally, you should only call this once per
   motion, at the very end. that's because it may
   install new panels (by swapping), and panel swaps
   are supposed to happen at the end. */
#define CHECKSTEPOFF2016(xx, yy) do {                                  \
    const int checkstepoff_xx = (xx), checkstepoff_yy = (yy);          \
    CHECKTRAP2016(checkstepoff_xx, checkstepoff_yy);                   \
    CHECKLEAVEPANEL2016(checkstepoff_xx, checkstepoff_yy);             \
  } while (0)

/* must call this whenever a bot steps
   onto a tile where there might be other
   bots. (walking atop one, or teleporting)

   we assume that there is at most ONE other
   bot, by invariant (which this function restores).
*/
#define CHECKBOTDEATH2016(xx, yy, me) do {                  \
    const int cbd_xx = (xx), cbd_yy = (yy);                 \
    const int cbd_me = (me);                                \
    if (cbd_me != B_PLAYER) { /* checked at end of turn */  \
      const int mei = level->Index(cbd_xx, cbd_yy);         \
      for (int b = 0; b < level->nbots; b++) {              \
        if (cbd_me != b &&                                  \
            level->bott[b] != B_DELETED &&                  \
            level->bott[b] != B_BOMB_X &&                   \
            mei == level->boti[b]) {                        \
          /* yes! delete other bot and make me */           \
          /* broken. */                                     \
          AFFECTI2016(mei);                                 \
          level->bott[b] = B_DELETED;                       \
          level->bott[cbd_me] = B_BROKEN;                   \
          /* AFFECTENT;  */                                 \
          BOTEXPLODE2016(b);                                \
        }                                                   \
      }                                                     \
    }                                                       \
  } while (0)

template<bool ANIMATING, class DAB>
void Moving::Bombsplode(Level *level,
                        int now,
                        int b, DAB *ctx, AList *&events,
                        AList **&etail) {
  level->bott[b] = B_BOMB_X;
  const auto &[x, y] = level->Where(level->boti[b]);

  /* animate first */
  if (ANIMATING) {
    for (dir dd = FIRST_DIR_SELF; dd < LAST_DIR; dd++) {
      int bx, by;
      if (level->Travel(x, y, dd, bx, by)) {
        AFFECT2016(bx, by);
      }
    }

    PUSHMOVE2016(bombsplosion, [&](bombsplosion_t *e) {
      e->x = x;
      e->y = y;
    });
  }

  for (dir dd = FIRST_DIR_SELF; dd <= LAST_DIR; dd++) {
    int bx, by;
    if (level->Travel(x, y, dd, bx, by)) {
      if (Bombable(level->TileAt(bx, by))) {
        /* animate? */
        level->SetTile(bx, by, T_FLOOR);
        /* clear flags */
        level->SetFlag(bx, by, level->FlagAt(bx, by) & ~(TF_HASPANEL |
                                                         TF_RPANELL  |
                                                         TF_RPANELH));
      }


      int z = level->Index(bx, by);
      for (int bdie = 0; bdie < level->nbots; bdie++) {
        if (level->boti[bdie] == z) {
          AFFECTENT2016(bdie, []{});

          bot bd = level->bott[bdie];
          if (bd == B_DELETED ||
              bd == B_BOMB_X) /* ignore */ continue;

          if (Level::IsBomb(bd)) {
            /* chain reaction */
            if (bdie < now) {
              Bombsplode<ANIMATING, DAB>(level, now, bdie, ctx, events, etail);
              break;
            } else {
              /* will explode this turn (unless a bot
                 pushes it??) */
              level->bota[bdie] = 0;
              break;
            }
          } else {
            /* non-bomb, so just kill it */
            /* FIXME: This isn't correct in the case that
               some bots move after the bomb explodes; they
               might move *onto* the explosion. See
               "revenge of the malformed levels 2". Of
               course, this can only happen if there are
               bots with higher numbers than bombs, which
               levels can't be made by the editor. Possibly
               they should be rejected by the sanitizer as
               well. */
            BOTEXPLODE2016(bdie);
            level->bott[bdie] = B_DELETED;
            break;
          } /* isbomb? */
        } /* bot here? */
      } /* loop over bots */

    } /* adjacent? */
  } /* loop adjacent */
}


// Assumes target = T_TRANSPORT.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntTransport(Level *level,
                              dir d, int enti, Capabilities cap,
                              int entx, int enty, int newx, int newy,
                              DAB *ctx, AList *&events,
                              AList **&etail) {
  /* not if there's an entity there */
  if (level->PlayerAt(newx, newy) ||
      level->BotAt(newx, newy)) return false;

  if (cap & (CAP_CANTELEPORT | CAP_ISPLAYER)) {
    AFFECT2016(newx, newy);

    AFFECTENT2016(enti, []{});
    WALKED2016(d, true);
    AFFECTENT2016(enti, []{});

    TELEPORTOUT2016(enti, newx, newy);

    const auto &[targx, targy] =
      level->Where(level->dests[level->Index(newx, newy)]);

    /* cache this, since stepping off might change it */
    const int targ = level->TileAt(targx, targy);

    CHECKSTEPOFF2016(entx, enty);
    SETENTPOS2016(targx, targy);

    AFFECT2016(targx, targy);
    AFFECTENT2016(enti, []{});

    /* teleporting always faces the player down;
       there's just one animation. */
    level->SetEntDir(enti, DIR_DOWN);

    TELEPORTIN2016(enti, targx, targy);

    switch (targ) {
    case T_PANEL:
      AFFECTI2016(level->DestAt(targx, targy));
      SWAPO2016(level->DestAt(targx, targy));
      break;
    default:
      break;
    }

    CHECKBOTDEATH2016(targx, targy, enti);

    return true;
  }
  return false;
}

// Assumes target == T_EXIT.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntExit(Level *level,
                         dir d, int enti, Capabilities cap,
                         int entx, int enty, int newx, int newy,
                         DAB *ctx, AList *&events,
                         AList **&etail) {
  /* XXX check cap */

  /* can't push bots off exits */
  if (level->PlayerAt(newx, newy) ||
      level->BotAt(newx, newy)) return false;

  CHECKSTEPOFF2016(entx, enty);

  AFFECTENT2016(enti, []{});
  WALKED2016(d, false);

  SETENTPOS2016(newx, newy);

  /* open door */
  AFFECT2016(newx, newy);

  PUSHMOVE2016(opendoor, [&](opendoor_t *e) {
    e->x = newx;
    e->y = newy;
  });

  return true;
}

// Assumes target == T_ON.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntOn(Level *level,
                       dir d, int enti, Capabilities cap,
                       int entx, int enty, int newx, int newy,
                       DAB *ctx, AList *&events,
                       AList **&etail) {
  /* can't activate if someone stands on it */
  if (level->PlayerAt(newx, newy) ||
      level->BotAt(newx, newy)) return false;

  for (int y = 0; y < level->Height(); y++) {
    for (int x = 0; x < level->Width(); x++) {
      if (level->TileAt(x, y) == T_ELECTRIC) {
        AFFECT2016(x, y);
        TOGGLE2016(x, y, T_ELECTRIC, std::abs(x - newx) + std::abs(y - newy));
        level->SetTile(x, y, T_FLOOR);
      }
    }
  }

  AFFECT2016(newx, newy);
  level->SetTile(newx, newy, T_OFF);
  /* XXX animate pushing, but not moving */
  BUTTON2016(newx, newy, T_ON);
  return true;
}

// Assumes target is one of RSPHERE, GSPHERE, BSPHERE, SPHERE, or GOLD.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntGoldlike(Level *level,
                             int target, dir d, int enti, Capabilities cap,
                             int entx, int enty, int newx, int newy,
                             DAB *ctx, AList *&events,
                             AList **&etail) {
  /* spheres allow pushing in a line:
     ->OOOO  becomes OOO   ---->O

     so keep traveling while the tile
     in the destination direction is
     a sphere of any sort.
  */
  const int firstx = newx, firsty = newy;
  int tnx, tny;
  while (Level::IsSphere(level->TileAt(newx, newy)) &&
         !(level->PlayerAt(newx, newy) ||
           level->BotAt(newx, newy)) &&
         level->Travel(newx, newy, d, tnx, tny) &&
         Level::IsSphere(level->TileAt(tnx, tny))) {
    newx = tnx;
    newy = tny;
    target = level->TileAt(tnx, tny);
  }

  /* can't push if we ran into entity */
  /* XXX jiggle anyway */
  if (level->PlayerAt(newx, newy) ||
      level->BotAt(newx, newy)) return false;

  if (ANIMATING) {
    /* if we passed through any spheres,
       'jiggle' them (even if we don't ultimately push). */
    if (firstx != newx || firsty != newy) {
      /* affect whole row */
      for (int ix = firstx, iy = firsty;
           (firstx != newx && firsty != newy);
           level->Travel(ix, iy, d, ix, iy)) {
        (void)firstx;
        (void)newx;
        (void)firsty;
        (void)newy;
        AFFECT2016(ix, iy);
      }

      const int num = abs(newx - firstx) + abs(newy - firsty);
      PUSHMOVE2016(jiggle, ([&](jiggle_t *e) {
        e->startx = firstx;
        e->starty = firsty;
        e->d = d;
        e->num = num;
      }));
    }
  }

  int goldx = newx, goldy = newy;

  /* remove gold block */
  AFFECT2016(goldx, goldy);
  const int replacement = (level->FlagAt(goldx, goldy) & TF_HASPANEL) ?
    RealPanel(level->FlagAt(goldx, goldy)) :
    T_FLOOR;

  level->SetTile(goldx, goldy, replacement);

  int tgoldx, tgoldy;

  while (level->Travel(goldx, goldy, d, tgoldx, tgoldy)) {
    int next = level->TileAt(tgoldx, tgoldy);
    if (!(next == T_ELECTRIC ||
          next == T_PANEL ||
          next == T_BPANEL ||
          next == T_RPANEL ||
          next == T_GPANEL ||
          next == T_FLOOR) ||
        level->BotAt(tgoldx, tgoldy) ||
        level->PlayerAt(tgoldx, tgoldy)) break;

    goldx = tgoldx;
    goldy = tgoldy;

    if (next == T_ELECTRIC) break;

    // Otherwise, going to fly through this spot.
    if (ANIMATING) {
      const bool did = ctx->affect(goldx, goldy, level, etail);
      if (false)
        printf("%d %d: %s %d %d\n", goldx, goldy, did ? "did" : "not",
               ctx->SerialAt(goldx, goldy),
               ctx->Serial());
    }
  }

  /* goldx is dest, newx is source */
  if (goldx != newx ||
      goldy != newy) {
    const int landon = level->TileAt(goldx, goldy);
    const bool doswap = ([&]{
      /* untrigger from source */
      if (level->FlagAt(newx, newy) & TF_HASPANEL) {
        const int pan = RealPanel(level->FlagAt(newx,newy));
        /* any */
        /* XXX2016? use Level::triggers here */
        if (pan == T_PANEL ||
            /* colors */
            (target == T_GSPHERE &&
             pan == T_GPANEL) ||
            (target == T_RSPHERE &&
             pan == T_RPANEL) ||
            (target == T_BSPHERE &&
             pan == T_BPANEL)) {
          return true;
        }
      }
      return false;
    }());

    /* only the correct color sphere can trigger
       the colored panels */
    const bool doswapt = Level::Triggers(target, landon);

    /* XXX may have already affected this
       if the gold block didn't move at all */
    AFFECT2016(goldx, goldy);
    level->SetTile(goldx, goldy, target);

    const bool zapped = ([&]{
      if (landon == T_ELECTRIC) {
        /* gold zapped. however, if the
           electric was the target of a panel
           that we just left, the electric has
           been swapped into the o world (along
           with the gold). So swap there. */
        level->SetTile(goldx, goldy, T_ELECTRIC);
        return true;
      }
      return false;
    }());

    if (ANIMATING) {
      const int distance = abs((goldx - newx) + (goldy - newy));
      PUSHMOVE2016(fly, ([&](fly_t *e) {
        e->what = target;
        e->whatunder = replacement;
        e->srcx = newx;
        e->srcy = newy;
        e->d = d;
        /* number of tiles traveled */
        e->distance = distance;
        e->zapped = zapped;
      }));
    }

    if (doswapt) {
      AFFECTI2016(level->DestAt(goldx, goldy));
      SWAPO2016(level->DestAt(goldx, goldy));
    }

    if (doswap) {
      AFFECTI2016(level->DestAt(newx, newy));
      SWAPO2016(level->DestAt(newx, newy));
    }

    return true;

  } else {
    /* didn't move; put it back */
    level->SetTile(newx, newy, target);

    /* TODO: animate it when 'stuck' */
    return false;
  }
}


// assumes target is T_BUTTON.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntButton(Level *level,
                           dir d, int enti, Capabilities cap,
                           int entx, int enty, int newx, int newy,
                           DAB *ctx, AList *&events,
                           AList **&etail) {
  /* careful: the order in which swaps happen
     matters, because an earlier pulse could
     block beams for a later one (by raising floor).
     So we have to save up the effects and then
     perform them all at once at the end. (Panel
     swaps from remotes are last as always.)
  */

  /* XXX check caps */

  /* We want the wires to only glow if they are connected to
     something, so the function ''isconnected'' will tell us
     if a pulse will succeed in hitting something.

     After that, in each successful direction we trace out
     and animate. We also remember the effects; these all
     have to happen at the end or else it could matter what
     order we try the pulses in.
  */

  /* these are for remote swaps */
  // PERF can probably just use vector<>?
  struct SwapList {
    int target;
    SwapList *next;
    SwapList(int t, SwapList *n) : target(t), next(n) {}
  };
  SwapList *remotes = nullptr;

  /* need to delay swaps to the end. */
  int bswaps = 0, rswaps = 0, gswaps = 0;

  if (level->PlayerAt(newx, newy) ||
      level->BotAt(newx, newy)) return false;

  /* but always push a button pressing anim */
  AFFECT2016(newx, newy);
  BUTTON2016(newx, newy, T_BUTTON);

  for (dir dd = FIRST_DIR; dd <= LAST_DIR; dd++) {
    // if animation is off, then don't pre-scan
    if (!ANIMATING ||
        level->IsConnected(newx, newy, dd)) {

      /* send a pulse in that direction. */
      int pulsex = newx, pulsey = newy;
      dir pd = dd;

      int dist = 0;

      while (pd != DIR_NONE &&
             level->Travel(pulsex, pulsey, pd, pulsex, pulsey)) {
        const int targ = level->TileAt(pulsex, pulsey);

        /* liteup animation if a wire. note: this code
           would lite up any tile (floor, exit, etc.) except
           that those are avoided by the pre-scan above. */
        if (ANIMATING) {
          switch (targ) {
          case T_BLIGHT:
          case T_RLIGHT:
          case T_GLIGHT:
          case T_REMOTE:
            break;
          default:
            /* Should affect here, because for example remotes
               might target some of these wires. But then subsequent
               wires are delayed because of the delay hack we use.
               So, if affecting advances the serial, reset the delay.
               XXX: The result is correct but not desirable; wires
               pause in the middle of their electricity. Probably
               should arrange it so it all goes in one shot. */
            if (ctx->affect(pulsex, pulsey, level, etail)) {
              dist = 0;
            }
            LITEWIRE2016(pulsex, pulsey, targ, pd, dist);
            break;
          }
        }

        dist++;

        switch (targ) {
        case T_REMOTE:
          if (ANIMATING) {
            AFFECT2016(pulsex, pulsey);
            PUSHMOVE2016(liteup, ([&](liteup_t *e) {
              e->x = pulsex;
              e->y = pulsey;
              e->what = targ;
              e->delay = dist;
            }));
          }
          remotes = new SwapList(level->DestAt(pulsex, pulsey), remotes);

          /* since this counts as being connected so far, we want to
             make sure that the circuit continues before animating
             it ... */
          if (ANIMATING) {
            if (!level->IsConnected(pulsex, pulsey, pd)) pd = DIR_NONE;
          }
          continue;

        case T_BLIGHT:
        case T_RLIGHT:
        case T_GLIGHT:
          if (ANIMATING) {
            AFFECT2016(pulsex, pulsey);
            PUSHMOVE2016(liteup, ([&](liteup_t *e) {
              e->x = pulsex;
              e->y = pulsey;
              e->what = targ;
              e->delay = dist;
            }));
          }

          if (targ == T_BLIGHT) bswaps++;
          if (targ == T_RLIGHT) rswaps++;
          if (targ == T_GLIGHT) gswaps++;
          pd = DIR_NONE;
          break;

        case T_TRANSPONDER: {
          // printf("transponder at %d/%d\n", pulsex, pulsey);
          const int transx = pulsex;
          const int transy = pulsey;
          if (!level->Travel(pulsex, pulsey, pd, pulsex, pulsey)) {
            pd = DIR_NONE;
          } else {
            /* keep going until we hit another transponder. */
            do {
              const int ta = level->TileAt(pulsex, pulsey);
              // printf(" ... at %d/%d: %d\n", pulsex, pulsey, ta);
              if (!level->AllowBeam(ta) ||
                  level->BotAt(pulsex, pulsey) ||
                  level->PlayerAt(pulsex, pulsey)) {
                /* hit something. is it a transponder? */
                if (ta == T_TRANSPONDER) {
                  /* okay, then we are on the 'old' tile with
                     the direction set, so we're ready to continue
                     the pulse loop */
                  LITEWIRE2016(pulsex, pulsey, targ, pd, dist);
                  PUSHMOVE2016(transponderbeam, ([&](transponderbeam_t *e) {
                    e->x = transx;
                    e->y = transy;
                    e->lx = pulsex;
                    e->ly = pulsey;
                    e->from = pd;
                    e->count = dist;
                  }));

                } else {
                  /* didn't hit transponder! stop. */
                  pd = DIR_NONE;
                }
                break;
              } else {
                /* in preparation for beam across these squares... */
                AFFECT2016(pulsex, pulsey);
              }
              /* otherwise keep going... */
            } while (level->Travel(pulsex, pulsey, pd, pulsex, pulsey));
          }

          break;
        }

        case T_NSWE:
          /* just keep going in same direction */
          continue;

        case T_NS:
          if (pd == DIR_UP || pd == DIR_DOWN) continue;
          else pd = DIR_NONE;
          break;

        case T_WE:
          if (pd == DIR_LEFT || pd == DIR_RIGHT) continue;
          else pd = DIR_NONE;
          break;

        case T_NW:
          if (pd == DIR_DOWN) pd = DIR_LEFT;
          else if (pd == DIR_RIGHT) pd = DIR_UP;
          else pd = DIR_NONE;
          break;

        case T_SW:
          if (pd == DIR_UP) pd = DIR_LEFT;
          else if (pd == DIR_RIGHT) pd = DIR_DOWN;
          else pd = DIR_NONE;
          break;

        case T_NE:
          if (pd == DIR_DOWN) pd = DIR_RIGHT;
          else if (pd == DIR_LEFT) pd = DIR_UP;
          else pd = DIR_NONE;
          break;

        case T_SE:
          if (pd == DIR_UP) pd = DIR_RIGHT;
          else if (pd == DIR_LEFT) pd = DIR_DOWN;
          else pd = DIR_NONE;
          break;

        default: /* any non-wire stops electricity */
          pd = DIR_NONE;
          break;
        }
      }
    }

  }

  /* XXX for better results, delay according
     to the time (push times onto a stack or
     something) */
  while (bswaps--) {
    SWAPTILES2016(T_BUP, T_BDOWN, 0);
  }

  while (rswaps--) {
    SWAPTILES2016(T_RUP, T_RDOWN, 0);
  }

  while (gswaps--) {
    SWAPTILES2016(T_GUP, T_GDOWN, 0);
  }

  while (remotes != nullptr) {
    SwapList *t = remotes;
    remotes = remotes->next;
    if (ANIMATING) {
      const auto &[x, y] = level->Where(t->target);
      if (false) Print("(was {} []) ",
                       ctx->SerialAt(x, y),
                       ctx->Serial());
      bool did = ctx->affecti(t->target, level, etail);
      if (false) Print("{}={},{}: {} {} {}\n",
                       t->target, x, y, did ? "did" : "not",
                       ctx->SerialAt(x, y),
                       ctx->Serial());
      // AFFECTI(t->target);
    }
    SWAPO2016(t->target);
    delete t;
  }

  return true;
}

// Assumes target == T_HEARTFRAMER.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntHeartframer(Level *level,
                                dir d, int enti, Capabilities cap,
                                int entx, int enty, int newx, int newy,
                                DAB *ctx, AList *&events,
                                AList **&etail) {
  if (level->BotAt(newx, newy) ||
      level->PlayerAt(newx, newy)) return false;

  /* only the player can pick up
     heart framers */
  if (cap & CAP_HEARTFRAMERS) {
    AFFECT2016(newx, newy);
    AFFECTENT2016(enti, []{});
    WALKED2016(d, true);

    /* snag heart framer */
    level->SetTile(newx, newy, T_FLOOR);

    PUSHMOVE2016(getheartframer, [&](getheartframer_t *e) {
      e->x = newx;
      e->y = newy;
    });

    /* any heart framers left? */

    if (!HasFramers(*level)) {
      for (int y = 0; y < level->Height(); y++) {
        for (int x = 0; x < level->Width(); x++) {
          const int t = level->TileAt(x, y);
          if (t == T_SLEEPINGDOOR) {
            AFFECT2016(x, y);
            PUSHMOVE2016(wakeupdoor, [&](wakeupdoor_t *e) {
              e->x = x; e->y = y;
            });
            level->SetTile(x, y, T_EXIT);
          }
        }
      }

      /* also bots */
      for (int i = 0; i < level->nbots; i++) {
        const int orig_bott = level->bott[i];
        switch (orig_bott) {
        case B_DALEK_ASLEEP:
        case B_HUGBOT_ASLEEP:
          AFFECTENT2016(i, [&]() {
            AFFECTI2016(level->boti[i]);
          });

          level->bott[i] =
            (orig_bott == B_DALEK_ASLEEP) ?
            B_DALEK : B_HUGBOT;
          PUSHMOVE2016(stand, [&](stand_t *e) {
              level->Where(level->boti[i], e->x, e->y);
              e->d = level->botd[i];
              e->entt = level->bott[i];
              e->data = level->bota[i];
          });

          if (ANIMATING) {
            const auto &[xx, yy] = level->Where(level->boti[i]);

            PUSHMOVE2016(wakeup, [&](wakeup_t *e) {
              e->x = xx;
              e->y = yy;
            });
          }
          break;

        default:
          break;
        }
      }
    }

    /* panel actions are last */
    CHECKSTEPOFF2016(entx, enty);

    SETENTPOS2016(newx, newy);

    return true;
  }
  return false;
}

// Assumes target == T_ELECTRIC.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntElectric(Level *level,
                             dir d, int enti, Capabilities cap,
                             int entx, int enty, int newx, int newy,
                             DAB *ctx, AList *&events,
                             AList **&etail) {
  if (level->BotAt(newx, newy) ||
      level->PlayerAt(newx, newy)) return false;
  /* some bots are stupid enough
     to zap themselves */
  if (enti != B_PLAYER &&
      (cap & CAP_ZAPSELF)) {

    AFFECT2016(newx, newy);
    AFFECTENT2016(enti, []{});
    WALKED2016(d, false);

    /* change where it is */
    level->boti[enti] = level->Index(newx, newy);

    /* then kill it */
    level->bott[enti] = B_DELETED;
    AFFECTENT2016(enti, []{});
    BOTEXPLODE2016(enti);

    /* might have stepped off a panel/trap */
    CHECKSTEPOFF2016(entx, enty);

    return true;
  }
  return false;
}

// Assumes target == T_BROKEN.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntBroken(Level *level,
                           dir d, int enti, Capabilities cap,
                           int entx, int enty, int newx, int newy,
                           DAB *ctx, AList *&events,
                           AList **&etail) {
  if (level->PlayerAt(newx, newy) ||
      level->BotAt(newx, newy)) return false;

  AFFECT2016(newx, newy);
  level->SetTile(newx, newy, T_FLOOR);

  PUSHMOVE2016(breaks, ([&](breaks_t *e) {
    e->x = newx;
    e->y = newy;
  }));
  return true;
}

// Assumes target == T_GREEN.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntGreen(Level *level,
                          dir d, int enti, Capabilities cap,
                          int entx, int enty, int newx, int newy,
                          DAB *ctx, AList *&events,
                          AList **&etail) {
  if (level->PlayerAt(newx, newy) ||
      level->BotAt(newx, newy)) return false;

  int destx, desty;
  if (level->Travel(newx, newy, d, destx, desty) &&
      level->TileAt(destx, desty) == T_FLOOR &&
      !level->BotAt(destx, desty) &&
      !level->PlayerAt(destx, desty)) {
    level->SetTile(destx, desty, T_BLUE);
    level->SetTile(newx, newy, T_FLOOR);

    AFFECT2016(destx, desty);
    AFFECT2016(newx, newy);
    AFFECTENT2016(enti, []{});

    PUSHMOVE2016(pushgreen, ([&](pushgreen_t *e) {
      e->srcx = newx;
      e->srcy = newy;
      e->d = d;
    }));

    WALKED2016(d, true);

    CHECKSTEPOFF2016(entx, enty);

    SETENTPOS2016(newx, newy);

    return true;
  }

  return false;
}


// Assumes target is one of RPANEL, GPANEL, BPANEL, TRAP1, TRAP2,
// FLOOR, ROUGH, RDOWN, GDOWN, BDOWN, or PANEL.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntFloorlike(Level *level,
                              int target, dir d, int enti, Capabilities cap,
                              int entx, int enty, int newx, int newy,
                              DAB *ctx, AList *&events,
                              AList **&etail) {
  bool pushing = false;
  int pushent = 0;
  if (level->PlayerAt(newx, newy)) {
    /* if player is on bot, can't push */
    if (level->BotAt(newx, newy)) return false;
    /* push? */
    if (cap & CAP_PUSHPLAYER) {
      pushing = true;
      pushent = B_PLAYER;
      /* step on? */
    } else if (cap & CAP_CRUSHPLAYER) {
      pushing = false;
    } else return false;
  } else if (level->BotAt(newx, newy, pushent)) {
    /* push? */
    if (cap & CAP_PUSHBOTS) {
      /* check that the target bot is pushable */
      pushing = true;
      /* step on? */
    } else if (cap & CAP_WALKINTOBOTS) {
      pushing = false;
    } else return false;
  }

  if (pushing) {
    /* OK, we know that the next spot contains a bot (or player), and
       we have the capability to push that entity. But we have to
       check what lives beyond this spot. */
    int farx, fary;
    if (level->Travel(newx, newy, d, farx, fary)) {
      int ftarget = level->TileAt(farx, fary);
      switch (ftarget) {
      case T_ELECTRIC:
        /* only bots pushed into electric */
        if (pushent == B_PLAYER) return false;
        else break;
      case T_TRAP2:
      case T_TRAP1:
      case T_FLOOR:
      case T_ROUGH:
      case T_RDOWN:
      case T_GDOWN:
      case T_BDOWN:
      case T_PANEL:
      case T_RPANEL:
      case T_GPANEL:
      case T_BPANEL:
        break;
      default:
        return false;
      }
      /* also check bot -- can't push two in a row */
      if (level->BotAt(farx, fary)) return false;
      if (level->PlayerAt(farx, fary)) return false;

      /* affect first, then make anims */

      AFFECTENT2016(enti, ([&]{
        AFFECTENT2016(pushent, ([&]{
          AFFECT2016(farx, fary);
          AFFECT2016(newx, newy);

          /* if a bomb, light and reset fuse */
          /* XXX animate? */
          if (pushent != -1 &&
              Level::IsBomb(level->bott[pushent]))
            level->bota[pushent] = ((int)level->bott[pushent] - (int)B_BOMB_0);

          // XXX2016: This inverts the order that postaffect
          // ran vs the move.h approach. (But it looks like that
          // shouldn't matter; they both just change serial?)
        }));
      }));


      /* XXX should be "waspushed" or whatever */
      WALKEDEX2016(d, newx, newy, pushent, false);
      /* okay, now move the pushing ent */
      WALKED2016(d, true);

      /* okay, push! */
      if (pushent == B_PLAYER) {
        level->guyx = farx;
        level->guyy = fary;
      } else {
        int id = level->Index(farx, fary);
        level->boti[pushent] = id;
      }

      /* handle leaving current (pusher) pos */
      CHECKTRAP2016(entx, enty);
      /* but still need to check panels, later... */
      const int srcx = entx, srcy = enty;
      const bool swapsrc = level->TileAt(entx, enty) == T_PANEL;

      /* then move me. */
      SETENTPOS2016(newx, newy);

      /* now deal with zapping */
      if (ftarget == T_ELECTRIC &&
          pushent != -1) {
        /* can't be player: kill bot */
        level->bott[pushent] = B_DELETED;
        AFFECT2016(farx, fary);
        BOTEXPLODE2016(pushent);
      }

      /* the tile in the middle is being stepped off
         and stepped on. if it's a panel, don't do anything.
         (to avoid a double swap) */
      if (target == T_PANEL) {
        /* do nothing */
      } else {
        CHECKTRAP2016(newx, newy);
      }

      /* -- panel phase -- */

      /* first, if pusher stepped off a panel, it swaps */
      if (swapsrc) {
        AFFECTI2016(level->DestAt(srcx, srcy));
        SWAPO2016(level->DestAt(srcx, srcy));
      }

      /* pushed ent is stepping onto new panel, perhaps */
      if (ftarget == T_PANEL) {
        AFFECTI2016(level->DestAt(farx, fary));
        SWAPO2016(level->DestAt(farx, fary));
      }

      return true;
    } else {
      return false;
    }

  } else {
    // Not pushing.

    /* XXX also affect source? */
    AFFECTENT2016(enti, ([&]{
      AFFECT2016(newx, newy);
    }));
    WALKED2016(d, false);

    /* might have stepped onto bot */
    CHECKBOTDEATH2016(newx, newy, enti);

    /* panel actions */
    CHECKSTEPOFF2016(entx, enty);

    SETENTPOS2016(newx, newy);

    if (target == T_PANEL) {
      // printf(" %d   step on panel...\n", enti);
      AFFECTI2016(level->DestAt(newx, newy));
      SWAPO2016(level->DestAt(newx, newy));
    }

    return true;
  }
}

// Target should be STEEL, RSTEEL, GSTEEL or BSTEEL.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntSteel(Level *level,
                          int target, dir d, int enti, Capabilities cap,
                          int entx, int enty, int newx, int newy,
                          DAB *ctx, AList *&events,
                          AList **&etail) {
  /* three phases. first, see if we can push this
     whole column one space.

     if so, generate animations.

     then, update panel states. this is tricky. */
  int destx = newx, desty = newy;
  {
    int curx = newx, cury = newy;
    /* go until not steel, or if we hit a robot
       anywhere along this, end */
    while (!level->BotAt(curx, cury) && !level->PlayerAt(curx, cury) &&
           level->Travel(curx, cury, d, destx, desty) &&
           Level::IsSteel(level->TileAt(destx, desty))) {
      curx = destx;
      cury = desty;
    }
  }

  /* entity in our column or at the end? sorry */
  if (level->BotAt(destx, desty) ||
      level->PlayerAt(destx, desty)) return false;

  /* what did we hit? */
  const int hittile = level->TileAt(destx, desty);
  const bool zap = hittile == T_ELECTRIC;
  switch (hittile) {
    /* nb if we "hit" steel, then it's steel to the edge of the
       level, so no push. */
  case T_PANEL:
  case T_GPANEL:
  case T_BPANEL:
  case T_RPANEL:
  case T_FLOOR:
  case T_ELECTRIC:
    break;
  default:
    return false;
  }

  /*  guy            destx,desty
      v              v
      [ ][S][S][S][S][ ]
      ^
      steels
      starting at newx,newy

      d  ---->
  */
  const dir revd = DirReverse(d);


  /* at this point, the push is going through */
  if (ANIMATING) {
    /* make pass to affect (so that all move in same serial) */
    {
      int xx = destx, yy = desty;
      do {
        level->Travel(xx, yy, revd, xx, yy);
        AFFECT2016(xx, yy);
      } while (! (xx == newx && yy == newy));
      AFFECT2016(newx, newy);
    }

    {
      int xx = destx, yy = desty;
      bool zappy = zap;
      do {
        level->Travel(xx, yy, revd, xx, yy);
        int replacement = (level->FlagAt(xx, yy) & TF_HASPANEL) ?
          RealPanel(level->FlagAt(xx,yy)) : T_FLOOR;
        int what = level->TileAt(xx, yy);
        PUSHED2016(d, what, xx, yy, replacement, zappy, false);

        /* only last one can zap */
        zappy = false;
      } while (! (xx == newx && yy == newy));
    }
  }

  /* move the steel blocks first. */
  {
    int movex = destx, movey = desty;
    while (! (movex == newx && movey == newy)) {
      int nextx = 0, nexty = 0;
      level->Travel(movex, movey, revd, nextx, nexty);
      level->SetTile(movex, movey, level->TileAt(nextx, nexty));
      movex = nextx;
      movey = nexty;
    }
  }

  /* and one more, for the tile that we're stepping onto */
  {
    const int replacement = (level->FlagAt(newx, newy) & TF_HASPANEL) ?
      RealPanel(level->FlagAt(newx,newy)) : T_FLOOR;
    level->SetTile(newx, newy, replacement);
  }

  /* reconcile panels.

     imagine pushing a row of blocks one space to the
     right.

     we loop over the NEW positions for the steel blocks.
     If a steel block is on a panel (that it can trigger),
     then we trigger that panel as long as the thing to
     its right (which used to be there) couldn't trigger
     it. this handles new panels that are turned ON.

     if we can't trigger the panel, then we check to see
     if the panel to our right (which used to be there)
     also can't trigger it. If so, we don't do anything.
     Otherwise, we "untrigger" the panel.

     To simplify, if triggerstatus_now != triggerstatus_old,
     we trigger. (Trigger has the same effect as untriggering.)

     Because these swaps are supposed to be delayed, we
     set the TF_TEMP flag if the tile should do a swap
     afterwards.
  */

  bool swapnew = false;
  {
    int lookx = destx, looky = desty;
    int prevt = T_FLOOR; /* anything that doesn't trigger */
    while (! (lookx == newx && looky == newy)) {
      const int heret = level->TileAt(lookx, looky);

      /* triggerstatus for this location (lookx, looky) */
      const bool triggerstatus_now =
        (level->FlagAt(lookx, looky) & TF_HASPANEL) &&
        Level::Triggers(heret, RealPanel(level->FlagAt(lookx, looky)));

      const bool triggerstatus_old =
        (level->FlagAt(lookx, looky) & TF_HASPANEL) &&
        level->IsSteel(prevt) &&
        Level::Triggers(prevt, RealPanel(level->FlagAt(lookx, looky)));

      if (triggerstatus_now != triggerstatus_old) {
        level->SetFlag(lookx, looky, level->FlagAt(lookx, looky) | TF_TEMP);
      } else {
        level->SetFlag(lookx, looky, level->FlagAt(lookx, looky) & ~TF_TEMP);
      }

      prevt = heret;

      int nextx = 0, nexty = 0;
      level->Travel(lookx, looky, revd, nextx, nexty);

      lookx = nextx;
      looky = nexty;
    }

    /* first panel is slightly different */
    {
      const int first = level->TileAt(newx, newy);
      const bool trig_now = first == T_PANEL;
      const bool trig_old =
        level->IsPanel(first) &&
        Level::Triggers(prevt, RealPanel(level->FlagAt(newx, newy)));

      if (trig_old != trig_now) {
        swapnew = true;
      }
    }
  }

  /* zap, if necessary, before swapping. This was
     already animated above. */
  if (zap) {
    level->SetTile(destx, desty, T_ELECTRIC);
  }

  /* now we can start swapping. */
  CHECKSTEPOFF2016(entx, enty);

  /* this part is now invariant to order, because there is
     only one destination per location */

  if (swapnew) {
    AFFECTI2016(level->DestAt(newx, newy));
    SWAPO2016(level->DestAt(newx, newy));
  }

  {
    int lookx = destx, looky = desty;
    while (! (lookx == newx && looky == newy)) {

      if (level->FlagAt(lookx, looky) & TF_TEMP) {
        AFFECTI2016(level->DestAt(lookx, looky));
        SWAPO2016(level->DestAt(lookx, looky));
        level->SetFlag(lookx, looky, level->FlagAt(lookx, looky) & ~TF_TEMP);
      }

      /* next */
      int nextx = 0, nexty = 0;
      level->Travel(lookx, looky, revd, nextx, nexty);
      lookx = nextx;
      looky = nexty;
    }
  }

  /* XXX also boundary conditions? (XXX what does that mean?) */
  AFFECTENT2016(enti, []{});
  WALKED2016(d, true);

  SETENTPOS2016(newx, newy);
  return true;
}

template<bool ANIMATING, class DAB>
bool Moving::MoveEnt01(Level *level,
                       int target, dir d, int enti, Capabilities cap,
                       int entx, int enty, int newx, int newy,
                       DAB *ctx, AList *&events,
                       AList **&etail) {
  if (level->PlayerAt(newx, newy) ||
      level->BotAt(newx, newy)) return false;

  const int opp = (target == T_0 ? T_1 : T_0);

  SWAPTILES2016(T_UD, T_LR, 0);

  AFFECT2016(newx, newy);
  level->SetTile(newx, newy, opp);
  BUTTON2016(newx, newy, target);

  return true;
}

// For target = T_TRANSPONDER, T_RED, T_NSWE, T_NS, T_NE, T_NW, T_SE,
// T_SW, T_WE, T_LR, T_UD, T_GREY.
template<bool ANIMATING, class DAB>
bool Moving::MoveEntPushable(Level *level,
                             int target, dir d, int enti, Capabilities cap,
                             int entx, int enty, int newx, int newy,
                             DAB *ctx, AList *&events,
                             AList **&etail) {
  /* don't push a block that an entity stands on! */
  if (level->PlayerAt(newx, newy) ||
      level->BotAt(newx, newy)) return false;

  // These can only be pushed in the correct direction.
  if (target == T_LR && (d == DIR_UP || d == DIR_DOWN)) return false;
  if (target == T_UD && (d == DIR_LEFT || d == DIR_RIGHT)) return false;

  /* we're always stepping onto the panel that the block was on, so we
     don't need to change its state. (if it's a regular panel, then
     don't change because our feet are on it. if it's a colored panel,
     don't change because neither the man nor the block can activate
     it.) But we do need to put a panel there instead of floor. */
  const int replacement = (level->FlagAt(newx, newy) & TF_HASPANEL)?
    RealPanel(level->FlagAt(newx, newy)) : T_FLOOR;

  bool doswap = false;
  bool zap = false;
  bool hole = false;
  int destx, desty;

  if (!level->Travel(newx, newy, d, destx, desty))
    return false;
  if (level->PlayerAt(destx, desty) ||
      level->BotAt(destx, desty))
    return false;
  const int destt = level->TileAt(destx, desty);
  switch (destt) {
  case T_FLOOR:
    /* easy */
    level->SetTile(destx, desty, target);
    level->SetTile(newx, newy, replacement);
    break;
  case T_ELECTRIC:
    if (target == T_LR || target == T_UD)
      return false;

    /* Zap! */
    level->SetTile(newx, newy, replacement);
    zap = true;
    break;
  case T_HOLE:
    /* only grey blocks into holes */
    if (target != T_GREY)
      return false;

    level->SetTile(destx, desty, T_FLOOR);
    level->SetTile(newx, newy, replacement);
    hole = true;
    break;
    /* all panels are pretty much the same */
  case T_BPANEL:
  case T_RPANEL:
  case T_GPANEL:
  case T_PANEL:
    if (target == T_LR || target == T_UD)
      return false;

    /* delay the swap */
    /* (can only push down grey panels */
    doswap = (destt == T_PANEL);
    level->SetTile(destx, desty, target);
    level->SetTile(newx, newy, replacement);
    break;
  default:
    return false;
  }

  /* Success! */

  AFFECT2016(newx, newy);
  AFFECT2016(destx, desty);
  AFFECTENT2016(enti, []{});

  PUSHED2016(d, target, newx, newy, replacement, zap, hole);
  WALKED2016(d, true);

  CHECKSTEPOFF2016(entx, enty);

  SETENTPOS2016(newx, newy);

  if (doswap) {
    AFFECTI2016(level->DestAt(destx, desty));
    SWAPO2016(level->DestAt(destx, desty));
  }

  return true;
}

template<bool ANIMATING, class DAB>
bool Moving::MoveEnt(Level *level, dir d, int enti, Capabilities cap,
                     int entx, int enty,
                     DAB *ctx, AList *&events, AList **&etail) {

  int newx = 0, newy = 0;
  if (level->Travel(entx, enty, d, newx, newy)) {
    const int target = level->TileAt(newx, newy);
    switch (target) {

    /* these aren't pressed by the player so act like floor */
    case T_BPANEL:
    case T_GPANEL:
    case T_RPANEL:

    /* these are only affected when we step *off* */
    case T_TRAP2:
    case T_TRAP1:

    case T_FLOOR:
    case T_ROUGH:
    case T_BDOWN:
    case T_RDOWN:
    case T_GDOWN:

    /* panels are mostly the same */
    case T_PANEL:
      return MoveEntFloorlike<ANIMATING, DAB>(
          level,
          target, d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_EXIT:
      return MoveEntExit<ANIMATING, DAB>(
          level,
          d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_ON:
      return MoveEntOn<ANIMATING, DAB>(
          level,
          d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_0:
    case T_1:
      return MoveEnt01<ANIMATING, DAB>(
          level,
          target, d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_BSPHERE:
    case T_RSPHERE:
    case T_GSPHERE:
    case T_SPHERE:
    case T_GOLD:
      return MoveEntGoldlike<ANIMATING, DAB>(
          level,
          target, d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_TRANSPORT:
      return MoveEntTransport<ANIMATING, DAB>(
          level,
          d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_BUTTON:
      return MoveEntButton<ANIMATING, DAB>(
          level,
          d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_BROKEN:
      return MoveEntBroken<ANIMATING, DAB>(
          level,
          d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_GREEN:
      return MoveEntGreen<ANIMATING, DAB>(
          level,
          d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_STEEL:
    case T_RSTEEL:
    case T_GSTEEL:
    case T_BSTEEL:
      return MoveEntSteel<ANIMATING, DAB>(
          level,
          target, d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    /* simple pushable blocks use this case */
    case T_TRANSPONDER:
    case T_RED:
    case T_NSWE:
    case T_NS:
    case T_NE:
    case T_NW:
    case T_SE:
    case T_SW:
    case T_WE:

    case T_LR:
    case T_UD:

    case T_GREY:
      return MoveEntPushable<ANIMATING, DAB>(
          level,
          target, d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_HEARTFRAMER:
      return MoveEntHeartframer<ANIMATING, DAB>(
          level,
          d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_ELECTRIC:
      return MoveEntElectric<ANIMATING, DAB>(
          level,
          d, enti,
          cap, entx, enty,
          newx, newy, ctx, events, etail);

    case T_BLUE:
    case T_HOLE:
    case T_LASER:
    case T_STOP:
    case T_RIGHT:
    case T_LEFT:
    case T_UP:
    case T_DOWN:
    case T_BLIGHT:
    case T_RLIGHT:
    case T_GLIGHT:
    case T_RUP:
    case T_BUP:
    case T_GUP:
    case T_OFF:
    case T_BLACK:

      /* XXX animate pushing up against these
         unsuccessfully (anim 'press?') */

    default:
      return false;
    }
  }
  return false;
}

/* always increment the serial at the end, which
   maintains the invt that every phase has a player
   motion in 'events.' Finally, call postanimate
   to add in winning or death events. */
template<bool ANIMATING, class DAB>
void Moving::PostAnimate(Level *level,
                         DAB *ctx, AList *&events, AList **&etail) {
  /* make sure there is animation for everything */
  ctx->serialup(level, etail);

  int lx, ly; dir from;
  if (level->IsDead(lx, ly, from)) {
    /* XXX or affect? */

    /* lite up laser tile (lx, ly), too  ... if laser death */
    AFFECTENT2016(B_PLAYER, []{});
    PUSHMOVE2016(lasered, ([&](lasered_t *e) {
        e->x = level->guyx;
        e->y = level->guyy;
        e->lx = lx;
        e->ly = ly;
        e->from = DirReverse(from);
      }));

    ctx->serialup(level, etail);

  } else if (level->IsWon()) {

    AFFECTENT2016(B_PLAYER, []{});

    PUSHMOVE2016(winner, ([&](winner_t *e) {
      e->x = level->guyx;
      e->y = level->guyy;
    }));

    ctx->serialup(level, etail);
  }
}

template<bool ANIMATING, class DAB>
bool Moving::MoveMaybeAnimate(Level *level,
                              dir d, DAB *ctx, AList *&events, AList **&etail) {
  ctx->clear();
  /* change our orientation, even if we don't move.
     TODO: animate this! */
  level->guyd = d;

  /* player always moves first */
  const bool player_moved = MoveEnt<ANIMATING, DAB>(
      level,
      d, -1, (Capabilities)GUYCAP, level->guyx, level->guyy,
      ctx, events, etail);

  if (player_moved) {
    for (int b = 0; b < level->nbots; b++) {

      const auto &[x, y] = level->Where(level->boti[b]);

      dir bd = DIR_NONE; /* dir to go */
      dir bd2 = DIR_NONE; /* second choice */
      Capabilities bcap = (Capabilities)0; /* its capabilities */

      if (Level::IsBomb(level->bott[b])) {
        /* bombs never move */
        bd = DIR_NONE;

        if (level->bota[b] == 0) {
          /* time's up: explodes */
          Bombsplode<ANIMATING, DAB>(level, b, b, ctx, events, etail);
        } else if (level->bota[b] > 0) {
          /* fuse burns */
          level->bota[b]--;
          /* XXX animate? */
        } else {
          /* unlit: do nothing */
        }
        continue;

      } else {
        switch (level->bott[b]) {
          /* nb, not isbomb */
        case B_BOMB_X:
          /* disappear */
          level->bott[b] = B_DELETED;
          /* no animation since it's already invisible */
          bd = DIR_NONE;
          break;

          /* these two are the same except for their
             capabilities */
        case B_HUGBOT:
        case B_DALEK: {
          /* dalek always moves towards player, favoring
             left/right movement */

          if (x == level->guyx) {
            /* same column? move up/down */
            if (y < level->guyy) bd = DIR_DOWN;
            else if (y > level->guyy) bd = DIR_UP;
            else bd = DIR_NONE; /* on player !! EXTERMINATE */
          } else {
            if (x > level->guyx) bd = DIR_LEFT;
            else bd = DIR_RIGHT;

            /* but still set second choice */
            if (y < level->guyy) bd2 = DIR_DOWN;
            else if (y > level->guyy) bd2 = DIR_UP;
          }

          switch (level->bott[b]) {
          default: /* impossible */
          case B_DALEK: bcap = DALEKCAP; break;
          case B_HUGBOT: bcap = HUGBOTCAP; break;
          }
          break;
        }
        default: bd = DIR_NONE;
          break;
        }
      }

      if (bd != DIR_NONE) {
        const bool bot_moved = MoveEnt<ANIMATING, DAB>(
            level,
            bd, b, bcap, x, y, ctx, events, etail);

        /* try second choice */
        if (!bot_moved && bd2 != DIR_NONE) {
          MoveEnt<ANIMATING, DAB>(level,
                                  bd2, b, bcap, x, y,
                                  ctx, events, etail);
        }
      }
    }

    PostAnimate<ANIMATING, DAB>(level, ctx, events, etail);
    return true;
  } else {
    PostAnimate<ANIMATING, DAB>(level, ctx, events, etail);
    return false;
  }
}

bool Moving::Move(Level *level, dir d) {
  NullDisamb ctx;
  AList *events = nullptr;
  AList **tail = &events;
  return MoveMaybeAnimate<false, NullDisamb>(level, d, &ctx, events, tail);
}

bool Moving::MoveAnimate(Level *level, dir d, Disamb *ctx, AList *&events) {
  AList **tail = &events;
  return MoveMaybeAnimate<true, Disamb>(level, d, ctx, events, tail);
}


void Moving::Warp(Level *level, int &entx, int &enty, int targx, int targy) {
  int target = level->TileAt(targx, targy);

  CheckStepOff(level, entx, enty);
  /* XXX can be incorrect --
     need to use SETENTPOS, since we need to reflect
     this immediately in the boti array
     (so that serialup knows where the bot is standing) */
  entx = targx;
  enty = targy;

  switch (target) {
  case T_PANEL:
    level->SwapO(level->DestAt(targx,targy));
    break;
  default:
    break;
  }
}

/* generate callable version of this macro */
void Moving::CheckStepOff(Level *level, int x, int y) {
  using DAB = NullDisamb;
  NullDisamb ctx_;
  DAB *ctx = &ctx_;
  AList *events = nullptr;
  AList **etail = &events;
  static constexpr bool ANIMATING = false;
  CHECKSTEPOFF2016(x, y);
}
