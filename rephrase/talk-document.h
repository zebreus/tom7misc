#ifndef _REPHRASE_TALK_DOCUMENT_H
#define _REPHRASE_TALK_DOCUMENT_H

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

#include "document.h"
#include "image-document.h"

struct TalkDocument : public ImageDocument {
  TalkDocument(std::string_view program_dir);

  // Outputs the image files and the .talk file.
  void GenerateOutput(
      std::string_view dirname,
      const std::map<int, std::map<int, DocTree>> &pages) override;

 private:
};

#endif
