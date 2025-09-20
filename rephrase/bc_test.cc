
#include "bc.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "base/print.h"
#include "bignum/big.h"

namespace bc {

static void TestValueEq() {

  // Different values that might be conflated.
  std::vector<Value> values {
    {.v = BigInt(0)},
    {.v = std::string("")},
    {.v = uint64_t{0}},
    {.v = double{0.0}},
    {.v = std::unordered_map<std::string, Value *>{}},
    {.v = std::vector<Value *>{}}
  };

  for (int i = 0; i < (int)values.size(); i++) {
    for (int j = 0; j < (int)values.size(); j++) {
      if (i == j) {
        CHECK(ValueEq()(values[i], values[j]));
        CHECK(ValueHash()(values[i]) == ValueHash()(values[j]));
      } else {
        CHECK(!ValueEq()(values[i], values[j]));
        CHECK(ValueHash()(values[i]) != ValueHash()(values[j]))
          << ColorValueString(values[i]) << " vs " << ColorValueString(values[j])
          << "\nThis is technically allowed, but we have a very bad "
          "hash in this case, since these values are probably "
          "the most common of each type.";
      }
    }
  }
}

}  // namespace bc

int main(int argc, char **argv) {
  bc::TestValueEq();

  Print("OK\n");
  return 0;
}
