
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

#endif
