
#include "bigconv.h"

#include <string>
#include <cstring>

#include "base/logging.h"
#include "bignum/big.h"
#include "bignbr.h"
#include "base/stringprintf.h"

using namespace std;

#ifndef BIG_USE_GMP
# error This program requires GMP mode for bignum.
#endif

