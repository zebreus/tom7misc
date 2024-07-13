
#include "mov.h"

#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "base/logging.h"

using Out = MOV::Out;

// Possibly useful reference:
// ffmpeg/libavformat/movenc.c

// 1.0 in a 32-bit fixed point quantity.
static constexpr uint32_t FIXED32_ONE = 0x00010000;
static constexpr uint16_t FIXED16_ONE = 0x0100;
// In matrices, the last column uses a 2_30 split of
// bits.
static constexpr uint32_t FRACT32_ONE = (1 << 30);

static constexpr uint32_t TRACK_ID = 1;

static constexpr uint32_t IDENTITY_MATRIX[9] = {
  FIXED32_ONE, 0, 0,
  0, FIXED32_ONE, 0,
  0, 0, FRACT32_ONE,
};

namespace {
struct Buf {
  void W8(uint8_t b) {
    bytes.push_back(b);
  }

  void W16(uint16_t w) {
    W8(0xFF & (w >> 8));
    W8(0xFF & w);
  }

  void W32(uint32_t w) {
    W8(0xFF & (w >> 24));
    W8(0xFF & (w >> 16));
    W8(0xFF & (w >>  8));
    W8(0xFF & (w >>  0));
  }

  void W32At(size_t idx, uint32_t w) {
    CHECK(idx + 3 < bytes.size());
    bytes[idx + 0] = 0xFF & (w >> 24);
    bytes[idx + 1] = 0xFF & (w >> 16);
    bytes[idx + 2] = 0xFF & (w >>  8);
    bytes[idx + 3] = 0xFF & (w >>  0);
  }

  void WCC(const char (&fourcc)[5]) {
    uint32_t enc =
      (uint32_t(fourcc[0]) << 24) |
      (uint32_t(fourcc[1]) << 16) |
      (uint32_t(fourcc[2]) << 8) |
      (uint32_t(fourcc[3]) << 0);
    W32(enc);
  }

  void WriteSizeTo(size_t idx) {
    size_t s = bytes.size();
    CHECK(s < 0x100000000) << "Need to add support for 64-bit sizes!";
    W32At(idx, s);
  }

  void AddBuf(const Buf &other) {
    for (uint8_t b : other.bytes) {
      bytes.push_back(b);
    }
  }

  size_t Size() const {
    return bytes.size();
  }

  std::vector<uint8_t> bytes;
};
}

namespace internal {
// Best to just use Chunk and AddChunk.
struct Chunk : public Buf {
  Chunk(const char (&fourcc)[5]) {
    // Reserved for size.
    W32(0);
    WCC(fourcc);
  }

  // Sets the chunk size (in the destination, not source).
  void AddChunk(const Chunk &other) {
    CHECK(other.Size() >= 4);
    size_t pos = bytes.size();
    AddBuf(other);
    W32At(pos, other.Size());
  }

};
}  // namespace

using Chunk = internal::Chunk;

void MOV::CloseOut(std::unique_ptr<Out> &out) {
  CHECK(out->file != nullptr);

  out->WriteHeader();

  // TODO: Finalize.
  fclose(out->file);
  out->file = nullptr;
  out.reset(nullptr);
}

void MOV::Out::Write8(uint8_t b) {
  if (file != nullptr) {
    if (EOF == fputc(b, file)) {
      fclose(file);
      file = nullptr;
    } else {
      pos++;
    }
  }
}

void MOV::Out::Write32(uint32_t w) {
  Write8(0xFF & (w >> 24));
  Write8(0xFF & (w >> 16));
  Write8(0xFF & (w >>  8));
  Write8(0xFF & (w >>  0));
}

void MOV::Out::Write16(uint16_t w) {
  Write8(0xFF & (w >>  8));
  Write8(0xFF & (w >>  0));
}

void MOV::Out::WriteCC(const char (&fourcc)[5]) {
  uint32_t enc =
    (uint32_t(fourcc[0]) << 24) |
    (uint32_t(fourcc[1]) << 16) |
    (uint32_t(fourcc[2]) << 8) |
    (uint32_t(fourcc[3]) << 0);
  Write32(enc);
}

void MOV::Out::WritePtr(const uint8_t *data, size_t size) {
  if (file != nullptr) {
    if (size == 0) return;

    if (1 == fwrite(data, size, 1, file)) {
      pos += size;
    } else {
      fclose(file);
      file = nullptr;
    }
  }
}

void MOV::Out::WriteChunk(const Chunk &chunk) {
  // TODO: Support 64-bit sizes.
  const size_t size = chunk.Size();
  Write32(size);
  WritePtr(chunk.bytes.data() + 4, size - 4);
}

MOV::Out::Out(FILE *f) : file(f) {}

