
#include "achievements.h"

#include <cstdio>
#include <string>
#include <memory>
#include <unordered_set>

#include "ansi.h"
#include "base/print.h"
#include "base64.h"
#include "image.h"
#include "util.h"

static constexpr const char *ACHIEVEMENTS_FILE = "achievements.txt";

// From trophy.png, converted manually.
static constexpr const char *TROPHY_PNG =
"iVBORw0KGgoAAAANSUhEUgAAABAAAAASBAMAAACgFUNZAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJ"
"bWFnZVJlYWR5ccllPAAAAyZpVFh0WE1MOmNvbS5hZG9iZS54bXAAAAAAADw/eHBhY2tldCBiZWdp"
"bj0i77u/IiBpZD0iVzVNME1wQ2VoaUh6cmVTek5UY3prYzlkIj8+IDx4OnhtcG1ldGEgeG1sbnM6"
"eD0iYWRvYmU6bnM6bWV0YS8iIHg6eG1wdGs9IkFkb2JlIFhNUCBDb3JlIDkuMS1jMDAyIDc5LmRi"
"YTNkYTNiNSwgMjAyMy8xMi8xNS0xMDo0MjozNyAgICAgICAgIj4gPHJkZjpSREYgeG1sbnM6cmRm"
"PSJodHRwOi8vd3d3LnczLm9yZy8xOTk5LzAyLzIyLXJkZi1zeW50YXgtbnMjIj4gPHJkZjpEZXNj"
"cmlwdGlvbiByZGY6YWJvdXQ9IiIgeG1sbnM6eG1wPSJodHRwOi8vbnMuYWRvYmUuY29tL3hhcC8x"
"LjAvIiB4bWxuczp4bXBNTT0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wL21tLyIgeG1sbnM6"
"c3RSZWY9Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC9zVHlwZS9SZXNvdXJjZVJlZiMiIHht"
"cDpDcmVhdG9yVG9vbD0iQWRvYmUgUGhvdG9zaG9wIDI1LjYgKFdpbmRvd3MpIiB4bXBNTTpJbnN0"
"YW5jZUlEPSJ4bXAuaWlkOjdEQkEyNTI1RTg3RjExRUVCOTVCQUYzNjQyNTgwQUVBIiB4bXBNTTpE"
"b2N1bWVudElEPSJ4bXAuZGlkOjdEQkEyNTI2RTg3RjExRUVCOTVCQUYzNjQyNTgwQUVBIj4gPHht"
"cE1NOkRlcml2ZWRGcm9tIHN0UmVmOmluc3RhbmNlSUQ9InhtcC5paWQ6N0RCQTI1MjNFODdGMTFF"
"RUI5NUJBRjM2NDI1ODBBRUEiIHN0UmVmOmRvY3VtZW50SUQ9InhtcC5kaWQ6N0RCQTI1MjRFODdG"
"MTFFRUI5NUJBRjM2NDI1ODBBRUEiLz4gPC9yZGY6RGVzY3JpcHRpb24+IDwvcmRmOlJERj4gPC94"
"OnhtcG1ldGE+IDw/eHBhY2tldCBlbmQ9InIiPz5hom9IAAAAFVBMVEX///8EBgY5FhaDBwHLfAf/"
"3RX////VfigKAAAAAXRSTlMAQObYZgAAAGlJREFUCB0FwdFJA0AQBcA5fb/iXgqQkBLsH6xAtAML"
"ELObCs6Z2KCznq63Vb8fUaY8C9Byunrf75O17XPxE3Qhp8v4mzhTp79Fr5qXFjgIvV+/COfxNgSr"
"ENb1UiPseuwR6/3MrSd8gn/47Si90kv7XgAAAABJRU5ErkJggg==";

namespace {

struct AchievementsImpl : public Achievements {
  std::unordered_set<std::string> achieved;
  std::unique_ptr<ImageRGBA> trophy;

  AchievementsImpl() {
    for (const std::string &a : Util::ReadFileToLines(ACHIEVEMENTS_FILE)) {
      achieved.insert(a);
    }

    trophy.reset(ImageRGBA::LoadFromMemory(Base64::DecodeV(TROPHY_PNG)));
  }

  void Achieve(const std::string &name,
               const std::string &description) override {
    if (!achieved.contains(name)) {
      if (trophy.get() != nullptr) {
        for (int y = 0; y < trophy->Height(); y++) {
          Print("  ");
          for (int x = 0; x < trophy->Width(); x++) {
            const auto &[r, g, b, a] = trophy->GetPixel(x, y);
            if (a > 127) {
              Print("{}██" ANSI_RESET,
                    ANSI::ForegroundRGB(r, g, b));
            } else {
              Print("  ");
            }
          }
          Print("\n");
        }
      }

      Print("\n"
            AWHITE("       Achievement Unlocked!") "\n"
            ACYAN("          ** {} **") "\n"
            "{}\n\n",
            name, description);

      FILE *f = fopen(ACHIEVEMENTS_FILE, "ab");
      Print(f, "{}\n", name);
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
