
#ifndef _GUITARCHIVE_CHORD_PARSER_H
#define _GUITARCHIVE_CHORD_PARSER_H

#include <vector>
#include <string>

#include "re2/re2.h"

struct ChordParser {
 public:
  ChordParser();
  ~ChordParser();

  struct Parsed {
    std::vector<std::string> chords;
    int lines = 0;
    int intro_lines = 0;
    int chord_lines = 0, crd_lines = 0;
    int chords_truncated = 0;
  };

  // Thread safe.
  Parsed ExtractChords(const std::string &body);

 private:
  RE2 *standard_chord_re = nullptr;
  RE2 *line_of_chords_re = nullptr;
  RE2 *extract_chord_line_re = nullptr;
  RE2 *line_with_crd_re = nullptr;
  RE2 *extract_bracketed_re = nullptr;
  RE2 *intro_line_re = nullptr;
};

#endif
