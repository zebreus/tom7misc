
#include "multi-rsa.h"

#include <optional>
#include <utility>
#include <vector>
#include <cstdint>

#include "ansi.h"
#include "base/print.h"
#include "base/logging.h"
#include "bignum/big.h"
#include "bignum/big-overloads.h"
#include "hexdump.h"
#include "timer.h"

#define CHECK_VEQ(a, b) do {                                \
    auto aa = std::vector<uint8_t>(a);                      \
    auto bb = std::vector<uint8_t>(b);                      \
    CHECK(aa == bb) << "Expected equal:\n" << #a <<         \
      "\nwhich is\n" << HexDump::Color(aa) <<               \
      "\nand\n" << #b <<                                    \
      "\nwhich is\n" << HexDump::Color(bb) << "\n";         \
  } while (0)


static void TestSerializationRoundTrip() {
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

void TestDecryptCRT() {
  std::optional<MultiRSA::Key> okey =
    MultiRSA::KeyFromPrimes(std::vector<BigInt>{
      BigInt("1452183499217746936988050278709957755003934074480911"),
      BigInt("2696423543995307208401026789711225014797731381216367"),
      BigInt("2465074989005639593525836528988016925392176901709149")});
  CHECK(okey.has_value());
  const MultiRSA::Key &key = okey.value();

  std::vector<uint8_t> block(MultiRSA::BlockSize(key));
  for (int i = 0; i < block.size(); i++) {
    block[i] = i;
  }
  CHECK(BigInt::FromBigEndianBytes(block) < key.n) << "The first whole byte "
    "of the block is zero, so this should be smaller than n.";

  std::vector<uint8_t> block2 = block;

  MultiRSA::RawDecryptInPlace(key, block);
  MultiRSA::RawDecryptInPlaceCRT(key, block2);

  CHECK_VEQ(block, block2);
}

void TestEncryptDecrypt() {
  std::optional<MultiRSA::Key> okey =
    MultiRSA::KeyFromPrimes(std::vector<BigInt>{
      BigInt("1452183499217746936988050278709957755003934074480911"),
      BigInt("2696423543995307208401026789711225014797731381216367"),
      BigInt("2465074989005639593525836528988016925392176901709149")});
  CHECK(okey.has_value());
  const MultiRSA::Key &key = okey.value();

  std::vector<uint8_t> block(MultiRSA::BlockSize(key));
  for (int i = 0; i < block.size(); i++) {
    block[i] = i;
  }
  CHECK(BigInt::FromBigEndianBytes(block) < key.n) << "The first whole byte "
    "of the block is zero, so this should be smaller than n.";

  std::vector<uint8_t> original = block;

  MultiRSA::RawEncryptInPlace(key.n, key.e, block);
  CHECK(block != original);
  MultiRSA::RawDecryptInPlace(key, block);
  CHECK(block == original);
}

void BenchDecrypt() {
  Print("Benchmarking decryption:\n");
  std::vector<BigInt> primes = {
  BigInt("220579997080201528679866389806010526018328171628748766160655660624723379952863"),
  BigInt("109925413483831602130182558106571963601983168935027100919610231432894866318593"),
  BigInt("105764244831800584366724882221227090875239012149228762943276725732291963037231"),
  BigInt("108421224043368668936672329236966942599746285337730584068331659584461609397127"),
  BigInt("106811015729839965900255639060621431503323480093428287608240449406834314769787"),
  BigInt("111958605507637259539652364018671121400555494420239282827410612888183902437289"),
  BigInt("111143530731000225172829218289379033511522931754509903417941101791766477527489"),
  BigInt("98108886666290271043620148700873881874702736772631693860464300807262807270181"),
  BigInt("106566327889341021190816958103532436080037456414016884599397503046281237823449"),
  BigInt("114295451891117855604903394532382400617222186837415799285779633756915057757983"),
  BigInt("95508574307584229062673554752471896220538652278792545264727647458688569715287"),
  BigInt("113570123542589104007016447691992534673838348848799751612728553367905426302197"),
  BigInt("97925785184069643431919720658906180649276544977837373164852667100005303734263"),
  BigInt("113682567771407355370301830569735975748688021997815000985784536143889990020339"),
  BigInt("108886500838026429825905236974757020084397749485796461776037676573843273676729"),
  BigInt("113585662962116035544088218437023333762581812810738982849705368122701001474173"),
  };

  std::optional<MultiRSA::Key> okey = MultiRSA::KeyFromPrimes(primes);
  CHECK(okey.has_value());
  const MultiRSA::Key &key = okey.value();

  std::vector<uint8_t> block(MultiRSA::BlockSize(key));
  for (int i = 0; i < block.size(); i++) {
    block[i] = i;
  }
  CHECK(BigInt::FromBigEndianBytes(block) < key.n) << "The first whole byte "
    "of the block is zero, so this should be smaller than n.";

  constexpr int ROUNDS = 1024;

  {
    std::vector<uint8_t> test_block = block;
    Timer timer;
    for (int i = 0; i < ROUNDS; i++) {
      MultiRSA::RawDecryptInPlace(key, test_block);
    }
    double sec = timer.Seconds();
    Print("[{} in {}] Standard: {} ea.\n", ROUNDS,
          ANSI::Time(sec), ANSI::Time(sec / ROUNDS));
  }

  {
    std::vector<uint8_t> test_block = block;
    Timer timer;
    for (int i = 0; i < ROUNDS; i++) {
      MultiRSA::RawDecryptInPlaceCRT(key, test_block);
    }
    double sec = timer.Seconds();
    Print("[{} in {}] CRT: {} ea.\n", ROUNDS,
          ANSI::Time(sec), ANSI::Time(sec / ROUNDS));
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestSerializationRoundTrip();
  TestEncryptDecrypt();
  TestDecryptCRT();

  BenchDecrypt();

  Print("OK\n");
  return 0;
};
