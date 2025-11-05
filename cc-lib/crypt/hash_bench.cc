
#include "arcfour.h"
#include "crypt/md5.h"
#include "crypt/sha256.h"
#include "crypt/sha1.h"

#include <cinttypes>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>
#include <string_view>

#include "base/print.h"

using uint8 = uint8_t;

template<class F>
static void Bench(std::string_view name, int input_size, F f) {
  ArcFour rc("bench");

  constexpr int ITERS = 1000000;

  uint64_t sum = 0;
  const auto start = std::chrono::steady_clock::now();
  for (int iter = 0; iter < ITERS; iter++) {
    std::vector<uint8> input;
    input.reserve(input_size);
    for (int i = 0; i < input_size; i++) input.push_back(rc.Byte());

    std::vector<uint8_t> ret = f(input);
    for (uint8_t b : ret) sum += b;
  }
  const auto end = std::chrono::steady_clock::now();
  const std::chrono::duration<double> diff = end - start;
  Print("[{}] [{}] Iters per second: {:.1f}\n",
     name, sum, (double)ITERS / (double)diff.count());
  fflush(stdout);
}

// Basically the minimal overhead would look something like this.
static std::vector<uint8_t> Prefix16(const std::vector<uint8_t> &input) {
  std::vector<uint8_t> pfx;
  pfx.reserve(16);
  for (size_t i = 0; i < 16 && i < input.size(); i++) {
    pfx.push_back(input[i]);
  }
  return pfx;
}

static std::vector<uint8_t> Md5(const std::vector<uint8_t> &input) {
  std::string s = MD5::Hashv(input);
  std::vector<uint8_t> ret;
  ret.resize(16);
  for (int i = 0; i < 16; i++) ret[i] = s[i];
  return ret;
}

static std::vector<uint8_t> Sha256(const std::vector<uint8_t> &input) {
  return SHA256::HashVector(input);
}

static std::vector<uint8_t> Sha1(const std::vector<uint8_t> &input) {
  SHA1::Ctx ctx;
  SHA1::Init(&ctx);
  SHA1::Update(&ctx, input.data(), input.size());
  std::vector<uint8_t> hash(SHA1::DIGEST_LENGTH);
  SHA1::Finalize(&ctx, hash.data());
  return hash;
}

int main(int argc, char **argv) {
  Bench("prefix", 256, Prefix16);
  Bench("md5", 256, Md5);
  Bench("sha256", 256, Sha256);
  Bench("sha1", 256, Sha1);

  return 0;
}
