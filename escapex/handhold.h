#ifndef _ESCAPE_HANDHOLD_H
#define _ESCAPE_HANDHOLD_H

struct HandHold {
  /* do something the first time the game is launched */
  static void FirstTime();
  static void Init();

  static void DidUpdate();
  static void DidUpgrade();

  static bool RecommendUpdate();
  static bool RecommendUpgrade();
};

#endif
