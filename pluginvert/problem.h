
// Problem-specific constants and things that are shared between
// training, network generation, eval, etc.

#ifndef _PLUGINVERT_PROBLEM_H
#define _PLUGINVERT_PROBLEM_H

#include <cstdint>

#include "network.h"

static constexpr int NES_WIDTH = 256;
static constexpr int NES_HEIGHT = 240;

static constexpr int INPUT_LAYER_SIZE = NES_WIDTH * NES_HEIGHT;
static constexpr int OUTPUT_LAYER_SIZE = NES_WIDTH * NES_HEIGHT;

// custom renderstyles for font problem
enum UserRenderStyle : uint32_t {
  RENDERSTYLE_NESUV = RENDERSTYLE_USER + 1000,

};


#endif
