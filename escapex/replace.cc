
/* upgrade trampoline for windows only
   does a series of moves (on the command line as
   replace.exe    src dst   src dst   src dst)

   then execs:
     replace.exe -upgraded


   Replace waits a few seconds and tries again on
   failure, in order to avoid race conditions.
 */

#ifndef WIN32
# error "replace.cc is only for win32 builds!"
#endif

#include <cstdlib>
#include <format>
#include <string>

#include <windows.h>
#include <malloc.h>

#include <unistd.h>

#include "escape-util.h"

using namespace std;

int main(int argc, char **argv) {

  if (argc >= 2) {

    string execafter = argv[1];

    /* ignores odd argument */
    for (int i = 2; i < (argc - 1); ) {
      int tries = 3;

      while (tries--) {
        if ((EscapeUtil::remove(argv[i + 1]) &&
             EscapeUtil::move(argv[i], argv[i+1]))) goto success;

        sleep(1);
      }

      {
        std::string msg =
          std::format("failed to remove {} or move {} to {}",
                      argv[i + 1], argv[i], argv[i + 1]);
        MessageBox(0, msg.c_str(), "upgrade problem?", 0);
      }

    success:
      i += 2;
    }

    for (int tries = 0; tries < 3; tries++) {
      spawnl(_P_OVERLAY, execafter.c_str(), execafter.c_str(),
             "-upgraded", 0);

      /* ok: win32 only */
      sleep(1 << tries);
    }

    MessageBox(0, "Exec failed\n", "upgrade incomplete", 0);
    return -2;

  } else {
    MessageBox(0,
               "replace.exe is used during the upgrade process and isn't\n"
               "very interesting to just run by itself.\n\n", "oops",
               0);
    return -1;
  }
}
