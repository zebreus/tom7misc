
#include "zip.h"

#include <string>
#include <vector>
#include <cstdint>
#include <deque>

#include "miniz.h"

#include "base/logging.h"


namespace {

template<size_t N_>
struct Vec {
  static constexpr size_t N = N_;
  std::array<uint8_t, N> arr;
  // Where the first byte would be. Never more than N.
  size_t start = 0;
  // one past last byte. Never more than N.
  size_t end = 0;

  const uint8_t *data() const {
    return arr.data() + start;
  }
  uint8_t *data() {
    return arr.data() + start;
  }

  // Amount of data available to read, starting at start.
  size_t size() const {
    DCHECK(start <= end);
    DCHECK(start <= N);
    DCHECK(end <= N);
    return end - start;
  }

  // Area in which to write new data.
  const uint8_t *space() const {
    return arr.data() + end;
  }
  uint8_t *space() {
    return arr.data() + end;
  }

  // Amount of space left to write at the end.
  size_t space_left() const {
    DCHECK(end <= N);
    return N - end;
  }
};

template<size_t N_>
struct Buf {
  static constexpr size_t N = N_;
  // A double-ended queue. The writer writes to the back, and the
  // reader reads from the front. Each vec has capacity N.

  // Get the next write destination, perhaps by allocating a new
  // block. It will always have at least one byte of space.
  Vec<N> *GetDest() {
    if (q.empty()) {
      q.emplace_back();
      return &q.back();
    }
    Vec<N> *v = &q.back();
    if (v->space_left() == 0) {
      DCHECK(v->end == N);
      q.emplace_back();
      return &q.back();
    } else {
      return v;
    }
  }

  // Get the next read source, possibly destroying empty blocks,
  // and possibly returning nullptr if there is no data. If non-null,
  // there will always be at least one byte to read.
  Vec<N> *GetSrc() {
    while (!q.empty()) {
      Vec<N> *v = &q.front();
      if (v->start == N) {
        DCHECK(v->end == N);
        q.pop_front();
      } else if (v->start == v->end) {
        // It is possible to reach the final block, which exists
        // as a write destination. In this case, the stream is
        // empty for reading.
        return nullptr;
      } else {
        return v;
      }
    }
    return nullptr;
  }

  // Callers need to manually adjust this.
  void AdjustSize(int64_t delta) {
    size += delta;
    CHECK(size >= 0) << size << " " << delta;
  }

  size_t WriteOutput(uint8_t *data, size_t size) {
    size_t done = 0;
    while (size > 0) {
      Vec<N> *v = GetSrc();
      if (v == nullptr) break;
      DCHECK(v->start <= v->end);
      size_t chunk_size = v->end - v->start;
      // We might not have enough room for the whole chunk
      // in the output.
      size_t copy_size = std::min(chunk_size, size);
      memcpy(data, v->data(), copy_size);
      done += copy_size;
      v->start += copy_size;
      DCHECK(copy_size <= size);
      DCHECK(v->start <= v->end);
      size -= copy_size;
    }
    return done;
  }

  std::deque<Vec<N>> q;
  int64_t size = 0;
};

struct EBImpl : public ZIP::EncodeBuffer {
  static constexpr size_t BUFFER_SIZE = 32768;

  // For levels 0..10, the number of hash probes to use.
  static constexpr const int probes_for_level[11] = {
    0, 1, 6, 32, 16, 32, 128, 256, 512, 768, 1500 };

  EBImpl(int level) {
    level = std::clamp(level, 0, 10);
    // Don't want any of the other flags. Just set the number of
    // probes by the compression level.
    const int FLAGS = probes_for_level[level] & TDEFL_MAX_PROBES_MASK;

    CHECK(TDEFL_STATUS_OKAY ==
          tdefl_init(&enc,
                     // no callback. using tdefl_compress interface.
                     nullptr, nullptr,
                     FLAGS));
  }

  ~EBImpl() override { }

  void InsertVector(const std::vector<uint8_t> &v) override {
    InsertPtr(v.data(), v.size());
  }

  void InsertString(const std::string &s) override {
    InsertPtr((const uint8_t *)s.data(), s.size());
  }

