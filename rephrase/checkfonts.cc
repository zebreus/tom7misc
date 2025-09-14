// Code for running over all the fonts in the database,
// for example to test a change to the font library.

#include <cstdint>
#include <ctime>
#include <format>
#include <mutex>
#include <stdio.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "map-util.h"
#include "periodically.h"
#include "status-bar.h"
#include "stb_truetype.h"
#include "threadutil.h"
#include "util.h"

using namespace std;

using uint8 = uint8_t;
using int64 = int64_t;
using uint64 = uint64_t;

static constexpr const char *DIRS[] = {
  "d:\\temp\\fonts2020",
  // "d:\\temp\\fonts2020\\SummitSoftCreativeFonts",
};

static string Backslash(const string &s) {
  string ret;
  for (const char c : s)
    ret += (c == '/' ? '\\' : c);
  return ret;
}

static void AddAllFilesRec(const string &dir, vector<string> *all_files) {
  for (const string &f : Util::ListFiles(dir)) {
    const string filename = Util::DirPlus(dir, f);
    if (Util::isdir(filename)) {
      AddAllFilesRec(filename, all_files);
    } else {
      if (!filename.empty() &&
          // Should probably delete emacs backups..?
          filename[filename.size() - 1] != '#' &&
          filename[filename.size() - 1] != '~') {
        all_files->push_back(Backslash(filename));
      }
    }
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  std::vector<string> all_filenames;
  for (const char *d : DIRS) {
    AddAllFilesRec(d, &all_filenames);
  }

  auto IsTruetype = [](std::string_view filename) {
      // TODO: TTC (truetype collection) may also work,
      // although we only will load the first font.
      std::string s = Util::lcase(filename);
      return Util::EndsWith(s, ".ttf") ||
        Util::EndsWith(s, ".otf");
    };

  int64 start = time(nullptr);

  std::mutex out_m;
  std::unordered_map<string, int64> counters;
  std::vector<string> good, bad;

  std::unordered_map<int, int64_t> good_by_format;

  std::mutex bytes_m;
  int64 total_bytes = 0;
  int64 files_processed = 0;

  Periodically status_per(1.0);
  StatusBar status(1);
  status.Print("Num files: {}\n", all_filenames.size());

  Periodically spam_per(1.0);

  UnParallelApp(
      all_filenames,
      [&](const string &filename) {
        {
          MutexLock ml(&out_m);
          files_processed++;
        }

        status_per.RunIf([&]() {
            status.Progressf(files_processed, all_filenames.size(),
                             "Checkin' fonts");
          });

        if (!IsTruetype(filename)) {
          MutexLock ml(&out_m);
          counters["filename"]++;
          bad.push_back(filename);
          return;
        }

        vector<uint8> ttf_bytes = Util::ReadFileBytes(filename);
        if (ttf_bytes.empty()) {
          MutexLock ml(&out_m);
          counters["cant_read"]++;
          status.Print("Can't read: {}\n", filename);
          bad.push_back(filename);
          return;
        }

        {
          MutexLock ml(&bytes_m);
          total_bytes += ttf_bytes.size();
        }

        // Print("{}\n", filename);
        // fflush(stdout);

        stbtt_fontinfo font;
        int offset = stbtt_GetFontOffsetForIndex(ttf_bytes.data(), 0);
        if (offset == -1) {
          MutexLock ml(&out_m);
          bad.push_back(filename);
          if (spam_per.ShouldRun()) {
            status.Print("Bad offset in {}", filename);
          }
          counters["bad_offset"]++;
          return;
        }

        if (!stbtt_InitFont(&font,
                            ttf_bytes.data(), ttf_bytes.size(), offset)) {
          MutexLock ml(&out_m);
          bad.push_back(filename);
          if (spam_per.ShouldRun()) {
            status.Print("Can't init {}", filename);
          }
          counters["cant_init"]++;
          return;
        }

        // More checks here...

        int ascent, descent;
        stbtt_GetFontVMetrics(&font, &ascent, &descent, nullptr);
        if ((ascent - descent) <= 0) {
          MutexLock ml(&out_m);
          if (spam_per.ShouldRun()) {
            status.Print("Bad vmetrics in {}", filename);
          }
          counters["bad_vmetrics"]++;
          bad.push_back(filename);
          return;
        }

        {
          int format_type = stbtt_GetEncodingFormat(&font);

          std::unordered_map<uint16_t, std::vector<uint32_t>>
            codepoint_from_glyph = stbtt_GetGlyphs(&font);

          bool have_s = stbtt_FindGlyphIndex(&font, 's') != 0;

          if (codepoint_from_glyph.empty()) {
            MutexLock ml(&out_m);
            if (spam_per.ShouldRun()) {
              status.Print("No glyphs in {}", filename);
            }
            counters[std::format("no_glyphs({})({})", format_type,
                                 have_s ? "have_s" : "no_s")]++;
            bad.push_back(filename);
            return;
          }

          std::unordered_set<uint32_t> codepoints_found;
          for (const auto &[glyph, codepoints] : codepoint_from_glyph) {
            // Note that this used to expect a single codepoint. At
            // some point I updated the API to return all the codepoints
            // that use a glyph, but I didn't really check this code
            // (I don't even remember what this is for!) -tom7 14 Sep 2025
            for (uint32_t codepoint : codepoints) {
              if (codepoint == 0) {
                MutexLock ml(&out_m);
                if (spam_per.ShouldRun()) {
                  status.Print("codepoint zero in {}", filename);
                }
                counters[std::format("zero_codepoint({})", format_type)]++;
                bad.push_back(filename);
                return;
              }
              codepoints_found.insert(codepoint);

              if (stbtt_FindGlyphIndex(&font, codepoint) != glyph) {
                MutexLock ml(&out_m);
                if (spam_per.ShouldRun()) {
                  status.Print("Bad cmap in {}", filename);
                }
                counters[std::format("bad_cmap({})({})",
                                     format_type,
                                     have_s ? "have_s" : "no_s")]++;
                bad.push_back(filename);
                return;
              }
            }
          }

          {
            MutexLock ml(&out_m);
            good_by_format[format_type]++;
          }
        }

        {
          MutexLock ml(&out_m);
          good.push_back(filename);
        }
      },
      16);

  Print("{} bytes in {} sec\n"
        "{} good and {} bad\n"
        "Bad:\n",
         total_bytes, time(nullptr) - start,
         (int64)good.size(),
         (int64)bad.size());
  for (const auto &[name, count] : counters) {
    Print("  {}: {}\n", name, count);
  }

  Print("Good with each format:\n");
  for (const auto &[fmt, count] : CountMapToDescendingVector(good_by_format)) {
    Print("  {}: {}\n", fmt, count);
  }

  Util::WriteLinesToFile("all_fonts_tmp.txt", good);
  return 0;
}
