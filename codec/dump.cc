
#include "mov.h"

#include <functional>
#include <cstdio>
#include <string>
#include <string_view>
#include <memory>

#include "base/logging.h"
#include "ansi.h"

using In = MOV::In;
using ChunkHeader = MOV::In::ChunkHeader;

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

      printf("%*s" AGREY("@%zu") " [" AYELLOW("%s") "] × %zu\n",
             depth * 2, "",
             (size_t)chunk_pos,
             cho->FourCC().c_str(), cho->total_size);
      // TODO: Handle zero size
      CHECK(cho->total_size != 0);

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
        printf("%*s" AGREY("   ... %zu bytes ...") "\n",
               depth * 2, "", cho->size_left);
        (void)in->ReadBytes(cho->size_left);
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
