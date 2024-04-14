
#ifndef _REPHRASE_TALK_H
#define _REPHRASE_TALK_H

#include <string>
#include <vector>
#include <optional>

struct Talk {

  struct Video {
    int x = 0, y = 0;
    int width = 0, height = 0;
    std::string src;
    bool loop = false;
  };

  struct Frame {
    // Source image.
    std::string filename;
    int duration = 1;
  };

  struct Slide {
    std::vector<Frame> frames;
    std::optional<Video> video;
  };

  static Talk Load(const std::string &filename);

  void SaveJS(const std::string &dir);

  std::vector<Slide> slides;
};

#endif
