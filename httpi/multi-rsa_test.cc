
#include "multi-rsa.h"

#include "ansi.h"
#include "base/print.h"

static void TestRoundTrip() {

  MultiRSA::Key key;
  key.n = BigInt("238402292925117127099162286134407962123");
  key.e = BigInt(65537);
  key.d = BigInt("7303389996357624649348554884502106553");

  MultiRSA::PrimeFactor p, q, r;
  p.p = BigInt("4116725966653");
  p.exp = BigInt("3104143416605");
  p.inv = BigInt(1);

  q.p = BigInt("8175276732047");
  q.exp = BigInt("4577316697525");
  q.inv = BigInt("5516585879127");

  r.p = BigInt("7083632256553");
  r.exp = BigInt("725257067633");
  r.inv = BigInt("3759599611372");

  key.factors = {std::move(p), std::move(q), std::move(r)};

  std::vector<uint8_t> pkcs1 = MultiRSA::EncodePKCS1(key);
  std::optional<MultiRSA::Key> key1 = MultiRSA::DecodePKCS1(pkcs1);
  CHECK(key1.has_value());
  CHECK(MultiRSA::KeyEq(key, key1.value()));

  std::vector<uint8_t> pkcs8 = MultiRSA::EncodePKCS8(key);
  std::optional<MultiRSA::Key> key8 = MultiRSA::DecodePKCS8(pkcs8);
  CHECK(key8.has_value());
  CHECK(MultiRSA::KeyEq(key, key8.value()));
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestRoundTrip();

  Print("OK\n");
  return 0;
};
