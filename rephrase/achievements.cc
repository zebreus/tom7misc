
#include "achievements.h"

#include <cstdio>
#include <string>
#include <memory>
#include <unordered_set>

#include "util.h"
#include "image.h"
#include "ansi.h"

static constexpr const char *ACHIEVEMENTS_FILE = "achievements.txt";
static constexpr const char *TROPHY_FILE = "trophy.png";

namespace {

struct AchievementsImpl : public Achievements {
  std::unordered_set<std::string> achieved;
  std::unique_ptr<ImageRGBA> trophy;

  AchievementsImpl() {
    for (const std::string &a : Util::ReadFileToLines(ACHIEVEMENTS_FILE)) {
      achieved.insert(a);
    }
    trophy.reset(ImageRGBA::Load(TROPHY_FILE));
  }

  void Achieve(const std::string &name,
               const std::string &description) override {
    if (!achieved.contains(name)) {
      if (trophy.get() != nullptr) {
        for (int y = 0; y < trophy->Height(); y++) {
          printf("  ");
          for (int x = 0; x < trophy->Width(); x++) {
            const auto &[r, g, b, a] = trophy->GetPixel(x, y);
            if (a > 127) {
              printf("%s██" ANSI_RESET,
                     ANSI::ForegroundRGB(r, g, b).c_str());
            } else {
              printf("  ");
            }
          }
          printf("\n");
        }
      }

    printf("\n"
           AWHITE("       Achievement Unlocked!") "\n"
           ACYAN("          ** %s **") "\n"
           "%s\n\n",
           name.c_str(), description.c_str());

    FILE *f = fopen(ACHIEVEMENTS_FILE, "ab");
    fprintf(f, "%s\n", name.c_str());
    fclose(f);
    }
  }
};

}  // namespace


Achievements::Achievements() {

}

Achievements &Achievements::Get() {
  static Achievements *singleton = new AchievementsImpl;
  return *singleton;
}
