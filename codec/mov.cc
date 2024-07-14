
#include "mov.h"

#include <cstddef>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"

using Out = MOV::Out;
using In = MOV::In;

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

namespace internal {
struct Buf {
  void WB(const std::initializer_list<uint8_t> &bs) {
    for (uint8_t b : bs) bytes.push_back(b);
  }

  void WPascal(const std::string &s) {
    CHECK(s.size() < 256) << "String too large to be stored "
      "as a Pascal string!";
    bytes.push_back((uint8_t)s.size());
    for (int i = 0; i < (int)s.size(); i++)
      bytes.push_back((uint8_t)s[i]);
  }

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

using Buf = internal::Buf;
using Chunk = internal::Chunk;

void MOV::CloseOut(std::unique_ptr<Out> &out) {
  CHECK(out->file != nullptr);

  out->FinalizeData();
  out->WriteHeader();
  out->WriteDelayed();

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

void MOV::Out::WriteBuf(const Buf &buf) {
  WritePtr(buf.bytes.data(), buf.Size());
}

void MOV::Out::WriteChunk(const Chunk &chunk) {
  // TODO: Support 64-bit sizes.
  const size_t size = chunk.Size();
  Write32(size);
  WritePtr(chunk.bytes.data() + 4, size - 4);
}

MOV::Out::Out(FILE *f) : file(f) {}

void MOV::Out::FinalizeData() {
  // Finalize the mdat chunk, which we're in the midst
  // of writing.
  int64_t mdat_size = Pos() - mdat_size32_pos;
  CHECK(mdat_size >= 8);
  CHECK(0 == (0x100000000 & mdat_size)) << "Please add "
    "support for 64-bit sizes!";

  Buf size32;
  size32.W32(mdat_size);
  delayed_writes.emplace_back(mdat_size32_pos, size32.bytes);
}

// Seek around the file to write stuff that doesn't
// happen in order.
void MOV::Out::WriteDelayed() {
  for (const auto &[pos, bytes] : delayed_writes) {
    if (file == nullptr)
      return;

    if (fseek(file, pos, SEEK_SET) < 0) {
      file = nullptr;
      return;
    }

    WritePtr(bytes.data(), bytes.size());
  }
}

// This depends on the frames, so it's actually written
// last.
void MOV::Out::WriteHeader() {
  Chunk moov("moov");

  // The nested movie header.
  Chunk mvhd("mvhd");
  // Version
  mvhd.W8(0); // ?
  // Flags, reserved.
  mvhd.WB({0, 0, 0});

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
  for (int i = 0; i < 9; i++) mvhd.W32(IDENTITY_MATRIX[i]);

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
    trhd.WB({0, 0, 0});

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
      mdhd.WB({0, 0, 0});
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

    //
    /*
       |- size (32 bits)          // Size of the entire atom, including header
  |- type (32 bits)          // Atom type identifier ('hdlr')
  |- version (8 bits)         // Usually set to 0
  |- flags (24 bits)         // Reserved; usually set to 0
  |- componentType (32 bits) // Predefined type (e.g., 'mhlr' - media handler)
  |- componentSubtype (32 bits) // FourCC code for the handler type (e.g., 'vide', 'soun')
  |- componentManufacturer (32 bits) // Optional; often set to 0
  |- componentFlags (32 bits)     // Optional flags; often set to 0
  |- componentFlagsMask (32 bits)  // Optional mask for flags; often set to 0
  |- componentName (Pascal string) // Optional human-readable name for the handler
    */

    // Required, but boring
    {
      Chunk hdlr("hdlr");
      // Version 0
      hdlr.W8(0);
      // Flags
      hdlr.WB({0, 0, 0});
      // 'media handler' for 'video'
      hdlr.WCC("mhlr");
      hdlr.WCC("vide");
      // manufacturer
      hdlr.W32(0);
      // flags
      hdlr.WB({0, 0, 0, 0});
      // flags mask
      hdlr.WB({0, 0, 0, 0});
      hdlr.WPascal("VideoHandler");

      mdia.AddChunk(hdlr);
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

      // TODO: vmhd?

      // Also required here, but boring
      {
        Chunk hdlr("hdlr");
        // Version 0
        hdlr.W8(0);
        // Flags
        hdlr.WB({0, 0, 0});
        // 'data handler' for 'url' (?)
        hdlr.WCC("dhlr");
        hdlr.WCC("url ");
        // manufacturer
        hdlr.W32(0);
        // flags
        hdlr.WB({0, 0, 0, 0});
        // flags mask
        hdlr.WB({0, 0, 0, 0});
        hdlr.WPascal("DataHandler");

        minf.AddChunk(hdlr);
      }


      // Since the sample description has to have an index
      // into dref, we write a single degenerate one.
      {
        Chunk dinf("dinf");
        Chunk dref("dref");
        // version
        dref.W8(0);
        // flags
        dref.W8(0);
        dref.W8(0);
        dref.W8(0);
        // one entry
        dref.W32(1);

        // The entry:
        // XXX ffmpeg claims "Unknown dref type 0x206c7275 size 12"
        // (but does it matter?)
        Chunk url("url ");
        // Version
        url.W8(0);
        // Flags
        url.W8(0);
        url.W8(0);
        url.W8(0);
        // Data; empty.

        dref.AddChunk(url);
        dinf.AddChunk(dref);
        minf.AddChunk(dinf);
      }

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

          stsd.AddChunk(GetVideoFormatChunk());

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


// I think this is where all the fourccs are defined in ffmpeg:
// libavformat/isom_tags.c

// Looks like 'raw ' and 'RGBA' and 'png ' are good options.

// In ffmpeg, compare mov_write_video_tag
Chunk MOV::Out::GetVideoFormatChunk() {
  // Entries.
  Chunk entry("raw ");
  // reserved
  for (int i = 0; i < 6; i++) entry.W8(0);
  // index of "data reference" (??)
  // https://developer.apple.com/documentation/quicktime-file-format/sample_description_atom
  // Do they mean mdat? Or is this sidecar data?
  entry.W16(0);

  return entry;
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

  out->WriteChunk(out->GetFtypChunk());

  // In the steady state, the file is writing the "mdat" chunk.
  // This is handled specially since we don't want to require
  // the whole thing to reside in RAM.

  // TODO: Support 64-bit sizes, especially here.

  // We have to seek back here to write this.
  out->mdat_size32_pos = out->Pos();
  out->Write32(0);
  out->WriteCC("mdat");


  return out;
}

Chunk MOV::Out::GetFtypChunk() {
  // Begins with an 'ftyp' atom.
  Chunk ftyp("ftyp");
  // MOV format.
  ftyp.WCC("qt  ");
  // minor version 0?
  // looks like this could be something like 0x20 0x04 0x06 0x00
  // for the date 2004-06, but I see zero in real files.
  ftyp.W32(0);
  ftyp.WCC("qt  ");
  return ftyp;
}

void MOV::Out::AddFrame(const ImageRGBA &img) {
  CHECK(img.Height() == height && img.Width () == width);
  Frame f{.pos = pos};

  Buf buf;
  for (int y = 0; y < img.Height(); y++) {
    for (int x = 0; x < img.Width(); x++) {
      const auto &[r, g, b, a] = img.GetPixel(x, y);
      buf.W8(r);
      buf.W8(g);
      buf.W8(b);
    }
  }

  WriteBuf(buf);

  f.size = img.Height() * img.Width() * 3;
  CHECK((int64_t)buf.Size() == (int64_t)f.size);
  frames.push_back(f);
}

template<class T>
static inline T AssertOpt(const char *what,
                          const std::optional<T> &to) {
  CHECK(to.has_value()) << what;
  return to.value();
}

std::optional<MOV::In::ChunkHeader> MOV::In::NextChunk() {
  CHECK(file != nullptr);

  std::optional<uint32_t> s = Read32();
  if (!s.has_value()) {
    fclose(file);
    file = nullptr;
    return std::nullopt;
  }

  ChunkHeader head;
  head.total_size = s.value();
  head.fourcc[0] = AssertOpt("fourcc", Read8());
  head.fourcc[1] = AssertOpt("fourcc", Read8());
  head.fourcc[2] = AssertOpt("fourcc", Read8());
  head.fourcc[3] = AssertOpt("fourcc", Read8());
  // 64-bit size?
  if (head.total_size == 1) {
    uint64_t size_hi = AssertOpt("64-bit size", Read32());
    uint64_t size_lo = AssertOpt("64-bit size", Read32());
    CHECK(0 == (size_hi & 0x80000000)) << "Size is way too big";
    head.total_size = (size_hi << 32) | size_lo;
    head.size_left = head.total_size - 4 * 4;
  } else {
    head.size_left = head.total_size - 2 * 4;
  }

  // Either way, the file pointer is at the beginning
  // of the chunk's data now, like we wanted.
  return {head};
}

std::vector<uint8_t> MOV::In::ReadBytes(size_t s) {
  CHECK(file != nullptr);
  if (s == 0) return {};
  std::vector<uint8_t> ret(s);
  CHECK(1 == fread(ret.data(), s, 1, file)) << "Failed to read "
                                            << s << " bytes.";
  pos += s;
  return ret;
}

std::optional<uint8_t> MOV::In::Read8() {
  CHECK(file != nullptr);
  int c = fgetc(file);
  if (c == EOF) return std::nullopt;
  pos++;
  return {c};
}

std::optional<uint16_t> MOV::In::Read16() {
  std::optional<uint16_t> hi = Read8();
  std::optional<uint16_t> lo = Read8();
  if (hi.has_value() && lo.has_value()) {
    return (hi.value() << 8) | lo.value();
  } else {
    return std::nullopt;
  }
}

std::optional<uint32_t> MOV::In::Read32() {
  std::optional<uint16_t> hi = Read16();
  std::optional<uint16_t> lo = Read16();
  if (hi.has_value() && lo.has_value()) {
    return (hi.value() << 16) | lo.value();
  } else {
    return std::nullopt;
  }
}

std::unique_ptr<In> MOV::OpenIn(std::string_view filename) {
  FILE *f = fopen(std::string(filename).c_str(), "rb");
  if (f == nullptr) return nullptr;

  return std::unique_ptr<In>(new In(f));
}

MOV::In::~In() {
  if (file != nullptr) {
    fclose(file);
    file = nullptr;
  }
}


bool MOV::In::ChunkHeader::IsFourCC(const char (&cc)[5]) const {
  return cc[0] == fourcc[0] &&
    cc[1] == fourcc[1] &&
    cc[2] == fourcc[2] &&
    cc[3] == fourcc[3];
}

static inline bool Printable(char c) {
  return c == ' ' || isalnum(c);
}

std::string MOV::In::ChunkHeader::FourCC() const {
  std::string ret;
  if (Printable(fourcc[0]) &&
      Printable(fourcc[1]) &&
      Printable(fourcc[2]) &&
      Printable(fourcc[3])) {
    ret.resize(4);
    ret[0] = fourcc[0];
    ret[1] = fourcc[1];
    ret[2] = fourcc[2];
    ret[3] = fourcc[3];
    return ret;
  } else {
    return StringPrintf("%02x.%02x.%02x.%02x",
                        fourcc[0],
                        fourcc[1],
                        fourcc[2],
                        fourcc[3]);
  }
}

