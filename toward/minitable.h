
#ifndef _TOWARD_MINITABLE_H
#define _TOWARD_MINITABLE_H

#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

#include "prop.h"

// Creates a table containing the smallest proposition that computes
// each function on four variables.
struct MiniTable {
  // You must supply enough options to compute the full gamut!
  static constexpr uint32_t OPT_AND = 0b1;
  static constexpr uint32_t OPT_NOT = 0b10;
  static constexpr uint32_t OPT_XOR = 0b100;
  static constexpr uint32_t OPT_OR = 0b1000;

  explicit MiniTable(uint32_t opts);

  // Given the four variables and the truth table,
  // Create the minimal proposition that has the given
  // truth table.
  Prop Minimal(int a, int b, int c, int d,
               uint16_t fn);

  // Get the truth table for the proposition, which must
  // use variables 0..3 only.
  static uint16_t Eval(const Prop &p);

  // If p has four or fewer variables, return those four variables
  // (possibly duplicating them to fill unused slots) and the
  // resulting truth table. This can be used directly to look up
  // the minimal proposition above.
  static std::optional<std::tuple<int, int, int, int, uint16_t>>
  GetQuad(const Prop &p);

 private:
  std::vector<Prop> minimal;
};

#endif
