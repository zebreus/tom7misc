#ifndef _QUAD_H
#define _QUAD_H

#include <string>

#include "bignum/big.h"

// If output is non-null, writes readable HTML there.
void QuadBigInt(bool teach,
                const BigInt &a, const BigInt &b, const BigInt &c,
                const BigInt &d, const BigInt &e, const BigInt &f,
                std::string *output);

#endif
