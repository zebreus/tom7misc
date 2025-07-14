
#ifdef __APPLE__
// fstat64 is deprecated; force fstat to be 64-bit
#define _DARWIN_USE_64_BIT_INODE 1
#endif

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

#ifdef __APPLE__
// fstat64 is deprecated; force fstat to be 64-bit
#define stat64 stat
#define fstat64 fstat
#endif

// 4 Mb
static constexpr int BUFFER_SIZE = 1 << 22;

int main(int argc, char **argv) {
  CHECK(argc >= 3) << "compress.exe -7 in out\n"
    "Where -7 can be -1 to -9, giving the compression level.\n"
    "Higher yields more compression, but is slower.\n";

  Periodically status_per(0.25);
  Timer timer;

  int level = 7;
  std::string infile, outfile;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (Util::TryStripPrefix("-", &arg)) {
      level = atoi(arg.c_str());
    } else if (infile.empty()) {
      infile = arg;
    } else if (outfile.empty()) {
      outfile = arg;
    } else {
      LOG(FATAL) << "Unknown command-line option?";
    }
  }

  printf("Compressing " AWHITE("%s") " to " AWHITE("%s")
         ", level " AWHITE("%d") ".\n",
         infile.c_str(), outfile.c_str(), level);

  FILE *fin = fopen(infile.c_str(), "rb");
  FILE *fou = fopen(outfile.c_str(), "wb");
  CHECK(fin) << infile;
  CHECK(fou) << outfile;

  // Guess file size, but this is just for progress reporting.
  int64_t input_size_guess = -1;
  int64_t input_processed = 0;
  {
    const int fd = fileno(fin);
    struct stat64 st;
    if (0 == fstat64(fd, &st)) {
      input_size_guess = st.st_size;
    }
  }

  // Write header, but we write the size last.
  ZIP::CCLibHeader header;
  header.SetFlags(0);
  header.SetSize(0);
  CHECK(1 == fwrite(&header, sizeof (header), 1, fou)) << outfile;

  std::vector<uint8_t> buf(BUFFER_SIZE, 0);

  int64_t output_size = 0;
  std::unique_ptr<ZIP::EncodeBuffer> eb(ZIP::EncodeBuffer::Create(level));
  auto Flush = [&]() {
      while (eb->OutputSize() > 0) {
        std::vector<uint8_t> v = eb->GetOutputVector();
        if (!v.empty()) {
          output_size += v.size();
          CHECK(1 == fwrite(v.data(), v.size(), 1, fou)) << outfile;
        }
      }
    };

  auto Status = [&](bool force = false) {
      if (force || status_per.ShouldRun()) {
        double ratio = output_size / (double)input_processed;
        printf(ANSI_UP "%s\n",
               ANSI::ProgressBar(input_processed, input_size_guess,
                                 StringPrintf("Compress (%.2f%%)",
                                              ratio * 100.0),
                                 timer.Seconds()).c_str());
      };
    };

  while (!feof(fin)) {
    size_t bytes = fread(buf.data(), 1, BUFFER_SIZE, fin);
    eb->InsertPtr(buf.data(), bytes);
    input_processed += bytes;
    CHECK(bytes != 0) << infile;
    Flush();
    Status();
  }

  eb->Finalize();
  Flush();
  fclose(fin);

  // Write header now that we have the actual size.
  printf("We compressed %lld (guess %lld) bytes.\n", input_processed,
         input_size_guess);
  fseek(fou, 0, SEEK_SET);
  header.SetFlags(0);
  header.SetSize(input_processed);
  CHECK(header.GetSize() == input_processed);
  CHECK(1 == fwrite(&header, sizeof (header), 1, fou)) << outfile;

  fclose(fou);

  Status(true);
  printf("Done\n");
  return 0;
}
