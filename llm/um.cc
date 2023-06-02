#include "um.h"

#include <stdlib.h>
#include <string.h>
#include <cstdint>
#include <cassert>
#include <functional>

// This used to assume pointers are 32-bit, but for 64 bit machines
// we need to do some indirection.

uint32_t UM::ulloc(uint32_t size) {
  // First word stores size.
  uint32_t *r = (uint32_t*)calloc((1 + size), sizeof (uint32_t));
  *r = size;
  if (freelist.empty()) {
    uint32_t id = mem.size();
    mem.push_back(r);
    return id;
  } else {
    uint32_t id = freelist.back();
    freelist.pop_back();
    mem[id] = r;
    return id;
  }
}

void UM::ufree(uint32_t id) {
  free(mem[id]);
  mem[id] = nullptr;
  freelist.push_back(id);
}

void UM::resize(uint32_t id, uint32_t size) {
  free(mem[id]);
  mem[id] = (uint32_t*)calloc((1 + size), sizeof (uint32_t));
  *mem[id] = size;
}


UM::UM(const std::vector<uint8_t> &contents) {
  assert(contents.size() % 4 == 0);

  // Expect first allocation to return 0.
  [[maybe_unused]]
  int z = ulloc(contents.size() >> 2);
  assert(0 == z);

  uint32_t *zeropage = arr(0);

  {
    int i = 0;
    for (int x = 0; x < (int)contents.size(); x++) {
      zeropage[i] <<= 8;
      zeropage[i] |= contents[x];

      if (x % 4 == 3) {
        i++;
      }
    }
  }
}

void UM::Run(const std::function<int()> &GetChar,
             const std::function<void(uint8_t)> &PutChar) {
  // This pointer always has the value of arr(0).
  uint32_t *zeropage = arr(0);

  /* spin cycle */
  for(;;) {
    const uint32_t w = zeropage[ip++];

    const uint8_t c = w & 7;
    const uint8_t b = (w >> 3) & 7;
    const uint8_t a = (w >> 6) & 7;

    switch (w >> 28) {
    case 0: if (reg[c]) reg[a] = reg[b]; break;
    case 1: reg[a] = arr(reg[b])[reg[c]]; break;
    case 2: arr(reg[a])[reg[b]] = reg[c]; break;
    case 3: reg[a] = reg[b] + reg[c]; break;
    case 4: reg[a] = reg[b] * reg[c]; break;
    case 5: reg[a] = reg[b] / reg[c]; break;
    case 6: reg[a] = ~(reg[b] & reg[c]); break;
    case 7: return;
    case 8: reg[b] = (uint32_t)ulloc(reg[c]); break;
    case 9: ufree(reg[c]); break;
    case 10: PutChar(reg[c]); break;
    case 11: reg[c] = GetChar(); break;
    case 12:
      // No need to "copy" if source is zero.
      if (reg[b] != 0) {
        const uint32_t s = usize(reg[b]);
        resize(0, s);
        zeropage = arr(0);
        memcpy(zeropage, arr(reg[b]), s * 4);
      }
      ip = reg[c];
      break;
    case 13: reg[7 & (w >> 25)] = w & 0177777777; break;
    }
  }
}
