
#ifndef _REPHRASE_TALK_H
#define _REPHRASE_TALK_H

#include <string>
#include <vector>
#include <optional>

struct Talk {

  // Screen width; fixed for the presentation.
  int screen_width = 1920;
  int screen_height = 1080;

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
    // Goal pace is to reach this slide by this number
    // of seconds (total).
    int target_seconds = 0;
  };

  static Talk Load(
      // The .talk file to load.
      const std::string &filename);

  void SaveJS(
      // The location of support files like talk.html
      const std::string &program_dir,
      // The directory to write into.
      const std::string &out_dir);

  std::vector<Slide> slides;
};

#endif
