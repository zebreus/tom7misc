
#ifndef _GUITARCHIVE_H
#define _GUITARCHIVE_H

#include <utility>
#include <vector>
#include <string>


// Parsed entry from disk. This is not sufficient to exactly recreate
// the file; see cleandb.cc for round-trip stuff.
struct Entry {
  // These are removed from the headers if present.
  std::string artist;
  std::string title;
  std::string album;
  // Other headers as key, value.
  std::vector<std::pair<std::string, std::string>> headers;

  std::string filename;

  // Unparsed body.
  std::string body;
};

struct Guitarchive {
  static void AddAllFilesRec(const std::string &dir,
                             std::vector<std::string> *all_files);

  static std::string Frontslash(const std::string &s);
  static std::string Backslash(const std::string &s);

  // Load everything, with headers.
  static std::vector<Entry> Load(int threads = 16);
};

#endif
