#ifndef _REPHRASE_MOVIE_DOCUMENT_H
#define _REPHRASE_MOVIE_DOCUMENT_H

#include <map>
#include <string_view>

#include "document.h"
#include "image-document.h"

struct MovieDocument : public ImageDocument {
  MovieDocument(std::string_view program_dir);

  // Outputs the movie file.
  void GenerateOutput(
      std::string_view dirname,
      const std::map<int, std::map<int, DocTree>> &pages) override;

 private:
};

#endif
