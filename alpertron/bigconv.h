
#ifndef _BIGCONV_H
#define _BIGCONV_H

#include <string>

#include "bignum/big.h"
#include "bignbr.h"

// Converts between BigInteger (alpertron), int array (alpertron),
// limbs (alpertron) and BigInt (cc-lib).
//
// For alpertron, limbs are always in little-endian order.
//
// The BigInteger representation is a struct with a fixed size array
// of limbs, and a separate sign and size.
//
// The Int Array representation is a size (usually positive, but
// negative may sometimes represent the negation of the number?)
// followed by limbs.
//
// The limbs representation is just a pointer to limbs, where the
// size is passed around separately (sometimes in some global state,
// like when everything is being computed mod some number).

// Writes the BigInt to the beginning of the array, with the size in
// the first element. Returns the number of ints written, which is
// always at least 2 (size and limb).
int BigIntToArray(const BigInt &b, int *arr);
void BigIntToBigInteger(const BigInt &b, BigInteger *g);

bool ParseBigInteger(const char *str, BigInteger *big);

BigInt BigIntegerToBigInt(const BigInteger *g);
// With length as first word.
BigInt ArrayToBigInt(const int *arr);
BigInt BigIntegerToBigInt(const BigInteger *g);
BigInt LimbsToBigInt(const limb *limbs, int num_limbs);
// Writes to limbs (must be enough space). Returns num_limbs.
int BigIntToLimbs(const BigInt &b, limb *limbs);
// Zero-padded. Must be enough space.
void BigIntToFixedLimbs(const BigInt &b, size_t num_limbs, limb *limbs);

std::string BigIntegerToString(const BigInteger *g);

#endif
