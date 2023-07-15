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

Karate::Karate(const std::string &fighter1, const std::string &fighter2) :
  fighter1(fighter1), fighter2(fighter2) {
}

void Karate::Reset() {
  x1 = 1;
  x2 = 3;
  jump1 = jump2 = nullopt;
  parry1 = parry2 = false;
}

string PosString(int x, std::optional<Dir> jump) {
  if (!jump.has_value()) {
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
      fighter1.c_str(), PosString(x1, jump1).c_str(),
      fighter2.c_str(), PosString(x2, jump2).c_str());
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
  else if (auto ko = GetKick(line)) return {make_pair(KICK, ko.value())};
  else if (auto bo = GetBlock(line)) return {make_pair(BLOCK, bo.value())};
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

Karate::ProcessResult Karate::Process(
    const std::string &say1, const std::string &say2,
    const std::string &do1, const std::string &do2) {
  bool prev_parry1 = parry1, prev_parry2 = parry2;
  parry1 = parry2 = false;

  optional<Dir> old_jump1 = jump1, old_jump2 = jump2;
  jump1 = nullopt;
  jump2 = nullopt;

  ProcessResult res;

  printf("Process %s|%s|%s|%s\n", say1.c_str(), say2.c_str(), do1.c_str(), do2.c_str());

  // First, move fighters
  auto mo1 = GetMove(do1);
  auto mo2 = GetMove(do2);

  // If there is a jump in progress, it overrides any explicit move.
  if (old_jump1.has_value()) mo1 = old_jump1;
  if (old_jump2.has_value()) mo2 = old_jump2;

  // A new jump is essentially like a movement, but it also puts
  // the fighter in the air, and queues up a movement for the next
  // turn.
  auto jo1 = GetJump(do1);
  auto jo2 = GetJump(do2);
  // Jump is ignored if already in the air.
  if (old_jump1.has_value()) jo1 = nullopt;
  if (old_jump2.has_value()) jo2 = nullopt;
  // Otherwise, treat it like a movement.
  if (jo1.has_value()) mo1 = jo1;
  if (jo2.has_value()) mo2 = jo2;

  // Process movement first, possibly ending due to ringout.
  if (mo1.has_value() || mo2.has_value()) {
    // Simultaneous movement.
    // If the fighters walk into each other,
    // the action fails.
    int nx1 = x1 + DirValue(mo1);
    int nx2 = x2 + DirValue(mo2);
    if (nx1 < nx2) {
      // Success
      x1 = nx1;
      x2 = nx2;
      jump1 = jo1;
      jump2 = jo2;
    }

    res.f1.x = x1;
    res.f1.in_air = old_jump1.has_value() || jo1.has_value();
    res.f2.x = x2;
    res.f2.in_air = old_jump2.has_value() || jo2.has_value();

    // XXX should still be able to jump if the space is occupied.
    // in fact, if just one fighter is jumping, they should win the race.

    if (x1 < 0 && x2 >= 5) {
      Reset();
      res.message = "Fighters stepped out of the ring simultaneously.";
      res.clash = true;
      res.reset = true;
      return res;
    }

    if (x1 < 0) {
      score2++;
      Reset();
      res.reset = true;
      res.message = "Fighter 1 stepped out of the ring.";
      return res;
    }
    if (x2 >= 5) {
      score1++;
      Reset();
      res.reset = true;
      res.message = "Fighter 2 stepped out of the ring.";
      return res;
    }

    // Otherwise, continue in new position.
  }

  auto ao1 = GetAttack(do1);
  auto ao2 = GetAttack(do2);

  // No mid-air punching or blocking.
  // XXX what to do about kick aims in air?
  if (old_jump1.has_value() &&
      ao1.has_value() && std::get<0>(ao1.value()) != KICK)
    ao1 = nullopt;
  if (old_jump2.has_value() &&
      ao2.has_value() && std::get<0>(ao2.value()) != KICK)
    ao2 = nullopt;

  // If the fighters are not within striking distance, attacks do nothing.
  if (std::abs(x1 - x2) <= 1) {
    if (ao1.has_value() && ao2.has_value()) {
      // Simultaneous attack.
      auto [a1, aim1] = ao1.value();
      auto [a2, aim2] = ao2.value();

      // If exactly one fighter is in the air, their kick beats any
      // attack. (XXX but can be blocked high)
      if (old_jump1.has_value() &&
          !old_jump2.has_value()) {
        CHECK(a1 == KICK);
        score1 += 2;
        res.message = "Fighter 1 lands a jump kick.";
        return res;
      }
      if (old_jump2.has_value() &&
          !old_jump1.has_value()) {
        CHECK(a2 == KICK);
        score2 += 2;
        res.message = "Fighter 2 lands a jump kick.";
        return res;
      }

      // Otherwise, both fighters are in the air or both
      // are on the ground, so regular rules apply. Some
      // situations will be impossible in the air, but
      // it doesn't matter.

      // Punch wins.
      if (a1 == PUNCH && a2 == KICK) {
        score1++;
        res.message = "Fighter 1 lands a quick punch.";
        return res;
      }
      if (a2 == PUNCH && a1 == KICK) {
        score2++;
        res.message = "Fighter 2 lands a quick punch.";
        return res;
      }

      // blocks here
      if (a1 == BLOCK && a2 == BLOCK) {
        res.message = "Both fighters block, so nothing happens.";
        return res;
      }

      if (a1 == BLOCK && aim2 == aim1) {
        parry1 = true;
        res.message = "Fighter 1 successfully parried.";
        return res;
      }

      if (a2 == BLOCK && aim1 == aim2) {
        parry2 = true;
        res.message = "Fighter 2 successfully parried.";
        return res;
      }

      // Otherwise, blocks do nothing.

      CHECK(a1 == a2);
      CHECK(!(prev_parry2 && prev_parry2));
      if (prev_parry1) {
        if (a1 == PUNCH) {
          score1++;
          res.message = "Fighter 1 lands a parry-punch combo.";
          return res;
        } else {
          score1 += 2;
          res.message = "Fighter 1 lands a parry-kick combo.";
          return res;
        }
      }
      if (prev_parry2) {
        if (a2 == PUNCH) {
          score2++;
          res.message = "Fighter 2 lands a parry-punch combo.";
          return res;
        } else {
          score2 += 2;
          res.message = "Fighter 2 lands a parry-kick combo.";
          return res;
        }
      }

      // Should this reset?
      res.clash = true;
      res.message = "Clash: Both fighters attacked simultaneously.";
      return res;
    }

    // XXX below, we should interpret a low kick for a jumping
    // fighter as high (and discard other attacks, which inherently
    // miss). We should also discard medium and low attacks for
    // the fighter on the ground.

    // Now we have some asymmetric attack (or no attack).
    if (ao1.has_value()) {
      const auto &[a1, aim1] = ao1.value();
      CHECK(!ao2.has_value());

      if (a1 == PUNCH) {
        score1++;
        res.message = "Fighter 1 lands a punch.";
        return res;
      } else {
        score1 += 2;
        res.message = "Fighter 1 lands a kick.";
        return res;
      }
    }

    if (ao2.has_value()) {
      const auto &[a2, aim2] = ao2.value();
      CHECK(!ao1.has_value());


      if (a2 == PUNCH) {
        score2++;
        res.message = "Fighter 2 lands a punch.";
        return res;
      } else {
        score2 += 2;
        res.message = "Fighter 2 lands a kick.";
        return res;
      }
    }

  }

  return res;
}

