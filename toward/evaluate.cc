
#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "eval.h"
#include "font-db.h"
#include "letters.h"
#include "map-util.h"
#include "periodically.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "atomic-util.h"

DECLARE_COUNTERS(ctr_invalid, ctr_discarded);

static constexpr int NUM_THREADS = 12;

// TODO: What's up with these? I looked at Rodchenko and it looks
// perfectly decent, but the thread gets stuck on it.
const std::unordered_set<std::string> &Banned() {
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

struct Evaluated {
  std::string fontname;
  std::array<double, 26> stabs = {};
  double avg = 0.0;
  bool valid = true;
};

static bool HasFailure(const Evaluated &e) {
  for (double v : e.stabs) {
    if (v >= 1000.0) return true;
  }
  return false;
}

static StatusBar *status = nullptr;

static void SetAction(int thread_idx, std::string_view s) {
  status->LineStatus(thread_idx, "{}", s);
}

static Evaluated Evaluate(std::string_view filename) {
  Evaluated ret;
  ret.fontname = std::string(filename);

  std::unique_ptr<Letters> letters = Letters::LoadFont(filename);
  if (letters.get() == nullptr) {
    ret.valid = false;
    return ret;
  }

  for (int i = 0; i < 26; i++) {
    auto it = letters->letter.find('A' + i);
    if (it == letters->letter.end()) {
      ret.valid = false;
      continue;
    }

    const Letter &letter = it->second;
    ret.stabs[i] = Eval::Stability(letter);
  }

  double total = 0.0;
  for (double &s : ret.stabs) total += s;
  ret.avg = total / ret.stabs.size();
  return ret;
}

static void EvaluateAll() {
  std::unique_ptr<FontDB> db = FontDB::Create("../fontdb/font-db.txt");

  status->Print("Num sorted: {}\n", db->NumSorted());

  const std::vector<std::pair<std::string, FontDB::Info>> files =
    MapToSortedVec(db->Files());

  #if 0
  // Debuggin'
  for (const auto &[fontname, info] : files) {
    if (info.type != FontDB::Type::BROKEN) {
      Print("{}...\n", fontname);
      (void)Evaluate(fontname);
    }
  }
  #endif

  std::mutex m;
  Timer timer;
  Periodically status_per(1.0);
  int64_t next_idx = 0;

  std::vector<Evaluated> evaled;

  ParallelFan(
      NUM_THREADS,
      [&](int thread_idx) {
        for (;;) {
          std::string fontname;
          {
            MutexLock ml(&m);

            if (next_idx >= files.size()) {
              SetAction(thread_idx, "Done");
              return;
            }

            const FontDB::Type t = files[next_idx].second.type;
            fontname = files[next_idx].first;

            next_idx++;

            // Don't even run certain types.
            if (t != FontDB::Type::SANS) {
              continue;
            }
          }

          if (Banned().contains(fontname)) {
            status->Print(ARED("BANNED") " {}\n", fontname);
            ctr_invalid++;
            continue;
          }

          status->LineStatus(thread_idx, "[" ACYAN("{}") "] {}",
                             thread_idx, fontname);
          Evaluated e = Evaluate(fontname);
          if (e.valid) {
            if (HasFailure(e)) {
              status->Print("Discarded: {}\n", fontname);
              ctr_discarded++;
            } else {
              MutexLock ml(&m);
              evaled.push_back(std::move(e));
            }
          } else {
            status->Print("Invalid: {}\n", fontname);
            ctr_invalid++;
          }

          status_per.RunIf([&]{
              MutexLock ml(&m);
              std::string bar =
                ANSI::ProgressBar(next_idx, files.size(),
                                  std::format(
                                      ARED("{}") " invalid, "
                                      AORANGE("{}") " discarded, "
                                      AGREEN("{}") " evaluated",
                                      ctr_invalid.Read(),
                                      ctr_discarded.Read(),
                                      evaled.size()),
                                  timer.Seconds());
              status->LineStatus(NUM_THREADS, "{}", bar);
            });
        }
      });

  status->Print("All done.\n");

  std::sort(evaled.begin(), evaled.end(),
            [](const Evaluated &a, const Evaluated &b) {
              return a.avg < b.avg;
            });

  status->Print("\nTop 10 most stable fonts:\n");
  for (size_t i = 0; i < 10 && i < evaled.size(); i++) {
    status->Print("{}. {} ({:.4f})\n", i + 1,
                  evaled[i].fontname, evaled[i].avg);
  }

  status->Print("\nTop 10 least stable fonts:\n");
  for (size_t i = 0; i < 10 && i < evaled.size(); i++) {
    size_t idx = evaled.size() - 1 - i;
    status->Print("{}. {} ({:.4f})\n", i + 1,
                  evaled[idx].fontname, evaled[idx].avg);
  }

  struct LetterResult {
    char c;
    std::string_view fontname;
    double stab;
  };

  std::vector<LetterResult> all_letters;
  all_letters.reserve(evaled.size() * 26);
  for (const auto &e : evaled) {
    for (int i = 0; i < 26; i++) {
      all_letters.push_back({(char)('A' + i), e.fontname, e.stabs[i]});
    }
  }

  std::sort(all_letters.begin(), all_letters.end(),
            [](const LetterResult &a, const LetterResult &b) {
              return a.stab > b.stab;
            });

  status->Print("\nTop 10 least stable letters overall:\n");
  for (size_t i = 0; i < 10 && i < all_letters.size(); i++) {
    status->Print("{}. {} in {} ({:.4f})\n", i + 1, all_letters[i].c,
                  all_letters[i].fontname, all_letters[i].stab);
  }

  std::array<double, 26> letter_totals = {};
  if (!evaled.empty()) {
    for (const Evaluated &e : evaled) {
      for (int i = 0; i < 26; i++) {
        letter_totals[i] += e.stabs[i];
      }
    }

    struct AvgLetterResult {
      char c = 0;
      double avg = 0.0;
    };

    std::vector<AvgLetterResult> avg_letters;
    avg_letters.reserve(26);
    for (int i = 0; i < 26; i++) {
      avg_letters.push_back({(char)('A' + i),
                             letter_totals[i] / (double)evaled.size()});
    }

    std::sort(avg_letters.begin(), avg_letters.end(),
              [](const AvgLetterResult &a, const AvgLetterResult &b) {
                return a.avg < b.avg;
              });

    status->Print("\nAverage stability per letter (most to least stable):\n");
    for (const AvgLetterResult &r : avg_letters) {
      status->Print("{:c}: {:.4f}\n", r.c, r.avg);
    }
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  status = new StatusBar(NUM_THREADS + 1);
  // Evaluate("helveticab.ttf");
  EvaluateAll();

  return 0;
}
