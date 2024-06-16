
#include "util.h"

#include <cstring>
#include "base/logging.h"
#include "base/stringprintf.h"

namespace {
struct PNGChunk {
  uint32_t length;
  char type[4];
};
}

// Read 4 bytes in big-endian; no checks.
[[maybe_unused]]
inline static uint32_t Read32(const uint8_t *data) {
  return ((uint32_t)data[0] << 24) |
    ((uint32_t)data[1] << 16) |
    ((uint32_t)data[2] << 8) |
    (uint32_t)data[3];
}

// data should point at the next chunk.
inline static PNGChunk ReadPngChunk(const uint8_t *data) {
  PNGChunk ret;
  ret.length = ((uint32_t)data[0] << 24) |
    ((uint32_t)data[1] << 16) |
    ((uint32_t)data[2] << 8) |
    (uint32_t)data[3];
  ret.type[0] = data[4];
  ret.type[1] = data[5];
  ret.type[2] = data[6];
  ret.type[3] = data[7];
  return ret;
}


static void GetPNGs(const std::string &contents) {
  const char *start = contents.data();
  size_t full_size = contents.size();

  static const uint8_t png_sig[8] = {
    137,80,78,71,13,10,26,10,
  };

  const char *next = start;
  while ((next = (const char *)Util::MemMem((const uint8_t *)next,
                                            full_size - (next - start),
                                            png_sig, 8))) {
    const char *png_start = next;
    std::string outfile = StringPrintf("offset%lld.png",
                                       (int64_t)(next - start));

    // Found PNG header here.
    // [[maybe_unused]]
    // PNGHeader hdr = ReadPngHeader((const uint8_t *)next);
    next += 8;
    for (;;) {
      PNGChunk chunk = ReadPngChunk((const uint8_t*)next);
      const uint32_t chunk_length = chunk.length;
      // Length, type + content
      next += 12 + chunk_length;

      printf("%c%c%c%c x %lld\n",
             chunk.type[0],
             chunk.type[1],
             chunk.type[2],
             chunk.type[3], (int64_t)chunk_length);

      if (0 == memcmp(chunk.type, "IEND", 4)) {
        // Done.
        const size_t png_size = next - png_start;
        std::vector<uint8_t> out;
        out.resize(png_size);
        memcpy(out.data(), png_start, png_size);
        Util::WriteFileBytes(outfile, out);
        printf("Wrote %s\n", outfile.c_str());
        break;
      }

      CHECK(next <= contents.data() + contents.size());
    }
  }
}

int main(int argc, char **argv) {
  CHECK(argc == 2) << "./extractpng.exe source-file\n"
    "Puts the output in [offset 1].png, [offset 2].png ...\n";

  std::string contents = Util::ReadFile(argv[1]);
  CHECK(!contents.empty()) << argv[1];

  GetPNGs(contents);

  return 0;
}
