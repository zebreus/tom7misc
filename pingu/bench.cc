
// Benchmark a "hard drive"

#include <unistd.h>
#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "timer.h"
#include "util.h"
#include "base/logging.h"

using namespace std;
using uint8 = uint8_t;
using int64 = int64_t;

int main(int argc, char **argv) {

  CHECK(geteuid() == 0) << "Must run as root.";

  CHECK(argc == 3) << "./bench.exe /mnt/testdrive datafile";

  string testdrive = argv[1];
  string datafilename = argv[2];
  vector<uint8> databytes = Util::ReadFileBytes(datafilename);
  CHECK(!databytes.empty()) << datafilename;

  printf("Dropping disk cache.\n");
  CHECK(0 == system("sync"));
  CHECK(0 == system("echo 3 > /proc/sys/vm/drop_caches"));
  printf("Cache dropped.\n");

  string destfile = Util::dirplus(testdrive, "benchfile");

  Timer run_timer;
  int64_t iters = 0;

  int64_t bytes_written = 0;
  int64_t bytes_read = 0;
  int64_t bytes_correct = 0;

  int64_t short_writes = 0;
  int64_t short_reads = 0;

  double write_sec = 0.0;
  double read_sec = 0.0;
  for (;;) {
    {
      Timer write_timer;
      printf("fopen w\n");
      FILE *f = fopen(destfile.c_str(), "w");
      if (1 != fwrite(databytes.data(), databytes.size(), 1, f)) {
        short_writes++;
      }
      printf("fflush:\n");
      fflush(f);
      printf("fflush done.\n");
      fclose(f);

      // Drop caches.
      printf("between sync\n");
      CHECK(0 == system("sync"));
      CHECK(0 == system("echo 3 > /proc/sys/vm/drop_caches"));
      printf("between sync done\n");

      write_sec += write_timer.Seconds();
      bytes_written += databytes.size();
    }


    {
      Timer read_timer;
      FILE *f = fopen(destfile.c_str(), "r");
      std::vector<uint8_t> read_data;
      read_data.reserve(databytes.size());
      // Make it the right size, but also with the wrong contents, so
      // that a failing read doesn't accidentally count as correct.
      for (int i = 0; i < (int)databytes.size(); i++)
        read_data.push_back(databytes[i] ^ 0x7F);

      if (1 != fread(read_data.data(), databytes.size(), 1, f)) {
        short_reads++;
      }
      fclose(f);
      read_sec += read_timer.Seconds();
      bytes_read += databytes.size();
      for (int i = 0; i < (int)databytes.size(); i++) {
        if (databytes[i] == read_data[i]) {
          bytes_correct++;
        }
      }
    }

    // Drop caches.
    CHECK(0 == system("sync"));
    CHECK(0 == system("echo 3 > /proc/sys/vm/drop_caches"));

    iters++;
    if (run_timer.Seconds() > 10.0) {
      break;
    }
  }

  // Includes flushing etc.
  const double run_sec = run_timer.Seconds();

  printf("Results for %s, file %s (%ld bytes):\n",
         testdrive.c_str(), datafilename.c_str(),
         databytes.size());

  printf("Writes: %ld bytes in %.2fs, %.2f bytes/sec\n"
         " (%ld short writes)\n",
         bytes_written, write_sec, (double)bytes_written / write_sec,
         short_writes);

  printf("Read: %ld bytes in %.2fs, %.2f bytes/sec\n"
         " (%ld short reads) (%ld errors, accuracy: %.3f%%)\n",
         bytes_read, read_sec, (double)bytes_read / read_sec,
         short_reads,
         bytes_read - bytes_correct,
         (bytes_correct * 100.0) / bytes_read);

  printf("Finished %ld iters in %.2fs.\n"
         "%.2f iters/sec %.2f bytes/sec overall\n",
         iters, run_sec,
         (double)iters / run_sec,
         (double)(bytes_read + bytes_written) / run_sec);

  return 0;
}
