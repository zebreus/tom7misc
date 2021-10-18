
// TODO: To cc-lib?

#ifndef _WIKIPEDIA_H
#define _WIKIPEDIA_H

#include <stdio.h>

#include <string>
#include <optional>

// Wikipedia as a large corpus of text. Unprincipled
// 'parsing' of the XML dump.
struct Wikipedia {

  static Wikipedia *Create(const std::string &filename);
  virtual ~Wikipedia() {}

  struct Article {
    std::string title;
    std::string body;
  };

  // Returns nullopt when at EOF.
  virtual std::optional<Article> Next() = 0;

  // Returns true if this is a #REDIRECT article, and can probably be ignored.
  virtual bool IsRedirect(const Article &article) = 0;

  // Replace HTML entities with their corresponding
  // UTF-8 (from an incomplete, hard-coded list).
  virtual std::string ReplaceEntities(std::string body) = 0;

  // TODO...
  // After ReplaceEntities. Removes tags like <ref> and <!-- comments -->.
  virtual std::string RemoveTags(std::string body) = 0;

  // Remove wikilinks. For example, "[[Pejorative|term of abuse]]" is
  // replaced with just "term of abuse". [[Image:whatever.jpg...] is
  // just dropped. {{templates}} are just dropped.
  static std::string RemoveWikilinks(const std::string &body);

  // Replace some UTF-8 sequences with simpler ASCII ones, mostly
  // fancy quotes and dashes.
  static std::string ASCIIify(const std::string &body);


 protected:
  Wikipedia() {}
};

#endif
