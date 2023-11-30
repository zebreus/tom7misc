
#ifndef _SOS_SOS_QUAD_H
#define _SOS_SOS_QUAD_H

#include <cstdint>
#include <vector>
#include <utility>

// Using Quadratic equation solver.
std::vector<std::pair<uint64_t, uint64_t>>
GetWaysQuad(uint64_t sum, int num_expected,
            int num_factors,
            const uint64_t *bases,
            const uint8_t *exponents);

#endif
