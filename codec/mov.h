
// The MOV file format. This is sometimes also known as the QuickTime
// file format. It's structurally identical to MP4, so you could
// possibly use this to work with MP4 files.
//
// https://developer.apple.com/documentation/quicktime-file-format/
//

#ifndef _MOV_H
#define _MOV_H

#include <cstdio>
#include <memory>
#include <string_view>
#include <string>
#include <cstdint>

#include "base/logging.h"

struct MOV {

  static constexpr int TIME_SCALE = 60000;
  // FPS values when using the above time scale.
  static constexpr int DURATION_59_94 = 1001;
  static constexpr int DURATION_60 = 1000;
  static constexpr int DURATION_29_97 = 2002;
  static constexpr int DURATION_30 = 2000;
  static constexpr int DURATION_24 = 2500;

  // An in-progress output stream (open file). Only basic video
  // is supported now.
  struct Out {



   private:
    friend struct MOV;
    int64_t pos = 0;
    int width = 0, height = 0;
    int num_frames = 0;
    int frame_duration = 0;
    Out(FILE *f);
    void Write8(uint8_t b);
    void Write16(uint16_t w);
    void Write32(uint32_t w);
    // WriteCC("qt  ");
    void WriteCC(const char (&fourcc)[5]);
    void WriteHeader();
    FILE *file = nullptr;

    std::vector<Frame> frames;

    struct Frame {
      // Absolute byte offset in output.
      int64_t pos = 0;

    };

  };

  std::unique_ptr<Out> OpenOut(std::string_view filename,
                               int width, int height);
  void CloseOut(Out *);

  #if 0
  struct FTyp {

  };


  // Parsed to host byte order.
  struct AtomHeader {
    // In the file, this can be stored as a 32- or 64-bit value.
    uint64_t size = 0;
    uint32_t type = 0;
  };
  #endif

};

#endif
