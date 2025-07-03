
// Problem-specific constants and things that are shared between
// training, network generation, eval, etc.

#ifndef _NETWORKTOYS_PROBLEM_H
#define _NETWORKTOYS_PROBLEM_H

#include <cstdint>

#include "network.h"

inline constexpr int NES_WIDTH = 256;
inline constexpr int NES_HEIGHT = 240;

inline constexpr int INPUT_LAYER_SIZE = NES_WIDTH * NES_HEIGHT;
inline constexpr int OUTPUT_LAYER_SIZE = NES_WIDTH * NES_HEIGHT;

// custom renderstyles for font problem
enum UserRenderStyle : uint32_t {
  RENDERSTYLE_NESUV = RENDERSTYLE_USER + 1000,
};


#endif
