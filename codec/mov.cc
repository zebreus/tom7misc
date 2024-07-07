
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

// 1.0 in a 32-bit fixed point quantity.
static constexpr uint32_t FIXED32_ONE = 0x00010000;
static constexpr uint16_t FIXED16_ONE = 0x0100;
// In matrices, the last column uses a 2_30 split of
// bits.
static constexpr uint32_t FRACT32_ONE = (1 << 30);

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
  // num_frames, fps

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
  Write32(1);

  // Now the tracks. We have just one.

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

  }

  Buf track;
  // size, tbd
  track.W32(0);
  track.WCC("trak");


}

std::unique_ptr<Out> MOV::OpenOut(std::string_view filename) {
  FILE *f = fopen(std::string(filename).c_str(), "wb");
  if (f == nullptr) return nullptr;

  std::unique_ptr<Out> out(new Out(f));

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

}
