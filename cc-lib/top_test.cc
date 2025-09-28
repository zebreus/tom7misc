
#include "top.h"

#include <string>

#include "base/print.h"

using namespace std;

int main(int argc, char **argv) {

  // Note: Can't fail! Perhaps could search for argv[0]?
  for (const string &proc : Top::Enumerate()) {
    Print("{}\n", proc);
  }

  return 0;
}
