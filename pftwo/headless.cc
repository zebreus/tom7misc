
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <string>
#include <unistd.h>

#ifdef __MINGW32__
#define byte win_byte_override
#include <windows.h>
#undef byte
#undef ARRAYSIZE
#undef CopyMemory
#endif

#include "options.h"
#include "base/stringprintf.h"
#include "nice.h"
#include "treesearch.h"
#include "util.h"

using namespace std;
using int64 = int64_t;

// Note: This is not actually a std::thread -- it's really the main
// thread. SDL really doesn't like being called outside the main
// thread, even if it's exclusive.
struct ConsoleThread {
  explicit ConsoleThread(TreeSearch *search) : search(search) {}

  void Run() {
    const int64 start = time(nullptr);
    int64 last_wrote = 0LL;
    int64 last_minute = 0LL;

    for (;;) {
      frame++;
      sleep(1);
      const int64 elapsed = time(nullptr) - start;
      search->SetApproximateSeconds(elapsed);

      constexpr int64 TEN_MINUTES = 10LL * 60LL;
      // Every ten minutes, write FM2 file.
      // We don't bother if running an experment, since these only
      // run for a few hours and in batch (so the checkpoint files
      // get mixed up anyway).
      if (experiment_file.empty() && elapsed - last_wrote > TEN_MINUTES) {
        string filename_part = StringPrintf("frame-%lld", frame);
        string filename = search->SaveBestMovie(filename_part);
        // XXX races are possible, and Util::CopyFile does byte-by-byte.
        // Should use posix link?
        (void)Util::RemoveFile("latest.fm2");
        if (!Util::CopyFileBytes(filename, "latest.fm2")) {
          printf("Couldn't copy to latest.fm2?\n");
        }
        last_wrote = elapsed;
      }

      const int64 total_nes_frames = search->UpdateApproximateNesFrames();

      int64 minutes = elapsed / 60LL;
      int64 hours = minutes / 60LL;
      if (minutes != last_minute) {
        last_minute = minutes;
        const int64 sec = elapsed % 60LL;
        const int64 min = minutes % 60LL;
        string es = experiment_file.empty()
                        ? ""
                        : StringPrintf(" [%s]", experiment_file.c_str());

        search->PrintPerfCounters();
        string pct;
        if (max_nes_frames > 0LL) {
          pct = StringPrintf(" (%.1f%%)",
                             (100.0 * total_nes_frames) / max_nes_frames);
        }
        printf("%02d:%02d:%02d  %lld NES Frames%s; %.2fKframes/sec%s\n",
               (int)hours, (int)min, (int)sec, total_nes_frames, pct.c_str(),
               (double)total_nes_frames / ((double)elapsed * 1000.0),
               es.c_str());
        fflush(stdout);
      }

      if (max_nes_frames > 0LL && total_nes_frames > max_nes_frames) {
        if (!experiment_file.empty()) {
          string f = search->SaveBestMovie(experiment_file);
          printf("Wrote final experiment results to %s\n", f.c_str());
        }
        return;
      }
    }
  }

  // If positive, then stop after about this many NES frames.
  int64 max_nes_frames = 0LL;
  // And write the final movie here.
  string experiment_file;

 private:
  TreeSearch *search = nullptr;
  int64 frame = 0LL;
};

int main(int argc, char *argv[]) {
  Nice::SetLowPriority();

  Options options;

  // XXX not general-purpose!
  string experiment;
  if (argc >= 2) {
    double d = std::stod((string)argv[1]);
    printf("\n***\nStarting experiment with value %.4f\n***\n\n", d);
    // options.frames_stddev = d;
    // options.node_batch_size = (int)d;
    options.p_stay_on_node = d;
    options.random_seed = (int)(d * 100002.0);
    experiment = std::format("expt-{}", argv[1]);
  }

  {
    TreeSearch search{options};
    search.StartThreads();
    {
      ConsoleThread *console_thread = new ConsoleThread(&search);
      if (!experiment.empty()) {
        console_thread->max_nes_frames = 3600 * 2 * 20000LL;
        console_thread->experiment_file = experiment;
      }
      console_thread->Run();
      delete console_thread;
    }
    search.DestroyThreads();
  }

  return 0;
}
