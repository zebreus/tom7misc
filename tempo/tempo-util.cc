#include "tempo-util.h"

#include <sys/sysinfo.h>
#include <string>

#include "base/stringprintf.h"

using namespace std;

// Return some basic system info as a plain ascii string formatted
// with newlines.
string SysInfoString() {
  struct sysinfo info;
  if (0 == sysinfo(&info)) {
    return StringPrintf(
        "linux uptime: %ld sec\n"
        "load: %ld %ld %ld\n"
        "free ram: %ld / %ld\n"
        "procs: %d\n",
        info.uptime,
        info.loads[0], info.loads[1], info.loads[2],
        info.freeram, info.totalram,
        (int)info.procs);
  } else {
    return "sysinfo() failed\n";
  }
}
