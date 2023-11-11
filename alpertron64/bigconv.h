
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
// of limbs, and a separate sign and size. (This is obsolete!)
//
// The Int Array representation is a size (usually positive, but
// negative may sometimes represent the negation of the number?)
// followed by limbs. (This is obsolete!)
//
// The limbs representation is just a pointer to limbs, where the
// size is passed around separately (sometimes in some global state,
// like when everything is being computed mod some number).

// Return the number of limbs needed to represent the BigInt in alpertron's
// format. Always at least one.
int BigIntNumLimbs(const BigInt &b);

BigInt LimbsToBigInt(const limb *limbs, int num_limbs);
// Writes to limbs (must be enough space). Returns num_limbs.
int BigIntToLimbs(const BigInt &b, limb *limbs);
// Zero-padded. Must be enough space.
// LimbsToBigInt can be used as the inverse, as it will just
// ignore the zero padding.
void BigIntToFixedLimbs(const BigInt &b, size_t num_limbs, limb *limbs);

#endif
