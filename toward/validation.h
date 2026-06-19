
#ifndef _TOWARD_VALIDATION_H
#define _TOWARD_VALIDATION_H

#include <memory>
#include <vector>
#include <string_view>

#include "level.h"
#include "scene.h"

// A validation instance is a spec with an implementation.
// The spec consists of:
//   - A way of sampling inputs
//   - The expected output

// A chute can have nothing, a zero or one bit, or garbage.
enum class ChuteValue {
  NOTHING,
  ZERO,
  ONE,
  GARBAGE,
};

struct ValidationSample {
  // Must match expected_inputs.
  std::vector<ChuteValue> input_values;

  // The expected output. There is typically one, but it is allowed
  // for there to be more than one valid output, e.g. we might
  // allow one chute to contain garbage conditional on another's
  // value.
  std::vector<std::vector<ChuteValue>> valid_outputs;
};

struct ValidationInstance {
  virtual std::string_view Filename() const = 0;
  virtual int ExpectedInputs() const = 0;
  virtual int ExpectedOutputs() const = 0;

  virtual bool AddInputWalls() const { return false; }

  virtual ValidationSample OneSample(uint64_t seed) const = 0;
  virtual ~ValidationInstance() {}
};

struct Validation {
  static std::unique_ptr<ValidationInstance> And();
  static std::unique_ptr<ValidationInstance> Separator();
  static std::unique_ptr<ValidationInstance> Not();


  // For separated inputs, we have either the given bit, or nothing.
  // SeparatedZero gives the contents of the "zero" chute when the
  // bit value is b.
  static inline ChuteValue SeparatedZero(bool b) {
    return b ? ChuteValue::NOTHING : ChuteValue::ZERO;
  }
  static inline ChuteValue SeparatedOne(bool b) {
    return b ? ChuteValue::ONE : ChuteValue::NOTHING;
  }

  // Sample a 1 or 0 input (with random position/velocity/etc.) in a
  // standard input region.
  static LevelBody SampleInput(uint64_t seed, vec2f input_pos, bool bit);
};

#endif
