
#include "bignum/big.h"
#include <string>
#include <vector>
#include <cstdint>

#include "ansi.h"

int main(int argc, char **argv) {
  ANSI::Init();

  BigInt a(360721);
  BigInt c(-1);

  BigInt x("35788163037699688619610394924982525579945157163345352020379195426768386648966107291553816907454124917927061822509551921333041670289288629820859356400236794119990009");
  BigInt y("21494389788386309709588402432960635231487947702588945479192410900234196463471861669937395560253207706490256148919403065294170220561777147584068622368355498225879506280");

  BigInt xx = BigInt::Times(x, x);
  BigInt yy = BigInt::Times(y, y);

  BigInt res = BigInt::Plus(BigInt::Times(a, xx), BigInt::Times(c, yy));

  printf("%s * %s^2 + %s * %s^2 = %s\n",
         a.ToString().c_str(),
         x.ToString().c_str(),
         c.ToString().c_str(),
         y.ToString().c_str(),
         res.ToString().c_str());

  BigInt u(222121);
  BigInt uxx = BigInt::Times(u, xx);
  printf("%s * %s^2 = %s\n",
         u.ToString().c_str(),
         x.ToString().c_str(),
         uxx.ToString().c_str());

  return 0;
}
