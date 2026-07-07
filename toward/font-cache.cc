
#include "font-cache.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "letters.h"
#include "threadutil.h"
#include "util.h"

static std::mutex m;

// Keyed by filename.
static std::unordered_map<std::string, Letters *> *cache =
  nullptr;

const Letters *FontCache::Get(std::string_view font_name) {
  static std::unordered_map<std::string, std::string_view> fontfiles = {
    {"helvetica-bold", "helveticab.ttf"},
    {"helvetica", "helvetica.ttf"},
    {"baskerville-bold", "baskervilleb.ttf"},
    // papyrus.ttf has self-intersecting geometry, omg!
    {"papyrus-regular", "papyrus-icg.ttf"},
    {"papyrus", "papyrus-icg.ttf"},
    {"comicsansms", "comic-sans-ms.ttf"},
    {"timesnewromanps-boldmt", "timesb.ttf"},
    {"georgia", "georgia.ttf"},
    {"georgia-italic", "georgiai.ttf"},
    {"adlib", "adlib-bt.ttf"},
    {"franklingothic-medium", "franklingothicdemi.ttf"},
    {"fixedersys1x", "fixedersys1x.ttf"},
    {"avantgardef-bold", "avantgardeb.ttf"},
  };

  auto fit = fontfiles.find(Util::lcase(font_name));
  CHECK(fit != fontfiles.end()) << "Unknown font: " << font_name;
  std::string key{fit->second};

  MutexLock ml(&m);
  if (cache == nullptr)
    cache = new std::unordered_map<std::string, Letters *>();

  // We use the filename as the key to avoid loading something with
  // an alias twice.

  auto it = cache->find(key);
  if (it != cache->end()) return it->second;

  std::unique_ptr<Letters> letters = Letters::LoadFont(fit->second);
  CHECK(letters.get() != nullptr) << "Couldn't load font: " << key;

  Letters *ret = letters.release();
  (*cache)[key] = ret;
  return ret;
}
