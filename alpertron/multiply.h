#ifndef _MULTIPLY_H
#define _MULTIPLY_H

// XXX make headers conditional too
#include "karatsuba.h"
#include "bigmultiply.h"

#if 0
  #define multiply multiplyKaratsuba
  #define multiplyWithBothLen multiplyWithBothLenKaratsuba
#else
  #define multiply BigMultiply
  #define multiplyWithBothLen BigMultiplyWithBothLen
#endif

#endif
