
#include "periodically.h"

#include <string>
#include <optional>
#include <memory>

#include "base/logging.h"

// We could do better here (e.g. see how much total time is taken)
// but for now you need to watch to see that we get about 4 a second.
static void WatchTest() {
  Periodically pe(0.25);
  int count = 0;
  while (count < 10) {
	if (pe.ShouldRun()) {
	  printf("%d\n", count);
	  count++;
	}
  }
}

int main(int argc, char **argv) {
  WatchTest();

  printf("OK\n");
  return 0;
}
