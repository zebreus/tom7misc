
#include <cstdint>
#include <vector>
#include <string>
#include <optional>

#include "base/print.h"
#include "util.h"
#include "bignum/big.h"
#include "csr.h"
#include "ansi.h"

static void TestCSR() {
  std::optional<MultiRSA::Key> okey =
    MultiRSA::KeyFromPrimes(std::vector<BigInt>{
      BigInt("1452183499217746936988050278709957755003934074480911"),
      BigInt("2696423543995307208401026789711225014797731381216367"),
      BigInt("2465074989005639593525836528988016925392176901709149")});
  CHECK(okey.has_value());

  std::vector<std::string> aliases = {
    "*.tom7.org",
    "virus.exe.tom7.org",
  };
  std::vector<uint8_t> csr = CSR::Encode("tom7.org", aliases, okey.value());

  // openssl req -inform DER -in test.csr -text -noout -verify
  Util::WriteFileBytes("test.csr", csr);
  Print("Wrote test.csr\n");
}

static void TestCRI() {
  std::vector<std::string> aliases = {
    "*.tom7.org",
    "virus.exe.tom7.org",
  };
  std::vector<uint8_t> cri = CSR::CertificationRequestInfo(
      "tom7.org",
      aliases,
      BigInt(31337), BigInt(7));

  // openssl asn1parse -inform DER -in test.cri -i
  Util::WriteFileBytes("test.cri", cri);
  Print("Wrote test.cri\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  Print("This test writes files you can verify with OpenSSL.\n"
        "See the source code for the commands.\n");
  TestCRI();
  TestCSR();

  Print("OK\n");
  return 0;
}
