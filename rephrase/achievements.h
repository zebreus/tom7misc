
#ifndef _REPHRASE_ACHIEVEMENTS_H
#define _REPHRASE_ACHIEVEMENTS_H

#include <string>

struct Achievements {
  static Achievements &Get();

  virtual void Achieve(const std::string &name,
                       const std::string &description) = 0;

 protected:
  // Use singleton.
  Achievements();
};

#endif
