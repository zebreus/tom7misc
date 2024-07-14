
// Incomplete!
// The MOV file format. This is sometimes also known as the QuickTime
// file format. It's structurally identical to MP4, so you could
// possibly use this to work with MP4 files.
//
// https://developer.apple.com/documentation/quicktime-file-format/
//

#ifndef _MOV_H
#define _MOV_H

#include <utility>
#include <optional>
#include <cstdio>
#include <memory>
#include <string_view>
#include <string>
#include <cstdint>
#include <vector>

#include "base/logging.h"
#include "image.h"

namespace internal {
struct Buf;
struct Chunk;
}

struct MOV {

  static constexpr int TIME_SCALE = 60000;
  // FPS values when using the above time scale.
  static constexpr int DURATION_59_94 = 1001;
  static constexpr int DURATION_60 = 1000;
  static constexpr int DURATION_29_97 = 2002;
  static constexpr int DURATION_30 = 2000;
  static constexpr int DURATION_24 = 2500;

  // An in-progress output stream (open file). Only basic video
  // is supported now. Create this with OpenOut below.
  struct Out {

    void AddFrame(const ImageRGBA &img);

   private:
    friend struct MOV;

    struct Frame {
      // Absolute byte offset in output.
      int64_t pos = 0;
      int64_t size = 0;
    };

    int64_t pos = 0;
    int width = 0, height = 0;
    int frame_duration = DURATION_60;
    int64_t mdat_size32_pos = 0;
    Out(FILE *f);
    void Write8(uint8_t b);
    void Write16(uint16_t w);
    void Write32(uint32_t w);
    // WriteCC("qt  ");
    void WriteCC(const char (&fourcc)[5]);
    void WriteHeader();
    void WritePtr(const uint8_t *data, size_t size);
    void WriteBuf(const internal::Buf &buf);
    void WriteChunk(const internal::Chunk &chunk);
    void FinalizeData();
    void WriteDelayed();
    int64_t Pos() const { return pos; }
    internal::Chunk GetVideoFormatChunk() const;
    static internal::Chunk GetFtypChunk();
    FILE *file = nullptr;

    std::vector<Frame> frames;
    std::vector<std::pair<std::size_t, std::vector<uint8_t>>>
      delayed_writes;
  };

  static std::unique_ptr<Out> OpenOut(std::string_view filename,
                                      int width, int height,
                                      int duration = DURATION_60);
  // Finalizes the file; consumes the argument.
  static void CloseOut(std::unique_ptr<Out> &out);


  // Extremely basic parsing, basically just for writing debugging
  // tools.
  struct In {
    ~In();

    struct ChunkHeader {
      // The value in the size field (whether 32-bit or 64-bit).
      // This is not usually what you care about, since we've
      // already read past the header.
      int64_t total_size = 0;
      // Number of bytes remaining in the chunk.
      int64_t size_left = 0;
      uint8_t fourcc[4] = {};

      bool IsFourCC(const char (&cc)[5]) const;
      std::string FourCC() const;
    };

    // With the file pointer pointing at a valid chunk. Aborts
    // if the file is already closed or if an incomplete/invalid
    // chunk header is read. Returns nullopt if at a clean EOF
    // (and then the object should not be used further).
    // Otherwise, the chunk header.
    std::optional<ChunkHeader> NextChunk();

    // Aborts if the target size cannot be read.
    std::vector<uint8_t> ReadBytes(size_t s);

    std::optional<uint8_t> Read8();
    std::optional<uint16_t> Read16();
    std::optional<uint32_t> Read32();

    int64_t Pos() const { return pos; }

   private:
    friend struct MOV;
    In(FILE *file) : file(file) {}
    FILE *file = nullptr;
    int64_t pos = 0;
  };

  // To close the input, simply destroy the object.
  static std::unique_ptr<In> OpenIn(std::string_view filename);
};

#endif
