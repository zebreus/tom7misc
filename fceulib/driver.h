#ifndef _FCEULIB_DRIVER_H
#define _FCEULIB_DRIVER_H

#include <stdio.h>
#include <string>
#include <iosfwd>

#include "types.h"
#include "git.h"
#include "file.h"

void FCEU_printf(char *format, ...);

// Displays an error.  Can block or not.
void FCEUD_PrintError(const char *s);

#endif

