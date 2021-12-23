#include "elife.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

elife :: elife() {
      // initialize to randoms.
      init();
}

elife :: init() {
      srandom(time(NULL));
      for (int x=0;x<256;x++)
          data[x] = random()&255;
      IP=0;
      for (int x=0;x<16;x++) regs[x] = 0;

}

elife :: ~elife() {}

elife :: advance() {
#define dz(c) (data[(c)&255])
if (data[IP] & 128) {
   // high bit set. We have 7 different opcodes with a register embedded.
   int reg = data[IP] & 15;
   switch (7&(data[IP]>>4)) {
          case 0: if (regs[reg])  IP = dz(IP+1); else IP+=2; break;
          case 1: if (!regs[reg]) IP = dz(IP+1); else IP+=2; break;
          case 2: regs[reg] = dz(IP+1); IP +=2; break;
          case 3: regs[reg] += dz(IP+1); IP +=2; break;
          case 4: regs[reg] -= dz(IP+1); IP +=2; break;
          case 5: data[dz(IP+1)] = regs[reg]; IP+=2; break;
          case 6: regs[reg] = ~regs[reg]; IP ++; break;
          case 7: regs[reg] = 0; IP++; break;
   }

} else {
      switch (data[IP]) {
      case 0: // ABSJUMP
           IP = data[(IP+1)&255]; break;
      case 1: // RELJUMP
           IP += data[(IP+1)&255]; break;
      case 2: // BACKJUMP
           IP -= data[(IP+1)&255]; break;
      case 3: // MOV
           data[dz(IP+1)] = data[dz(IP+2)]; IP+=3; break;
      case 4: // RMOV
           data[dz(IP+2)] = data[dz(IP+1)]; IP+=3; break;
      case 5: // INC
           data[dz(IP+1)]++; IP += 2; break;
      case 6: // DEC
           data[dz(IP+1)]--; IP += 2; break;
      case 7: // ADD
           data[dz(IP+1)] += dz(IP+2); IP +=3; break;
      case 8: // [ADD]
           data[dz(IP+1)] += data[dz(IP+2)]; IP+=3; break;
      case 9: // SUB
           data[dz(IP+1)] -= dz(IP+2); IP +=3; break;
      case 10: // [SUB]
           data[dz(IP+1)] += data[dz(IP+2)]; IP+=3; break;
      case 11: // XOR
           data[dz(IP+1)] ^= dz(IP+2); IP += 3; break;
      case 12: // [XOR]
           data[dz(IP+1)] ^= data[dz(IP+2)]; IP+=3; break;
      case 13: // NOT
           dz(IP+1) = ~dz(IP+1); IP+=2; break;
      case 14: // [NOT]
           data[dz(IP+1)] = ~data[dz(IP+1)]; IP+=2; break;
      case 15: // XCHG
           {int n = regs[dz(IP+1)>>4]; regs[dz(IP+1)>>4]=
                regs[dz(IP+1)&7]; regs[dz(IP+1)&7]=n; IP+=2; break; }
      case 16: // BLITREG
           data[dz(IP+1)] = regs[dz(IP+2)&7]; IP+=3; break;
      case 17: // LOADREG
           regs[dz(IP+1)&7] = data[dz(IP+2)]; IP +=3; break;
      default: // NOP
      IP++;
      }
}
}

