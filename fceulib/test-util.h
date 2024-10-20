// This is a clone of cc-lib/base.h, but only included for the
// testing code in this directory, since FCEULib doesn't need it
// and I'd like to keep its dependencies manageable.
//
// XXX: Since this should only be used in tools (where I am already
// depending on cc-lib) I should just switch to the unforked version
// in cc-lib!

#ifndef _FCEULIB_TESTUTIL_H
#define _FCEULIB_TESTUTIL_H

#include <vector>
#include <string>
#include <utility>
#include <cstdint>

#ifdef __GNUC__
#include <ext/hash_map>
#include <ext/hash_set>
#else
#include <hash_map>
#include <hash_set>
#endif

#ifdef __GNUC__
namespace std {
using namespace __gnu_cxx;
}

// Needed on cygwin, at least. Maybe not all gnuc?
namespace __gnu_cxx {
template<>
struct hash< unsigned long long > {
  size_t operator()( const unsigned long long& x ) const {
    return x;
  }
};
}
#endif

// Can probably retire this; little chance that we compile with
// MSVC any more, and anyway we should be using uint8.
#ifdef COMPILER_MSVC
// http://msdn.microsoft.com/en-us/library/b0084kay(v=vs.71).aspx
#ifndef _CHAR_UNSIGNED
#error Please compile with /J for unsigned chars!
#endif
#endif

std::string ReadFile(const std::string &s);
std::vector<std::string> ReadFileToLines(const std::string &s);
std::vector<std::string> SplitToLines(const std::string &s);
std::string Chop(std::string &s);
std::string LoseWhiteL(const std::string &s);
bool ExistsFile(const std::string &s);

// Generally we just want reliable and portable names for specific
// word sizes. C++11 actually gives these to us now; no more
// "well, long long is at least big enough to hold 64 bits, and
// chars might actually be 9 bits, etc.".

using int8 = int8_t;
using uint8 = uint8_t;
using int16 = int16_t;
using uint16 = uint16_t;
using int32 = int32_t;
using uint32 = uint32_t;
using int64 = int64_t;
using uint64 = uint64_t;

// I think that the standard now REQUIRES the following assertions to
// succeed. But if some shenanigans are going on, let's get out of
// here.
static_assert(UINT8_MAX == 255, "Want 8-bit chars.");
static_assert(sizeof (uint8) == 1, "8 bits is one byte.");
static_assert(sizeof (int8) == 1, "8 bits is one byte.");

static_assert(sizeof (int16) == 2, "16 bits is two bytes.");
static_assert(sizeof (uint16) == 2, "16 bits is two bytes.");

static_assert(sizeof (int32) == 4, "32 bits is four bytes.");
static_assert(sizeof (uint32) == 4, "32 bits is four bytes.");

static_assert(sizeof (int64) == 8, "64 bits is eight bytes.");
static_assert(sizeof (uint64) == 8, "64 bits is eight bytes.");

#endif
