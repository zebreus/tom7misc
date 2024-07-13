
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

  std::vector<uint8_t> bytes;
};
}  // namespace

void MOV::CloseOut(Out *out) {
  CHECK(out->file != nullptr);
  // TODO: Finalize.
  fclose(out->file);
  out->file = nullptr;
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

MOV::Out::Out(FILE *f) : file(f) {}

void MOV::Out::WriteHeader() {

  // XXX this depends on
  // num_frames, fps. So probably we should
  // write it at the end.

  static constexpr uint32_t moov_size = 0; // XXX
  Write32(moov_size);
  WriteCC("moov");

  // The nested movie header.
  static constexpr uint32_t mvhd_size = 0;
  Write32(mvhd_size);
  WriteCC("mvhd");
  // Version
  Write8(0); // ?
  // Flags, reserved.
  Write8(0);
  Write8(0);
  Write8(0);

  // The creation and edited time. Note that these
  // fields are 32 bit, so they will overflow soon.
  // Probably nobody cares about these.
  const uint32_t now = (uint32_t)time(nullptr);
  Write32(now);
  Write32(now);

  Write32(TIME_SCALE);
  Write32(num_frames * frame_duration);
  // Speed and volume, both 1.0.
  Write32(FIXED32_ONE);
  Write16(FIXED16_ONE);

  // Transformation matrix.
  for (int i = 0; i < 9; i++) Write32(IDENTITY_MATRIX[i]);

  // Preview start and duration. We just use the first frame?
  Write32(0);
  Write32(frame_duration);

  // Poster time is also the first frame.
  Write32(0);

  // Selection time and duration
  Write32(0);
  Write32(0);

  // Current time.
  Write32(0);

  // Next track id. This code only outputs one track.
  Write32(TRACK_ID + 1);


  // Now the tracks. We have just one.

  Buf trak;
  // Size, tbd
  trak.W32(0);
  trak.WCC("trak");
  // Required sub-atoms: trhd, mdia

  {
    // https://developer.apple.com/documentation/quicktime-file-format/track_header_atom
    Buf trhd;
    // size, tbd
    trhd.W32(0);
    trhd.WCC("trhd");
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
    trhd.WriteSizeTo(0);
    trak.AddBuf(trhd);
  }

  {
    Buf mdia;
    mdia.W32(0);
    mdia.WCC("mdia");

    {
      Buf mdhd;
      mdhd.W32(0);
      mdhd.WCC("mdhd");
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

      mdhd.WriteSizeTo(0);
      mdia.AddBuf(mdhd);
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
      Buf minf;
      minf.W32(0);
      minf.WCC("minf");

      {
        // sample table
        Buf stbl;
        stbl.W32(0);
        stbl.WCC("stbl");

        {
          Buf stsd;
          stsd.W32(0);
          stsd.WCC("stsd");

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
          Buf entry;
          entry.W32(0);
          entry.WCC("h777");
          // reserved
          for (int i = 0; i < 6; i++) entry.W8(0);
          // index of "data reference" (??)
          // https://developer.apple.com/documentation/quicktime-file-format/sample_description_atom
          // Do they mean mdat? Or is this sidecar data?
          entry.W16(0);

          entry.WriteSizeTo(0);

          stsd.AddBuf(entry);
          stsd.WriteSizeTo(0);
          stbl.AddBuf(stsd);
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

        stbl.WriteSizeTo(0);
        minf.AddBuf(stbl);
      }

      minf.WriteSizeTo(0);
      mdia.AddBuf(minf);
    }

    mdia.WriteSizeTo(0);
    trak.AddBuf(mdia);
  }

  trak.WriteSizeTo(0);
  // XXX write to output

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
