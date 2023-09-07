
#ifndef _BIGCONV_H
#define _BIGCONV_H

#include "bignum/big.h"
#include "bignbr.h"

// Converts between BigInteger (alpertron), int array (alpertron)
// and BigInt (cc-lib).

// Writes the BigInt to the beginning of the array. Returns the
// number of ints written, which is always at least 2 (size and
// limb).
int BigIntToArray(const BigInt &b, int *arr);
void BigIntToBigInteger(const BigInt &b, BigInteger *g);

bool ParseBigInteger(const char *str, BigInteger *big);

BigInt BigIntegerToBigInt(const BigInteger *g);
// With length as first word.
BigInt ArrayToBigInt(int *arr);
BigInt BigIntegerToBigInt(const BigInteger *g);
BigInt LimbsToBigInt(const limb *limbs, int num_limbs);
// Writes to limbs (must be enough space). Returns num_limbs.
int BigIntToLimbs(const BigInt &b, limb *limbs);

#endif
