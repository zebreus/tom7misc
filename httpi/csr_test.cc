
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "bignum/big.h"
#include "csr.h"
#include "multi-rsa.h"
#include "pem.h"
#include "util.h"

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

static void TestExpiration() {
  constexpr std::string_view CERT = R"(
-----BEGIN CERTIFICATE-----
MIIFMzCCBBugAwIBAgISBvoWn7+3czOWvLkGJd6YcJygMA0GCSqGSIb3DQEBCwUA
MDMxCzAJBgNVBAYTAlVTMRYwFAYDVQQKEw1MZXQncyBFbmNyeXB0MQwwCgYDVQQD
EwNSMTMwHhcNMjUxMTIzMTczMTEzWhcNMjYwMjIxMTczMTEyWjATMREwDwYDVQQD
Ewh0b203Lm9yZzCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAJ0DSZBX
VGvI2bmvmx98xRqIeW+xCGALHB6Zgu6NCVUGIY+0RAttttIfGxsT1oJBqxtOci2v
yL5Ue+yJz/8VIsig6YtZUrO9q3GGB3WRb5JftCPB6ZTjrA9MwBvtMC5nMWzfum2r
etPeP0GN8e7C8NPvt94qnGiCR6U5G+DxVvruPIVEQn0occPjE1on1kUUogY7uxWz
uIeiXEVecfMYSpnJvHlusbhrdPBzxlCxILfPoAeigAilVjJQIzsCuKU94QfkP1Lf
oKLnFPWG21kyKV4V33sOJXz6T6130q/aRVXtW0+2DOEd0V064LLCS1AK+mu1K8BB
EqAbiOY6R8JY9X0CAwEAAaOCAl8wggJbMA4GA1UdDwEB/wQEAwIFoDAdBgNVHSUE
FjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDAYDVR0TAQH/BAIwADAdBgNVHQ4EFgQU
DpYx3scwv1riTThp/1K72BxLV/IwHwYDVR0jBBgwFoAU56ufDywzoFPTXk94yLKE
DjvWkjMwMwYIKwYBBQUHAQEEJzAlMCMGCCsGAQUFBzAChhdodHRwOi8vcjEzLmku
bGVuY3Iub3JnLzBZBgNVHREEUjBQghgqLmNhcm5hZ2UtbWVsb24udG9tNy5vcmeC
DioubXAzLnRvbTcub3Jngg4qLnBhYy50b203Lm9yZ4IKKi50b203Lm9yZ4IIdG9t
Ny5vcmcwEwYDVR0gBAwwCjAIBgZngQwBAgEwLgYDVR0fBCcwJTAjoCGgH4YdaHR0
cDovL3IxMy5jLmxlbmNyLm9yZy8zMy5jcmwwggEFBgorBgEEAdZ5AgQCBIH2BIHz
APEAdwAZhtTHKKpv/roDb3gqTQGRqs4tcjEPrs5dcEEtJUzH1AAAAZqx+qFSAAAE
AwBIMEYCIQDbr5vdilM8asPCs3UWyA47S3fF3XGuDm+QcAElVrYX1AIhALSvBINK
3ckauDDFUhLMCVQ12Vnfd8Sb7cRNARVqiCb6AHYADleUvPOuqT4zGyyZB7P3kN+b
wj1xMiXdIaklrGHFTiEAAAGasfqhUAAABAMARzBFAiBupdQVqXBmcE6R6aQRsTBa
lzT3BfbIR+FJcflGhxatMgIhAND07FuevUeddRDBI6bC01LWvFhIJmpI8Sq0qnRP
iwAXMA0GCSqGSIb3DQEBCwUAA4IBAQBwTmwHSkkVD5E0E+SiyuRI63vUH4Zj6TEE
0CvlmdO9O5e+uydZz2W7S/XsbH7hJNHwd8ouPT3RxkIYDvRR9T9Bq/1BHNZQ/koc
NfI9DuEi0QDX5LCH6Dgpck1y+fd2gdtM2NtCVS5812zenQvUOuWC78pFjrABbhrq
ZfuaS248SqBvjWmC+YhUtTR5gjpRaGAXo2O6MkMvrYo8VmW6EaGz0JSQ7wX5gd1+
n2I7avtKffv2yDvx9KgBaEI+Ms7kOB+eJbIcU2XZLQeYxtdV+XliL/fWEsZ2PzZ/
QhrbdEZr4Uq6yKfW0k4baZQPEoHkOOnL5MYMsjcu66UOjyFu/C1o
-----END CERTIFICATE-----
)";

  std::vector<uint8_t> cert =
    PEM::ParsePEM(CERT, "CERTIFICATE");

  std::string exp = CSR::GetExpirationTimeString(cert);
  CHECK(exp == "260221173112Z") << exp;
  time_t expt = CSR::GetExpirationTime(cert);
  CHECK(expt == int64_t{1771695072}) << expt;
}

int main(int argc, char **argv) {
  ANSI::Init();

  Print("This test writes files you can verify with OpenSSL.\n"
        "See the source code for the commands.\n");
  TestCRI();
  TestCSR();
  TestExpiration();

  Print("OK\n");
  return 0;
}
