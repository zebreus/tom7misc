// Interactive karate test.

#include "karate.h"

#include <iostream>
#include <cstdio>
#include <string>

#include "arcfour.h"
#include "randutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"

using namespace std;

static string RandomHeight(ArcFour *rc) {
  switch (RandTo(rc, 3)) {
  default:
  case 0: return "HIGH";
  case 1: return "MEDIUM";
  case 2: return "LOW";
  }
}

static string RandomAttack(ArcFour *rc) {
  switch (RandTo(rc, 3)) {
  default:
  case 0: return "PUNCH " + RandomHeight(rc);
  case 1: return "KICK " + RandomHeight(rc);
  case 2: return "BLOCK " + RandomHeight(rc);
  }
}

static string RandomDir(ArcFour *rc) {
  switch (RandTo(rc, 3)) {
  default:
  case 0: return "LEFT";
  case 1: return "NEUTRAL";
  case 2: return "RIGHT";
  }
}

static string RandomMove(ArcFour *rc) {
  // TODO: Jump
  return "MOVE " + RandomDir(rc);
}

static string RandomAction(ArcFour *rc) {
  switch (RandTo(rc, 2)) {
  default:
  case 0: return RandomAttack(rc);
  case 1: return RandomMove(rc);
  }
}

int main(int argc, char **argv) {
  AnsiInit();

  Karate karate("stdin", "random");

  ArcFour rc("karate");

  for (;;) {

    string say1, say2;
    printf("%s says: ", karate.Fighter1().c_str());
    std::getline(cin, say1);
    say2 = StringPrintf("%08llx", Rand64(&rc));
    printf("%s says: %s\n", karate.Fighter2().c_str(), say2.c_str());
    string do1, do2;
    printf("%s: ", karate.Fighter1().c_str());
    std::getline(cin, do1);
    do2 = RandomAction(&rc);
    printf("%s: %s\n", karate.Fighter2().c_str(), do2.c_str());

    karate.Process(say1, say2, do1, do2);
    printf(AGREY("%s") "\n", karate.GetStatus().c_str());
  }

  return 0;
}