void MOV::Out::WriteHeader() {

  // XXX this depends on
  // num_frames, fps. So probably we should
  // write it at the end.

  Chunk moov("moov");

  // The nested movie header.
  Chunk mvhd("mvhd");
  // Version
  mvhd.W8(0); // ?
  // Flags, reserved.
  mvhd.W8(0);
  mvhd.W8(0);
  mvhd.W8(0);

  // The creation and edited time. Note that these
  // fields are 32 bit, so they will overflow soon.
  // Probably nobody cares about these.
  const uint32_t now = (uint32_t)time(nullptr);
  mvhd.W32(now);
  mvhd.W32(now);

  mvhd.W32(TIME_SCALE);
  mvhd.W32(num_frames * frame_duration);
  // Speed and volume, both 1.0.
  mvhd.W32(FIXED32_ONE);
  mvhd.W16(FIXED16_ONE);

  // Transformation matrix.
  for (int i = 0; i < 9; i++) Write32(IDENTITY_MATRIX[i]);

  // Preview start and duration. We just use the first frame?
  mvhd.W32(0);
  mvhd.W32(frame_duration);

  // Poster time is also the first frame.
  mvhd.W32(0);

  // Selection time and duration
  mvhd.W32(0);
  mvhd.W32(0);

  // Current time.
  mvhd.W32(0);

  // Next track id. This code only outputs one track.
  mvhd.W32(TRACK_ID + 1);


  // Now the tracks. We have just one.

  Chunk trak("trak");
  // Required sub-atoms: trhd, mdia

  {
    // https://developer.apple.com/documentation/quicktime-file-format/track_header_atom
    Chunk trhd("trhd");
    // trhd 0
    trhd.W8(0);
    // Flags, zero.
    trhd.W8(0);
    trhd.W8(0);
    trhd.W8(0);

    // Creation and modification times.
    trhd.W32(now);
    trhd.W32(now);

    trhd.W32(TRACK_ID);
    // Reserved 1
    trhd.W32(0);
    // Duration
    trhd.W32(num_frames * frame_duration);
    // Reserved 2, 3
    trhd.W32(0);
    trhd.W32(0);

    // Layer
    trhd.W16(0);
    // Alternate group (?)
    trhd.W16(0);
    // Volume
    trhd.W16(FIXED16_ONE);
    // Reserved 4
    trhd.W16(0);

    for (int i = 0; i < 9; i++) trhd.W32(IDENTITY_MATRIX[i]);

    trhd.W32(width);
    trhd.W32(height);

    trak.AddChunk(trhd);
  }

  {
    Chunk mdia("mdia");

    {
      Chunk mdhd("mdhd");
      // Version 0
      mdhd.W8(0);
      // Flags, 0
      mdhd.W8(0);
      mdhd.W8(0);
      mdhd.W8(0);
      // Creation and Modification time
      mdhd.W32(now);
      mdhd.W32(now);

      mdhd.W32(TIME_SCALE);
      mdhd.W32(num_frames * frame_duration);

      // Language code 0 = English
      mdhd.W16(0);
      // Quality. Undocumented. ffmpeg writes 0.
      mdhd.W16(0);

      mdia.AddChunk(mdhd);
    }

    /*
       minf
        stbl
          stsd
          stco
          co64
          stts
          stss
          stsc
          stsz
    */

    {
      // media info
      Chunk minf("minf");

      {
        // sample table
        Chunk stbl("stbl");

        {
          Chunk stsd("stsd");

          // Version
          stsd.W8(0);
          // Reserved flags
          stsd.W8(0);
          stsd.W8(0);
          stsd.W8(0);
          // We just use one encoding, so the
          // table just needs one entry.
          // Number of entries.
          stsd.W32(1);

          // Entries.
          Chunk entry("h777");
          // reserved
          for (int i = 0; i < 6; i++) entry.W8(0);
          // index of "data reference" (??)
          // https://developer.apple.com/documentation/quicktime-file-format/sample_description_atom
          // Do they mean mdat? Or is this sidecar data?
          entry.W16(0);

          stsd.AddChunk(entry);
          stbl.AddChunk(stsd);
        }

        // TODO:
        // use stco or co64 depending on how
        // big the offsets are

        // TODO:
        // https://developer.apple.com/documentation/quicktime-file-format/composition_offset_atom
        // ctts is what orders the presentation of frames (I think
        // they are always ordered in decoding order)

        // TODO:
        // stss to mark keyframes ("sync" frames, i-frames)
        // if absent, everything is an i-frame.

        // sample table sizes.

        minf.AddChunk(stbl);
      }

      mdia.AddChunk(minf);
    }

    trak.AddChunk(mdia);
  }

  moov.AddChunk(trak);

  moov.AddChunk(mvhd);
  moov.WriteSizeTo(0);
  WriteChunk(moov);
}

std::unique_ptr<Out> MOV::OpenOut(std::string_view filename,
                                  int width, int height) {
  // TODO: Check maximum values here too
  CHECK(width > 0);
  CHECK(height > 0);

  FILE *f = fopen(std::string(filename).c_str(), "wb");
  if (f == nullptr) return nullptr;

  std::unique_ptr<Out> out(new Out(f));
  out->width = width;
  out->height = height;

  // Begins with an 'ftyp' atom. It has the size,
  // type, major brand, minor version, and one
  // compatible brand, so 4x5 = 20 bytes.
  out->Write32(20);
  out->WriteCC("ftyp");
  out->WriteCC("qt  ");
  // minor version 0?
  // looks like this could be something like 0x20 0x04 0x06 0x00
  // for the date 2004-06, but I see zero in real files.
  out->Write32(0);
  out->WriteCC("qt  ");

  return out;
}

void MOV::Out::AddFrame(const ImageRGBA &img) {
  CHECK(img.Height() == height && img.Width () == width);
  Frame f{.pos = pos};
  // TODO! Write it as raw bytes, R-G-B.
  for (int y = 0; y < img.Height(); y++) {
    for (int x = 0; x < img.Width(); x++) {
      // ...
    }
  }

  f.size = img.Height() * img.Width() * 3;
  frames.push_back(f);
}
