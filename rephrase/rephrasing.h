
#ifndef _REPHRASE_REPHRASING_H
#define _REPHRASE_REPHRASING_H

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <utility>

#include "document.h"

struct LLM;

struct Rephrasing {
  Rephrasing();
  ~Rephrasing();

  struct Rephrasable {
    std::vector<std::shared_ptr<DocTree>> images;
    std::vector<std::unordered_map<std::string, AttrVal>> classes;
    std::string context;
    std::string text;
  };

  // Attempt to make a new rephrasing. If it is valid, insert
  // it in the database.
  bool MakeNewRephrasing(const Rephrasable &rephrasable);

  // Returns the known rephrasings of the string. Sorted by loss
  // (ascending). The rephrasings should be valid (can be joined
  // back up with the rephrasable).
  std::vector<std::pair<double, std::string>> GetRephrasings(
      const Rephrasable &rephrasable);

  Rephrasable GetTextToRephrase(const DocTree &doc);

  // Get the database key, which depends on the context, text, and
  // current model. It is ASCII.
  std::string DatabaseKey(const Rephrasable &rephrasable);

 private:
  void LazyInit();

  std::string model_key;
  std::unique_ptr<LLM> llm;
};

#endif
