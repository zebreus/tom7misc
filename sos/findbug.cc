
#include <vector>
#include <cstdint>
#include <tuple>

#include "sos-util.h"
#include "threadutil.h"
#include "sos-quad.h"

#include "factorization.h"

int main(int argc, char **argv) {
  // int64_t start = 23'358'400'000'000LL;
  int64_t start = 23361808000000LL;
  constexpr int INNER_ROLL = 2'000;
  constexpr int ROLL = INNER_ROLL * INNER_ROLL;

  for (uint64_t batch_start = start; true; batch_start += ROLL) {

    std::mutex m;
    std::vector<std::tuple<uint64_t, uint32_t, CollatedFactors>> todo;

    ParallelComp(
        ROLL / INNER_ROLL,
        [&](int64_t idx) {
          uint64_t inner_batch_start = batch_start + (idx * INNER_ROLL);
          std::vector<std::tuple<uint64_t, uint32_t, CollatedFactors>>
            local_todo;
          for (int i = 0; i < INNER_ROLL; i++) {
            const uint64_t sum = inner_batch_start + i;
            if (MaybeSumOfSquaresFancy4(sum)) {
              CollatedFactors factors;
              factors.num_factors = Factorization::FactorizePreallocated(
                  sum, factors.bases, factors.exponents);
              const int nways = ChaiWahWuFromFactors(
                  sum, factors.bases, factors.exponents, factors.num_factors);
              if (nways > 8) {
                local_todo.emplace_back(sum, nways, factors);
              }
            }
          }

          {
            MutexLock ml(&m);
            for (auto &row : local_todo) todo.emplace_back(std::move(row));
          }
        },
        12);

    printf("From %lld, %d eligible sums...\n", batch_start,
           (int)todo.size());
    for (const auto &[sum, expected, factors] : todo) {
      printf("  %lld\n", sum);
      (void)GetWaysQuad(sum, expected, factors.num_factors,
                        factors.bases, factors.exponents);
    }
  }

  return 0;
}
