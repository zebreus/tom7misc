
#include "mov.h"

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

  int depth = 0;
  for (;;) {
    const int chunk_pos = in->Pos();
    std::optional<ChunkHeader> cho = in->NextChunk();
    if (!cho.has_value()) {
      printf("EOF\n");
      in.reset(nullptr);
      return;
    }

    printf(AGREY("@%zu") " [" AYELLOW("%s") "] × %zu\n",
           (size_t)chunk_pos,
           cho->FourCC().c_str(), cho->total_size);
    // TODO: Handle zero size
    if (cho->size_left > 0) {
      printf(AGREY("   ... %zu bytes ...") "\n", cho->size_left);
      (void)in->ReadBytes(cho->size_left);
    }
  }
}


int main(int argc, char **argv) {
  CHECK(argc == 2) << "./dump.exe test.mov\n";

  Dump(argv[1]);

  return 0;
}
