
#include "driver.h"

#include <cstdio>
#include <cstdlib>
#include <stdlib.h>

/**
 * Prints an error string to STDOUT.
 */
void FCEUD_PrintError(char *s) {
  puts(s);
}

// Print error to stderr.
void FCEUD_PrintError(const char *errormsg) {
  fprintf(stderr, "%s\n", errormsg);
}

// morally FCEUD_
unsigned int *FCEUD_GetKeyboard() {
  abort();
}
