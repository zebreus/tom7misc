
#ifndef _REPHRASE_REPHRASING_H
#define _REPHRASE_REPHRASING_H

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>

#include "document.h"

struct Rephrasing {
  static Rephrasing *Create(const std::string &database_file);
  virtual ~Rephrasing();

  struct Rephrasable {
    std::vector<std::shared_ptr<DocTree>> images;
    std::vector<std::unordered_map<std::string, AttrVal>> classes;
    std::vector<std::unordered_map<std::string, AttrVal>> wrap_all;
    std::string context;
    std::string text;
  };

  virtual void Save() = 0;

  // Attempt to make a new rephrasing. If it is valid, insert
  // it in the database.
  virtual bool Rephrase(const Rephrasable &rephrasable) = 0;

  // TODO: Allow providing scores for rephrasings, perhaps at
  // specific spots, as feedback from pack-boxes?

  // Returns the known rephrasings of the string. Sorted by loss
  // (ascending). The rephrasings should be valid (can be joined
  // back up with the rephrasable).
  virtual std::vector<std::pair<double, std::string>> GetRephrasings(
      const Rephrasable &rephrasable) = 0;

  // Counts only valid rephrasings.
  virtual int GetNumRephrasings(const Rephrasable &rephrasable) = 0;

  static bool Rejoin(const Rephrasable &rephrasable,
                     const std::string &text,
                     DocTree *doc,
                     std::string *error);

  static Rephrasable GetTextToRephrase(const DocTree &doc);

  // Get the database key, which depends on the context, text, and
  // current model. It is ASCII.
  virtual std::string DatabaseKey(const Rephrasable &rephrasable) = 0;

 protected:
  // Use Create().
  Rephrasing();
};

#endif
