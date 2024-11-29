#ifndef _FCEULIB_DRIVER_H
#define _FCEULIB_DRIVER_H

void FCEU_printf(char *format, ...);

// Displays an error.  Can block or not.
void FCEUD_PrintError(const char *s);

// Returns a pointer to 256 bytes representing
// the keyboard. XXX Maybe should be in "input"?
// This is only used in an obscure mapper.
unsigned int *FCEUD_GetKeyboard();

#endif

