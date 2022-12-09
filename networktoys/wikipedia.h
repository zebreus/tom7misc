
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

  // Returns true if this is a #REDIRECT article, and can probably be
  // ignored.
  virtual bool IsRedirect(const Article &article) = 0;

  // Replace HTML entities with their corresponding
  // UTF-8 (from an incomplete, hard-coded list).
  virtual std::string ReplaceEntities(std::string body) = 0;

  // After ReplaceEntities.
  // Removes tags like <ref> and <!-- comments -->.
  virtual std::string RemoveTags(const std::string &body) = 0;

  // Remove wikilinks. For example, "[[Pejorative|term of abuse]]" is
  // replaced with just "term of abuse". [[Image:whatever.jpg...] is
  // just dropped. [[Category:whatever]] is also just dropped.
  // {{templates}} are just dropped, although for cases like
  // {{infobox start}}...{{infobox end}} this may not do what you
  // want.
  virtual void RemoveWikilinks(std::string *body) = 0;

  // Remove markup like ''italics'' and '''bold'''.
  virtual void RemoveMarkup(std::string *body) = 0;

  // Replace some UTF-8 sequences with simpler ASCII ones, mostly
  // fancy quotes and dashes.
  virtual void ASCIIify(std::string *body) = 0;

  // TODO:
  // Remove between {| ... |}. Seldom prose.
  virtual void RemoveTables(std::string *body) = 0;


  // TODO: Remove [http://site.com hyperlinks], replacing with the
  // linked text.
  // TODO: Drop __NOTOC__

  // TODO: Something with bullet-point lists?

  // TODO: Filter articles like /Wikipedia:Articles for deletion/etc.
  // TODO: Filter articles like Category:Westchester County, New York

 protected:
  Wikipedia() {}
};

#endif
