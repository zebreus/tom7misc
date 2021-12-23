#ifndef __ELIFE_H
#define __ELIFE_H

class elife {
public:
      elife();
     ~elife();

      advance();
      init();

      unsigned char data[256];
      unsigned char IP;

      unsigned char regs[16];
};

#endif
