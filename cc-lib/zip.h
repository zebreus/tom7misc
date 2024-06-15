
#ifndef _CC_LIB_ZIP_H
#define _CC_LIB_ZIP_H

#include <cstdint>
#include <string>
#include <vector>

// Simple flate codec, wrapping miniz. These are raw streams with
// header or footer. You could add your own (typically it is useful
// to record the size of the decompressed data and a checksum) or
// maybe I'll add those in the future.

struct ZIP {

  // The simplest functions operate on the entire input at once.

  // The "level" can range from 0 to 9. Higher is slower, but generates
  // smaller files.

  static std::vector<uint8_t> ZipVector(const std::vector<uint8_t> &v,
                                        int level = 7);
  static std::string ZipString(const std::string &s,
                               int level = 7);
  static std::vector<uint8_t> ZipPtr(const uint8_t *data, size_t size,
                                     int level = 7);

  static std::vector<uint8_t> UnzipVector(const std::vector<uint8_t> &v);
  static std::string UnzipString(const std::string &s);
  static std::vector<uint8_t> UnzipPtr(const uint8_t *data, size_t size);

  static std::vector<uint8_t> ZipPtrRaw(const uint8_t *data, size_t size,
                                        int level = 7);
  static std::vector<uint8_t> UnzipPtrRaw(const uint8_t *data, size_t size);

  // Streaming interfaces.

  struct EncodeBuffer {
    // Level ranges from 0 to 10; higher levels are slower but can
    // produce better compression.
    static EncodeBuffer *Create(int level = 7);
    virtual ~EncodeBuffer();

    // Insert raw data to be compressed. New compressed bytes may
    // become available in the output.
    virtual void InsertVector(const std::vector<uint8_t> &v) = 0;
    virtual void InsertPtr(const uint8_t *data, size_t size) = 0;
    virtual void InsertString(const std::string &s) = 0;

    virtual void Finalize() = 0;

    // Number of bytes that are ready.
    virtual size_t OutputSize() const = 0;
    virtual std::vector<uint8_t> GetOutputVector() = 0;
    virtual std::string GetOutputString() = 0;
    // Write up to size bytes; return the number written.
    virtual size_t WriteOutput(uint8_t *data, size_t size) = 0;

   protected:
    // Use Create.
    EncodeBuffer();
  };

  struct DecodeBuffer {
    static DecodeBuffer *Create();
    virtual ~DecodeBuffer();

    // Insert compressed data. New decompressed bytes may become
    // available in the output.
    virtual void InsertVector(const std::vector<uint8_t> &v) = 0;
    virtual void InsertPtr(const uint8_t *data, size_t size) = 0;
    virtual void InsertString(const std::string &s) = 0;

    // XXX need some way to indicate that the stream is done.
    // XXX need some way to indicate failure.

    // Number of bytes that are ready.
    virtual size_t OutputSize() const = 0;
    virtual std::vector<uint8_t> GetOutputVector() = 0;
    virtual std::string GetOutputString() = 0;
    // Write up to size bytes; return the number written.
    virtual size_t WriteOutput(uint8_t *data, size_t size) = 0;
   protected:
    // Use Create.
    DecodeBuffer();
  };

};



#endif
