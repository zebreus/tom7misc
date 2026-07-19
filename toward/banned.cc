#include "banned.h"

#include <string>
#include <unordered_set>

// TODO: What's up with these? I looked at Rodchenko and it looks
// perfectly decent, but the thread gets stuck on it.
const std::unordered_set<std::string> &BannedFonts() {
  static std::unordered_set<std::string> *BANNED = new std::unordered_set<std::string>{
    "d:\\temp\\fonts2020\\Google_Fonts_2017\\ofl\\rajdhani\\Rajdhani-Light.ttf",
    "d:\\temp\\fonts2020\\Google_Fonts_2017\\ofl\\rajdhani\\Rajdhani-SemiBold.ttf",
    "d:\\temp\\fonts2020\\Fonts\\R\\TrueType\\Rodchenko Regular.ttf",
    "d:\\temp\\fonts2020\\Google_Fonts_2017\\ofl\\rajdhani\\Rajdhani-Medium.ttf",
    "d:\\temp\\fonts2020\\Fonts\\C\\TrueType\\Calico.ttf",
    "d:\\temp\\fonts2020\\Fonts\\C\\TrueType\\Calico Italic.ttf",
    "d:\\temp\\fonts2020\\Fonts\\C\\TrueType\\Calico(1).ttf",
  };

  return *BANNED;
};
