
#include <string>
#include <memory>
#include <cstdio>
#include <malloc.h>
#include <cstdint>

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"
#include "../fceulib/x6502.h"

using int64 = int64_t;

// TODO: Cross-platform wrapper for this.
#if 0
// on linux
static inline int64 HeapUsage() {
  return mallinfo().uordblks;
}
#endif

// on windows
#include <windows.h>
static int64 HeapUsage() {
  _HEAPINFO hinfo;
  hinfo._pentry = nullptr;
  int64 total_size = 0;
  for (;;) {
    int heapstatus = _heapwalk(&hinfo);
    if (heapstatus == _HEAPEND ||
        heapstatus == _HEAPEMPTY)
      break;

    // Otherwise, it's corrupted...
    CHECK(heapstatus == _HEAPOK) << heapstatus;
    
    if (hinfo._useflag == _USEDENTRY) {
      total_size += hinfo._size;
    }
  }
  return total_size;
}

int main(int argc, char **argv) {

  int allocated = HeapUsage();
  for (int i = 0; i < 20; i++) {
  
  std::unique_ptr<Emulator> emu(
      Emulator::Create("tetris.nes"));

  for (int i = 0; i < 1000; i++)
    emu->Step(0, 0);
  
  int postallocated = HeapUsage();

  emu.reset();
  
  int clearallocated = HeapUsage();
  
  printf("Start: %d\n"
         "Load emu: %d (+ %d)\n"
         "Clear emu: %d (+ %d)\n",
         allocated,
         postallocated, postallocated - allocated,
         clearallocated, clearallocated - allocated);
  }
  
  return 0;
}
