
#include "movie-maker.h"

#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>

#include "threadutil.h"
#include "arcfour.h"
#include "randutil.h"
#include "timer.h"

#ifdef __MINGW32__
#include <windows.h>
#endif

using namespace std;
using uint64 = uint64_t;

#define ANSI_RED "\x1B[1;31;40m"
#define ANSI_GREY "\x1B[1;30;40m"
#define ANSI_BLUE "\x1B[1;34;40m"
#define ANSI_CYAN "\x1B[1;36;40m"
#define ANSI_YELLOW "\x1B[1;33;40m"
#define ANSI_GREEN "\x1B[1;32;40m"
#define ANSI_WHITE "\x1B[1;37;40m"
#define ANSI_PURPLE "\x1B[1;35;40m"
#define ANSI_RESET "\x1B[m"

static void CPrintf(const char* format, ...) {
  // Do formatting.
  va_list ap;
  va_start(ap, format);
  string result;
  StringAppendV(&result, format, ap);
  va_end(ap);

  #ifdef __MINGW32__
  DWORD n = 0;
  WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE),
               result.c_str(),
               result.size(),
               &n,
               nullptr);
  #else
  printf("%s", result.c_str());
  #endif
}


static constexpr int NUM_THREADS = 4;


static string VecBytes(const std::vector<uint8> &v) {
  string out;
  for (uint8 b : v) {
    StringAppendF(&out, "%02x", b);
  }
  return out;
}

static void RunOne(uint64 run_seed) {
  ArcFour rc(StringPrintf("run.%llx", run_seed));

  const uint64_t mm_seed = Rand64(&rc);
  vector<uint8> encode;
  for (int i = 0; i < 8; i++) encode.push_back(rc.Byte());

  Timer one_timer;
  MovieMaker mm("solutions.txt", "tetris.nes", mm_seed);

  CPrintf("Encode [" ANSI_YELLOW "%s" ANSI_RESET "] "
          "with seed [" ANSI_WHITE "%llx" ANSI_RESET "]\n",
          VecBytes(encode).c_str(), mm_seed);

  auto Stuck = [mm_seed, encode, &one_timer]() {
      return StringPrintf("Encoding [%s] with seed %llx, apparently stuck "
                          "after %.1fs", VecBytes(encode).c_str(), mm_seed,
                          one_timer.Seconds());
    };

  MovieMaker::Callbacks callbacks;
  callbacks.retried = [&one_timer, &Stuck](
      const Emulator &emu, int retry_count, Piece expected) {
      CHECK(retry_count < 10000) << Stuck();
      CHECK(one_timer.Seconds() < 60 * 10) << Stuck();
    };

  callbacks.made_step = [&one_timer, &Stuck](
      const Emulator &emu, int64 steps) {
      CHECK(steps < 200000) << Stuck();
      CHECK(one_timer.Seconds() < 60 * 10) << Stuck();
    };

  vector<uint8> movie = mm.Play(encode, callbacks);

  CPrintf("Done: [" ANSI_YELLOW "%s" ANSI_RESET "] "
          "with seed [" ANSI_WHITE "%llx" ANSI_RESET "], "
          ANSI_PURPLE "%.2f" ANSI_WHITE "s"
          ANSI_RESET "\n",
          VecBytes(encode).c_str(), mm_seed, one_timer.Seconds());
}

static void RunForever() {

  ArcFour rc(StringPrintf("mmt.%lld", time(nullptr)));

  Asynchronously async(NUM_THREADS);

  Timer run_timer;
  static constexpr int REPORT_EVERY = 30;
  int64 next_report = 0;

  std::mutex m;
  int64 num_done = 0;
  for (;;) {
    const uint64 seed = Rand64(&rc);
    async.Run([&m, &num_done, seed]() {
        RunOne(seed);
        {
          MutexLock ml(&m);
          num_done++;
          // more stats?
        }
      });


    if (run_timer.Seconds() > next_report) {
      {
        MutexLock ml(&m);
        num_done++;
        double sec = run_timer.Seconds();
        CPrintf(ANSI_WHITE "%lld " ANSI_GREY " done in "
                ANSI_BLUE "%.2f" ANSI_WHITE "s"
                ANSI_GREY " = " ANSI_PURPLE "%.4f "
                ANSI_WHITE "bytes" ANSI_GREY "/" ANSI_WHITE "sec"
                ANSI_RESET "\n",
                num_done, sec, (num_done * 8.0) / sec);
      }
      next_report = run_timer.Seconds() + REPORT_EVERY;
    }

  }
}


int main(int argc, char **argv) {
  #ifdef __MINGW32__
  if (!SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)) {
    LOG(FATAL) << "Unable to go to BELOW_NORMAL priority.\n";
  }

  HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
  // mingw headers may not know about this new flag
  static constexpr int kVirtualTerminalProcessing = 0x0004;
  DWORD old_mode = 0;
  GetConsoleMode(hStdOut, &old_mode);
  SetConsoleMode(hStdOut, old_mode | kVirtualTerminalProcessing);
  #endif

  RunForever();
  return 0;
}
