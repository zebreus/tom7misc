#ifndef _QUAD_H
#define _QUAD_H

#include <string>

// If output is non-null, writes readable HTML there.
void quadBigInt(bool teach,
                BigInteger *a, BigInteger *b, BigInteger *c,
                BigInteger *d, BigInteger *e, BigInteger *f,
                std::string *output);

#endif
