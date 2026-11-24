#ifndef _ESCAPE_DIRINDEX_H
#define _ESCAPE_DIRINDEX_H

/* a directory index provides meta-information about a directory in a
   managed collection (e.g., triage). it includes:
     * a nice name (title) for the directory
     * global ratings for each level
     * dates that the levels were created (can be used to sort)
     * current speed records for each level
     * (your idea here)

   indices are written as index.esi in each managed directory.

   Note: This is being replaced by leveldb for the 4.0 series.
*/

#include <string>
#include <string_view>

#define DIRINDEXNAME "index.esi"
#define WEBINDEXNAME "webindex.esi"

/* all as totals */
struct RateStatus {
  int nvotes = 0;
  int difficulty = 0;
  int style = 0;
  int rigidity = 0;
  int cooked = 0;
  int solved = 0;

  RateStatus() {}
};

struct DirIndex {
  /* make an empty index, suitable for later writing to disk */
  static DirIndex *Create();

  virtual ~DirIndex() {}

  /* read from disk */
  static DirIndex *FromFile(std::string_view f);

  static bool IsIndex(std::string_view f);

  virtual void WriteFile(std::string_view f) = 0;
  virtual void AddEntry(std::string_view filename, RateStatus v,
                        int date, int speedrecord, int owner) = 0;

  virtual bool GetEntry(
      std::string_view filename, RateStatus &v,
      int &date, int &speedrecord, int &owner) const = 0;

  /* true if this is a managed collection */
  virtual bool WebCollection() const = 0;

  std::string title;
};

#endif
