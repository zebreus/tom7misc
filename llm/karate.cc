#include "karate.h"

#include <string>

#include "base/logging.h"
#include "base/stringprintf.h"

#include "re2/re2.h"

using namespace std;

using Aim = Karate::Aim;
using Dir = Karate::Dir;
using Attack = Karate::Attack;
using enum Karate::Aim;
using enum Karate::Dir;
using enum Karate::Attack;

Karate::Karate(const std::string &fighter1, const std::string &fighter2) : fighter1(fighter1), fighter2(fighter2) {

}

string PosString(int x, int y) {
  if (y > 0) {
    return StringPrintf("standing on the %dm marker", x);
  } else {
    return StringPrintf("in the air above the %dm marker", x);
  }
}

string Karate::GetStatus() {
  string ret = StringPrintf(
      "Score: %s (%d)  %s (%d)\n"
      "%s is %s, facing right.\n"
      "%s is %s, facing left.\n",
      fighter1.c_str(), score1, fighter2.c_str(), score2,
      fighter1.c_str(), PosString(x1, y1).c_str(),
      fighter2.c_str(), PosString(x2, y2).c_str());
  // At most one of these should be true.
  if (parry1)
    StringAppendF(&ret, "%s just had a successful parry.\n", fighter1.c_str());
  if (parry2)
    StringAppendF(&ret, "%s just had a successful parry.\n", fighter2.c_str());

  return ret;
}

#define AIM_RE2 "HIGH|MEDIUM|LOW"
#define DIR_RE2 "LEFT|NEUTRAL|RIGHT"

static std::optional<Aim> GetAim(const std::string &line) {
  if (line == "HIGH") return {HIGH};
  else if (line == "MEDIUM") return {MEDIUM};
  else if (line == "LOW") return {LOW};
  return nullopt;
}

static std::optional<Dir> GetDir(const std::string &line) {
  if (line == "LEFT") return {LEFT};
  else if (line == "NEUTRAL") return {NEUTRAL};
  else if (line == "RIGHT") return {RIGHT};
  return nullopt;
}

std::optional<Aim> Karate::GetBlock(const std::string &line) {
  string aim;
  if (RE2::FullMatch(line, "BLOCK (" AIM_RE2 ")", &aim))
    return GetAim(aim);
  return nullopt;
}

std::optional<Aim> Karate::GetPunch(const std::string &line) {
  string aim;
  if (RE2::FullMatch(line, "PUNCH (" AIM_RE2 ")", &aim))
    return GetAim(aim);
  return nullopt;
}

std::optional<Aim> Karate::GetKick(const std::string &line) {
  string aim;
  if (RE2::FullMatch(line, "KICK (" AIM_RE2 ")", &aim))
    return GetAim(aim);
  return nullopt;
}

std::optional<std::pair<Attack, Aim>> Karate::GetAttack(const std::string &line) {
  if (auto po = GetPunch(line)) return {make_pair(PUNCH, po.value())};
  else if (auto ko = GetKick(line)) return {make_pair(KICK, po.value())};
  else return nullopt;
}

std::optional<Dir> Karate::GetMove(const std::string &line) {
  string dir;
  if (RE2::FullMatch(line, "MOVE (" DIR_RE2 ")", &dir))
    return GetDir(dir);
  return nullopt;
}

std::optional<Dir> Karate::GetJump(const std::string &line) {
  string dir;
  if (RE2::FullMatch(line, "JUMP (" DIR_RE2 ")", &dir))
    return GetDir(dir);
  return nullopt;
}

static int DirValue(std::optional<Dir> od) {
  if (!od.has_value()) return 0;
  switch (od.value()) {
  default:
  case LEFT: return -1;
  case NEUTRAL: return 0;
  case RIGHT: return +1;
  }
}

string Karate::Process(const std::string &say1, const std::string &say2,
                       const std::string &do1, const std::string &do2) {

  // First, move players
  const auto mo1 = GetMove(do1);
  const auto mo2 = GetMove(do2);
  // TODO: Implied move from a diagonal jump on the previous move.

  // Process movement first, possibly ending due to ringout.
  if (mo1.has_value() || mo2.has_value()) {
    parry1 = parry2 = false;
    // Simultaneous movement.
    // If the players walk into each other,
    // the action fails.
    int nx1 = x1 + DirValue(mo1);
    int nx2 = x2 + DirValue(mo2);
    if (nx1 < nx2) {
      // Success
      x1 = nx1;
      x2 = nx2;
    }
    if (x1 < 0 && x2 >= 5) {
      x1 = 1;
      x2 = 3;
      return "Clash: Fighters stepped out of the ring simultaneously.";
    }

    if (x1 < 0) {
      score2++;
      return "Fighter 1 stepped out of the ring.";
    }
    if (x2 >= 5) {
      score1++;
      return "Fighter 2 stepped out of the ring.";
    }

    // Otherwise, continue in new position.
  }

  auto bo1 = GetBlock(do1);
  auto bo2 = GetBlock(do2);

  // TODO: Mid-air blocking not allowed.


  return "";
}

