#ifndef _ESCAPE_MOVING_H
#define _ESCAPE_MOVING_H

#include "disamb.h"
#include "level-base.h"
#include "level.h"

#include "aevent.h"

struct Moving {

  // Try moving in the direction, modifying the level and returning true if
  // this is possible.
  static bool Move(Level *level, dir d);

  // see animation.h for documentation.
  static bool MoveAnimate(Level *level, dir d, Disamb *ctx, PtrList<AEvent> *&events);

  // Go straight to the target. no animation.
  static void Warp(Level *level, int &entx, int &enty, int targx, int targy);

 private:
  static void CheckStepOff(Level *level, int x, int y);

  using AList = PtrList<AEvent>;

  // XXX: Perhaps this stuff can just be static linkage.

  template<bool ANIMATING, class DAB>
  static void PostAnimate(Level *level,
                          DAB *ctx, AList *&events, AList **&etail);

  /* pass the entity index, or -1 for the player */
  template<bool ANIMATING, class DAB>
  static bool MoveEnt(Level *level,
                      dir d, int enti, Capabilities cap,
                      int entx, int enty,
                      DAB *ctx, AList *&events, AList **&etail);

  template<bool ANIMATING, class DAB>
  static void Bombsplode(Level *level,
                         int now,
                         int bombi, DAB *ctx, AList *&events,
                         AList **& etail);

  // n.b. Not all of these have the same signature!

  template<bool ANIMATING, class DAB>
  static bool MoveEntTransport(Level *level,
                               dir d, int enti, Capabilities cap,
                               int entx, int enty, int newx, int newy,
                               DAB *ctx, AList *&events,
                               AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntExit(Level *level,
                          dir d, int enti, Capabilities cap,
                          int entx, int enty, int newx, int newy,
                          DAB *ctx, AList *&events,
                          AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntOn(Level *level,
                        dir d, int enti, Capabilities cap,
                        int entx, int enty, int newx, int newy,
                        DAB *ctx, AList *&events,
                        AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntGoldlike(Level *level,
                              int target, dir d, int enti, Capabilities cap,
                              int entx, int enty, int newx, int newy,
                              DAB *ctx, AList *&events,
                              AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntButton(Level *level,
                            dir d, int enti, Capabilities cap,
                            int entx, int enty, int newx, int newy,
                            DAB *ctx, AList *&events,
                            AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntHeartframer(Level *level,
                                 dir d, int enti, Capabilities cap,
                                 int entx, int enty, int newx, int newy,
                                 DAB *ctx, AList *&events,
                                 AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntElectric(Level *level,
                              dir d, int enti, Capabilities cap,
                              int entx, int enty, int newx, int newy,
                              DAB *ctx, AList *&events,
                              AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntBroken(Level *level,
                            dir d, int enti, Capabilities cap,
                            int entx, int enty, int newx, int newy,
                            DAB *ctx, AList *&events,
                            AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntGreen(Level *level,
                           dir d, int enti, Capabilities cap,
                           int entx, int enty, int newx, int newy,
                           DAB *ctx, AList *&events,
                           AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntFloorlike(Level *level,
                               int target, dir d, int enti, Capabilities cap,
                               int entx, int enty, int newx, int newy,
                               DAB *ctx, AList *&events,
                               AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntSteel(Level *level,
                           int target, dir d, int enti, Capabilities cap,
                           int entx, int enty, int newx, int newy,
                           DAB *ctx, AList *&events,
                           AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEnt01(Level *level,
                        int target, dir d, int enti, Capabilities cap,
                        int entx, int enty, int newx, int newy,
                        DAB *ctx, AList *&events,
                        AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveEntPushable(Level *level,
                              int target, dir d, int enti, Capabilities cap,
                              int entx, int enty, int newx, int newy,
                              DAB *ctx, AList *&events,
                              AList **&etail);

  template<bool ANIMATING, class DAB>
  static bool MoveMaybeAnimate(Level *level,
                               dir d, DAB *ctx, AList *&events, AList **&etail);

  Moving() = delete;
};

#endif