  void InsertPtr(const uint8_t *data_in, size_t size_in) override {
    const uint8_t *data = data_in;
    size_t remaining = size_in;
    while (remaining > 0) {
      V *out = buf.GetDest();
      DCHECK(V::N > out->end);
      const size_t space_left = out->space_left();
      DCHECK(space_left != 0);
      // in/out parameter
      size_t out_bytes = space_left;
      size_t in_bytes = remaining;

      uint8_t *out_space = out->space();

      tdefl_status status =
        tdefl_compress(&enc,
                       // input buffer
                       data, &in_bytes,
                       // output buffer.
                       out_space, &out_bytes,
                       TDEFL_NO_FLUSH);

      CHECK(status == TDEFL_STATUS_OKAY) << "Bug! Only OKAY status makes "
        "sense since we don't FINISH here.";

      DCHECK(in_bytes <= remaining);
      remaining -= in_bytes;

      DCHECK(out_bytes <= out->space_left()) << out_bytes << " "
                                             << out->space_left();
      out->end += out_bytes;
      buf.AdjustSize(+out_bytes);

    } while (remaining > 0);
  }

  void Finalize() override {

    for (;;) {
      V *out = buf.GetDest();
      DCHECK(V::N > out->end);
      const size_t space_left = out->space_left();
      DCHECK(space_left != 0);
      // In/out parameter.
      size_t out_bytes = space_left;
      uint8_t *out_space = out->space();

      size_t in_bytes = 0;
      tdefl_status status =
        tdefl_compress(&enc,
                       // (empty) input buffer
                       nullptr, &in_bytes,
                       // output buffer.
                       out_space, &out_bytes,
                       TDEFL_FINISH);

      CHECK(in_bytes == 0);

      CHECK(status == TDEFL_STATUS_OKAY ||
            status == TDEFL_STATUS_DONE) << "Must be one of these, "
        "or something is wrong.";

      DCHECK(out_bytes <= space_left) << out_bytes << " " << space_left;
      out->end += out_bytes;
      buf.AdjustSize(+out_bytes);

      if (status == TDEFL_STATUS_DONE) return;
    }
  }

  size_t OutputSize() const override { return buf.size; }

  std::vector<uint8_t> GetOutputVector() override {
    size_t sz = buf.size;
    std::vector<uint8_t> ret(sz);
    CHECK(WriteOutput(ret.data(), ret.size()) == sz);
    return ret;
  }
  std::string GetOutputString() override {
    size_t sz = buf.size;
    std::string ret;
    ret.resize(sz);

    size_t wrote = WriteOutput((uint8_t *)ret.data(), ret.size());

    CHECK(wrote == sz) << wrote << " " << sz;
    return ret;
  }

  // Write up to size bytes; return the number written.
  size_t WriteOutput(uint8_t *data, size_t size) override {
    return buf.WriteOutput(data, size);
  }


  tdefl_compressor enc;
  using V = Vec<BUFFER_SIZE>;
  Buf<BUFFER_SIZE> buf;
};

struct DBImpl : public ZIP::DecodeBuffer {
  static constexpr size_t BUFFER_SIZE = 32768;

  ~DBImpl() override {
    tinfl_decompressor_free(dec);
    dec = nullptr;
  }

  DBImpl() : dec(tinfl_decompressor_alloc()) {
    CHECK(dec != nullptr);
    tinfl_init(dec);
  }

  // Insert compressed data. New decompressed bytes may become
  // available in the output.
  void InsertVector(const std::vector<uint8_t> &v) override {
    InsertPtr(v.data(), v.size());
  }
  void InsertString(const std::string &s) override {
    InsertPtr((const uint8_t *)s.data(), s.size());
  }
  void InsertPtr(const uint8_t *data_in, size_t size_in) override {
    if (size_in == 0) return;

    // TODO: Figure out if I want headers etc.
    // Probably not by default?

    const uint8_t *data = data_in;
    size_t remaining = size_in;
    bool has_more_output = false;
    do {
      V *out = buf.GetDest();
      const size_t space_left = out->space_left();
      DCHECK(space_left != 0);
      // in/out parameter.
      size_t out_bytes = space_left;
      // Note this may be zero if we're making more calls just to
      // read output.
      size_t in_bytes = remaining;

      uint8_t *out_space = out->space();

      tinfl_status status =
        tinfl_decompress(dec,
                         // input buffer
                         data, &in_bytes,
                         // output buffer. not using a circular
                         // buffer here.
                         out_space, out_space, &out_bytes,
                         TINFL_FLAG_HAS_MORE_INPUT |
                         TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

      CHECK(status != TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS)
        << "Should be impossible since we're passing data with each "
        "call.";

      CHECK(status != TINFL_STATUS_BAD_PARAM) << "Bug?";

      CHECK(status != TINFL_STATUS_FAILED) <<
        "Flate stream is corrupt (miscellaneous).";

      CHECK(status != TINFL_STATUS_ADLER32_MISMATCH) <<
        "Flate stream is corrupt (bad checksum).";

      /*
      CHECK(status != TINFL_STATUS_DONE) <<
        "Bug: This is unexpected because we've said there's more input, "
        "but possibly I do not understand the miniz API.";
      */
      CHECK(status == TINFL_STATUS_DONE ||
            status == TINFL_STATUS_NEEDS_MORE_INPUT ||
            status == TINFL_STATUS_HAS_MORE_OUTPUT);

      DCHECK(in_bytes <= remaining);
      remaining -= in_bytes;

      DCHECK(out_bytes <= out->space_left());
      out->end += out_bytes;
      buf.AdjustSize(+out_bytes);

      if (status == TINFL_STATUS_NEEDS_MORE_INPUT) {
        // We've read all the output. We're done if we've inserted all
        // our input.
        has_more_output = false;
      } else if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
        // Need to loop again, at least to read the pending output.
        has_more_output = true;
      } else if (status == TINFL_STATUS_DONE) {
        // The situations where this is returned are not clearly
        // documented (since I think the flate stream is not
        // delimited), but it's pretty clear that we don't have more
        // output to read.
        has_more_output = false;
      } else {
        LOG(FATAL) << "Bug: Unknown status " << status;
      }

    } while (remaining > 0 || has_more_output);
  }


