#include <conio.h>
#include <stdlib.h>
#include <stdio.h>
#include <pc.h>

#include "elife.h"

int ITERATION=0;
int continuous=0;
main () {
elife mandark;
clrscr();
_setcursortype(_NOCURSOR);
while (1) {
        ScreenSetCursor(0,0);
        for (int x=0;x<256;x++) {
            if (!(x&15)) cprintf("\r\n");
            if (mandark.IP == x) textcolor(15); else textcolor((x&1)?7:3);
            cprintf ("%02X",mandark.data[x]);
        }
        cprintf ("\r\n\r\n");
        textcolor(3);
        for (int x=0;x<16;x++)
            cprintf("%02X ",mandark.regs[x]);
        textcolor(7);
        cprintf ("\r\n\r\n%d\r\n", ITERATION);
                mandark.advance();
                ITERATION ++;
        char a=0;
        if ((!continuous) || kbhit()) if ('q' == (a = getch())) break;
        if (a=='c') continuous^=1;
        if (a=='z') { mandark.init(); ITERATION=0; }
}
_setcursortype(_NORMALCURSOR);
}
