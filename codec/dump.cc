
#include "mov.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"

using In = MOV::In;
using ChunkHeader = MOV::In::ChunkHeader;

static bool ShouldShowData(const std::string &s) {
  static std::unordered_set<std::string> fourccs{
    "dref",
    "vmhd",
    "hdlr",
    "stsd",
  };

  return fourccs.contains(s);
}

static std::string ColorByte(uint8_t b) {
  if (b == 0) return ANSI_FG(30, 30, 30) "00";
  else return StringPrintf(ANSI_FG(60, 60, 70) "%02x", b);
}

static bool Printable(uint8_t c) {
  return c == ' ' || isalnum(c);
}

static void Dump(std::string_view f) {
  std::unique_ptr<MOV::In> in = MOV::OpenIn(f);
  CHECK(in.get() != nullptr) << f;

  // size_left = 0 means there's no limit (top-level chunks).
  std::function<bool(int, int64_t)> DumpChunk =
    [&](int depth, int64_t size_left) -> bool {
      int64_t start_pos = in->Pos();
      for (;;) {
        const int64_t chunk_pos = in->Pos();
        const int64_t consumed_size = chunk_pos - start_pos;
        CHECK(size_left == 0 ||
              consumed_size <= size_left) << "Chunk exceeded "
          "its parent's size?";
        if (size_left != 0 && consumed_size == size_left)
          return true;

        // If there is more remaining space, read a chunk.
        std::optional<ChunkHeader> cho = in->NextChunk();
        if (!cho.has_value()) {
          if (depth == 0)
            printf("EOF\n");
          return false;
        }

        std::string indent(depth * 2, ' ');

        printf("%s" AGREY("@%zu") " [" AYELLOW("%s") "] × %zu\n",
               indent.c_str(),
               (size_t)chunk_pos,
               cho->FourCC().c_str(), cho->total_size);
        // TODO: Handle zero size
        CHECK(cho->total_size != 0);

        const std::string fourcc = cho->FourCC();

        if (cho->IsFourCC("moov") ||
            cho->IsFourCC("trak") ||
            cho->IsFourCC("edts") ||
            cho->IsFourCC("mdia") ||
            cho->IsFourCC("minf") ||
            cho->IsFourCC("stbl") ||
            cho->IsFourCC("dinf")) {

          if (!DumpChunk(depth + 1, cho->size_left))
            return false;

        } else if (cho->size_left > 0) {
          if (ShouldShowData(fourcc)) {
            std::vector<uint8_t> bytes =
              in->ReadBytes(cho->size_left);

            int width = 16;
            int height = (bytes.size() + (width - 1)) / width;

            for (int y = 0; y < height; y++) {
              printf("%s", indent.c_str());
              for (int x = 0; x < width; x++) {
                int idx = y * width + x;
                if (idx < (int)bytes.size()) {
                  uint8_t b = bytes[idx];
                  printf("%s" ANSI_RESET " ", ColorByte(b).c_str());
                } else {
                  printf(ANSI_FG(20, 20, 20) ".." ANSI_RESET " ");
                }
                if (x % 4 == 3) printf(" ");
              }

              printf(ANSI_FG(20, 20, 20) " | ");
              // Now as ASCII

              for (int x = 0; x < width; x++) {
                int idx = y * width + x;
                if (idx < (int)bytes.size()) {
                  uint8_t b = bytes[idx];
                  if (Printable(b)) {
                    printf(ANSI_FG(30, 40, 30) "%c", b);
                  } else {
                    printf(ANSI_FG(20, 20, 20) ".");
                  }
                } else {
                  printf(" ");
                }
                if (x % 4 == 3) printf(" ");
              }

              printf("\n");
            }

          } else {
            printf("%s" AGREY("   ... %zu bytes ...") "\n",
                   indent.c_str(), cho->size_left);
            (void)in->ReadBytes(cho->size_left);
          }
        }
      }
  };

  DumpChunk(0, 0);
}


int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 2) << "./dump.exe test.mov\n";

  Dump(argv[1]);

  return 0;
}