  // Number of bytes that are ready.
  size_t OutputSize() const override {
    return buf.size;
  }

  // Note: These are correct, but we could be a little faster by
  // skipping the logic below that makes sure we don't exceed
  // the buffer size. We have exactly enough space by construction.
  std::vector<uint8_t> GetOutputVector() override {
    size_t sz = buf.size;
    std::vector<uint8_t> ret(sz);
    CHECK(WriteOutput(ret.data(), ret.size()) == sz);
    return ret;
  }
  std::string GetOutputString() override {
    size_t sz = buf.size;
    std::string ret;
    ret.resize(sz);
    CHECK(WriteOutput((uint8_t *)ret.data(), ret.size()) == sz);
    return ret;
  }

  // Write up to size bytes; return the number written.
  size_t WriteOutput(uint8_t *data, size_t size) override {
    return buf.WriteOutput(data, size);
  }

  // PERF: We should be able to avoid an indirection here.
  // Just nest the struct.
  tinfl_decompressor *dec = nullptr;
  using V = Vec<BUFFER_SIZE>;
  Buf<BUFFER_SIZE> buf;
};
}

ZIP::DecodeBuffer *ZIP::DecodeBuffer::Create() {
  return new DBImpl;
}

ZIP::EncodeBuffer *ZIP::EncodeBuffer::Create(int level) {
  return new EBImpl(level);
}

ZIP::EncodeBuffer::EncodeBuffer() { }
ZIP::EncodeBuffer::~EncodeBuffer() { }

ZIP::DecodeBuffer::DecodeBuffer() { }
ZIP::DecodeBuffer::~DecodeBuffer() { }

std::vector<uint8_t> ZIP::UnzipPtr(const uint8_t *data, size_t size) {
  std::unique_ptr<DecodeBuffer> dec(ZIP::DecodeBuffer::Create());
  CHECK(dec.get() != nullptr);

  dec->InsertPtr(data, size);

  return dec->GetOutputVector();
}

std::vector<uint8_t> ZIP::UnzipVector(const std::vector<uint8_t> &v) {
  return UnzipPtr(v.data(), v.size());
}

std::string ZIP::UnzipString(const std::string &s) {
  std::unique_ptr<DecodeBuffer> dec(ZIP::DecodeBuffer::Create());
  CHECK(dec.get() != nullptr);

  dec->InsertString(s);

  return dec->GetOutputString();
}


std::vector<uint8_t> ZIP::ZipPtr(const uint8_t *data, size_t size,
                                 int level) {
  std::unique_ptr<EncodeBuffer> enc(ZIP::EncodeBuffer::Create(level));
  CHECK(enc.get() != nullptr);

  enc->InsertPtr(data, size);
  enc->Finalize();
  return enc->GetOutputVector();
}

std::vector<uint8_t> ZIP::ZipVector(const std::vector<uint8_t> &v,
                                    int level) {
  return ZipPtr(v.data(), v.size(), level);
}

std::string ZIP::ZipString(const std::string &s, int level) {
  std::unique_ptr<EncodeBuffer> enc(ZIP::EncodeBuffer::Create(level));
  CHECK(enc.get() != nullptr);

  enc->InsertString(s);
  enc->Finalize();
  return enc->GetOutputString();
}
