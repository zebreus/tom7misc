
#ifndef _FCEULIB_OPCODES_H
#define _FCEULIB_OPCODES_H

#include <cstdint>

struct Opcodes {
  // The instruction's name, including its addressing mode.
  // Note: These are non-canonical and there are multiple
  // instructions with the same name (e.g. NOP).
  static const char *const opcode_name[256];

  // Number of bytes in the instruction, including the
  // initial opcode.
  static uint8_t opcode_size[256];
};

#endif
