
#include <sys/stat.h>

#include <vector>
#include <cstdint>
#include <cstdio>

#include "zip.h"
#include "ansi.h"
#include "util.h"
#include "timer.h"
#include "periodically.h"
#include "base/logging.h"
#include "base/stringprintf.h"

// 1 Mb
static constexpr int BUFFER_SIZE = 1 << 20;

int main(int argc, char **argv) {
  CHECK(argc == 3) << "decompress.exe in.ccz out\n";

  Periodically status_per(0.25);
  Timer timer;

  std::string infile = argv[1], outfile = argv[2];

  printf("Decompressing " AWHITE("%s") " to " AWHITE("%s") ".\n",
         infile.c_str(), outfile.c_str());

  FILE *fin = fopen(infile.c_str(), "rb");
  FILE *fou = fopen(outfile.c_str(), "wb");
  CHECK(fin) << infile;
  CHECK(fou) << outfile;

  ZIP::CCLibHeader header;
  CHECK(1 == fread(&header, sizeof (header), 1, fin)) << infile;
  CHECK(header.HasCorrectMagic()) << "Not a ccz file.\n";

  CHECK(header.GetFlags() == 0) << "Unsupported flags.\n";

  // Guess file size, but this is just for progress reporting.
  /*
  printf("Header bytes:\n");
  for (int i = 0; i < sizeof (header); i++) {
    printf(" %02x", ((const uint8_t*)&header)[i]);
  }
  */
  const int64_t output_size = header.GetSize();
  // printf("Supposedly there are %lld bytes in the output.\n", output_size);
  int64_t input_processed = 0;
  int64_t output_written = 0;

  std::unique_ptr<ZIP::DecodeBuffer> db(ZIP::DecodeBuffer::Create());
  auto Flush = [&]() {
      while (db->OutputSize() > 0) {
        std::vector<uint8_t> v = db->GetOutputVector();
        if (!v.empty()) {
          CHECK(1 == fwrite(v.data(), v.size(), 1, fou)) << outfile;
          output_written += v.size();
        }
      }
    };

  auto Status = [&](bool force = false) {
      if (force || status_per.ShouldRun()) {
        double ratio = output_written / (double)input_processed;
        printf(ANSI_UP "%s\n",
               ANSI::ProgressBar(output_written, output_size,
                                 StringPrintf("Decompress (%.2f%%)",
                                              ratio * 100.0),
                                 timer.Seconds()).c_str());
      };
    };

  printf("\n");
  std::vector<uint8_t> buf(BUFFER_SIZE, 0);
  while (!feof(fin)) {
    size_t bytes = fread(buf.data(), 1, BUFFER_SIZE, fin);
    db->InsertPtr(buf.data(), bytes);
    input_processed += bytes;
    CHECK(bytes != 0) << infile;
    Flush();
    Status();
  }

  Flush();
  fclose(fin);
  fclose(fou);

  Status(true);
  printf("Done\n");
  return 0;
}
