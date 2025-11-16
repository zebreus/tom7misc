// Code for cleaning and working with ASCII guitar tab files, e.g. from OLGA.

#include "guitarchive.h"

#include <cstdint>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "re2/re2.h"
#include "threadutil.h"
#include "util.h"

using namespace std;
using int64 = int64_t;

static constexpr const char *DIRS[] = {
  "c:\\code\\electron-guitar\\tabscrape\\tabs",
  "d:\\temp\\olga",
  "d:\\temp\\tabs",
};


void Guitarchive::AddAllFilesRec(const string &dir, vector<string> *all_files) {
  for (const string &f : Util::ListFiles(dir)) {
    const string filename = Util::DirPlus(dir, f);
    // Print("{} + {} = {}\n", dir, f, filename);
    if (Util::isdir(filename)) {
      // Print("Dir: [{}]\n", filename);
      AddAllFilesRec(filename, all_files);
    } else {
      if (!filename.empty() &&
      // Should probably delete emacs backups..?
      filename[filename.size() - 1] != '#' &&
      filename[filename.size() - 1] != '~') {
        all_files->push_back(filename);
      }
    }
  }
}

string Guitarchive::Frontslash(const string &s) {
  string ret;
  for (const char c : s)
    ret += (c == '\\' ? '/' : c);

  if (ret.find("d:/") == 0) {
    ret[0] = '/';
    ret[1] = 'd';
  } else if (ret.find("c:/") == 0) {
    ret[0] = '/';
    ret[1] = 'c';
  }

  return ret;
}

string Guitarchive::Backslash(const string &s) {
  string ret;
  for (const char c : s)
    ret += (c == '/' ? '\\' : c);
  return ret;
}

vector<Entry> Guitarchive::Load(int threads) {

  Print("List files..\n");
  fflush(stdout);

  vector<string> all_filenames;
  for (const char *d : DIRS) {
    Guitarchive::AddAllFilesRec(d, &all_filenames);
  }

  Print("Num files: {}\nReading..\n", all_filenames.size());
  fflush(stdout);

  // For a well-formed file, this will stop on the blank line after the
  // headers.
  RE2 normalized_header{"([A-Za-z0-9][^:]+): (.+)\n"};

  auto MakeEntry = [&normalized_header](const string &filename,
                    const string &contents) {
      Entry entry;
      entry.filename = filename;

      std::string_view cont(contents);
      string key, val;
      while (RE2::Consume(&cont, normalized_header, &key, &val)) {
        if (key == "Title") {
          entry.title = std::move(val);
        } else if (key == "Artist") {
          entry.artist = std::move(val);
        } else if (key == "Album") {
          entry.album = std::move(val);
        } else {
          entry.headers.emplace_back(std::move(key), std::move(val));
        }
      }

      entry.body = std::string(cont);
      return entry;
    };

  vector<Entry> entries =
    ParallelMap(all_filenames,
        [&MakeEntry](const string &filename) {
          string f = Backslash(filename);
          return MakeEntry(f, Util::ReadFile(f));
        },
        threads);

  return entries;
}


