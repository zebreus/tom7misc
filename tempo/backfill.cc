
#include <cstdio>
#include <cstdint>

using int64 = int64_t;

// before thinning: 
// mysql> select count(*) from tempo.reading;
// +-----------+
// | count(*)  |
// +-----------+
// | 248795074 |
// +-----------+
// 1 row in set (4 min 33.89 sec)

// constexpr int64 START = 1583000000;
// constexpr int64 END = 1592058229;
// constexpr int64 STRIDE = 500000;

static void FillSample() {
  constexpr int64 START = 1591990000;
  constexpr int64 END = 1592000000;
  constexpr int64 STRIDE = 1000;

  for (int64 lo = START; lo < END; lo += STRIDE) {
    printf("update tempo.reading set sample_key = "
           "((id * 31337) ^ (probeid * 82129) + (timestamp * 257)) "
           "mod 65521 where timestamp between %lld and %lld;\n",
           lo, lo + STRIDE);
  }
}

static void Thin() {
  constexpr int64 START = 1591990000;
  constexpr int64 END = 1650232120;
  constexpr int64 STRIDE = 100000;

  for (int64 lo = START; lo < END; lo += STRIDE) {
    printf("delete from tempo.reading where sample_key > 8192 "
           " and timestamp between %lld and %lld;\n",
           lo, lo + STRIDE);
  }
 
}

int main(int argc, char **argv) {
  // FillSample
  Thin();
  
  return 0;
}
