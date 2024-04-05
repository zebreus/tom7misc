
#ifndef _REPHRASE_TALK_H
#define _REPHRASE_TALK_H

#include <string>
#include <vector>

struct Talk {

  struct Frame {
    // Source image.
    std::string filename;
    int duration = 1;
  };

  struct Slide {
    std::vector<Frame> frames;
  };

  static Talk Load(const std::string &filename);

  void SaveJS(const std::string &dir);

  std::vector<Slide> slides;
};

#endif
