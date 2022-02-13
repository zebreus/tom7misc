
#include "emulator.h"

#include <cmath>
#include <string>
#include <vector>
#include <memory>
#include <sys/time.h>
#include <sstream>
#include <unistd.h>
#include <cstdio>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "test-util.h"
#include "rle.h"
#include "simplefm2.h"
#include "simplefm7.h"
#include "timer.h"

#include "x6502.h"

static constexpr const char *ROMFILE = "mario.nes";
static constexpr uint64 expected_nes = 0x0c8abfc012bf6c84ULL;
static constexpr uint64 expected_img = 0x9c8975828c9578a7ULL;

std::tuple<uint64, uint64, double> RunBenchmark(Emulator *emu,
                                                const vector<uint8> &start,
                                                const vector<uint8> &movie) {
  emu->LoadUncompressed(start);
  Timer exec_timer;

  // Only the last step needs to be full, so that we render the image.
  for (int i = 0; i < (int)movie.size() - 1; i++)
    emu->Step(movie[i], 0);
  emu->StepFull(movie[movie.size() - 1], 0);

  const double exec_seconds = exec_timer.Seconds();
  return make_tuple(emu->MachineChecksum(), emu->ImageChecksum(),
                    exec_seconds);
}

int main(int argc, char **argv) {
  string romdir = "roms/";

  Timer startup_timer;
  // TODO: This is not really fair since it counts all the IO.
  std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
  CHECK(emu.get() != nullptr);
  const double startup_seconds = startup_timer.Seconds();
  vector<uint8> start = emu->SaveUncompressed();
  
  vector<uint8> movie = SimpleFM7::ReadInputs("mario-long.fm7");
  CHECK(!movie.empty());

  printf("Benchmarking %s. Startup in %0.4f sec...\n",
         ROMFILE, startup_seconds);
  
  double exec_seconds = -1.0;
  
  int executions = 0;
  double total_time = 0.0;
  vector<int> last_means;
  for (int i = 0; /* exit upon convergence */; i++) {
    const auto [ram, img, sec] = RunBenchmark(emu.get(), start, movie);
    executions++;
    total_time += sec;
    double mean = total_time / (double)executions;
    // TODO: Use actual variance to compute convergence. This
    // depends too much on base 10 (e.g. if the mean is very close
    // to the rounding boundary, it will likely run more times).
    int mtrunc = (int)mean; // XXX (int)(round(mean * 10.0));
    if (last_means.size() >= 5) {
      if ([&]() {
          for (int lm : last_means) {
            if (lm != mtrunc) return false;
          }
          return true;
        }()) {
        // Convergence!
        exec_seconds = mean;
        break;
      }
      // Discard oldest to keep 5 means.
      last_means.erase(last_means.begin());
    }
    last_means.push_back(mtrunc);
    printf("Round %4d in %.4f, mean %.4f\n", executions, sec, mean);
    fflush(stdout);
    break;
  }
  
  const uint64 nes_checksum = emu->MachineChecksum();
  const uint64 img_checksum = emu->ImageChecksum();
  fprintf(stderr,
          "NES checksum: %016llx\n"
          "Img checksum: %016llx\n",
          nes_checksum,
          img_checksum);

  fprintf(stderr,
          "Startup time: %.4fs\n"
          "Exec time:    %.4fs\n",
          startup_seconds, exec_seconds);
  
  int status = 0;
  if (nes_checksum != expected_nes) {
    fprintf(stderr, "*** NES checksum mismatch. "
            "Got %016llx; wanted %016llx!\n",
            nes_checksum, expected_nes);
    status = -1;
  }
  if (img_checksum != expected_img) {
    fprintf(stderr, "*** Img checksum mismatch. "
            "Got %016llx; wanted %016llx!\n",
            img_checksum, expected_img);
    status = -1;
  }
  
  return status;
}
