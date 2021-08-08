
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
// XXX deletey
enum UserRenderStyle : uint32_t {
  RENDERSTYLE_INPUTXY = RENDERSTYLE_USER + 1000,
  RENDERSTYLE_OUTPUTXY = RENDERSTYLE_USER + 1001,

  RENDERSTYLE_SDF = RENDERSTYLE_USER + 2000,
  RENDERSTYLE_SDF26 = RENDERSTYLE_USER + 2001,
};


#endif
