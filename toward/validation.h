
#ifndef _TOWARD_VALIDATION_H
#define _TOWARD_VALIDATION_H

#include <memory>
#include <span>
#include <utility>
#include <vector>
#include <string_view>

#include "level.h"
#include "prop.h"
#include "scene.h"
#include "circuit.h"

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
  virtual std::string_view Name() const = 0;
  virtual Level InitialLevel() const = 0;
  virtual int ExpectedInputs() const = 0;
  virtual int ExpectedOutputs() const = 0;

  virtual ValidationSample OneSample(uint64_t seed) const = 0;
  virtual ~ValidationInstance() {}
};

struct Validation {
  static std::unique_ptr<ValidationInstance> And();
  static std::unique_ptr<ValidationInstance> Separator();
  static std::unique_ptr<ValidationInstance> Not();
  // Separates and duplicates its one input.
  static std::unique_ptr<ValidationInstance> DupSep();
  // Exchanges separated A and ¬A.
  static std::unique_ptr<ValidationInstance> SepXchg();
  // Exchanges two separated zeroes.
  static std::unique_ptr<ValidationInstance> Sep00Xchg();
  // A separated 0 and a separated 1.
  static std::unique_ptr<ValidationInstance> Sep01Xchg();
  static std::unique_ptr<ValidationInstance> Sep10Xchg();
  static std::unique_ptr<ValidationInstance> Sep11Xchg();

  // Wire shape A, offset +0.
  static std::unique_ptr<ValidationInstance> WireA0();
  // Offset -1.
  static std::unique_ptr<ValidationInstance> WireAN1();
  // Offset -2, -4, -8, etc.
  static std::unique_ptr<ValidationInstance> WireAN2();
  static std::unique_ptr<ValidationInstance> WireAN4();
  static std::unique_ptr<ValidationInstance> WireAN8();
  static std::unique_ptr<ValidationInstance> WireAN16();
  static std::unique_ptr<ValidationInstance> WireAN32();

  // TODO:
  // Create a validation instance that tests a standard cell,
  // verifying that it matches its logical behavior (Transform).
  // The args must match the input size for the cell. Most of
  // the time, these will just be distinct variables, and the
  // instance will sample from any assignment to them. Some
  // cells have a requirement on the inputs (e.g. SELFXCHG must
  // take separated A and ¬A) which we express by having
  // more constrained propositions.
  static std::unique_ptr<ValidationInstance>
  ValidateCell(const Cell &cell, std::span<const Prop> args);

  // For separated inputs, we have either the given bit, or nothing.
  // SeparatedZero gives the contents of the "zero" chute when the
  // bit value is b.
  static inline ChuteValue SeparatedZero(bool b) {
    return b ? ChuteValue::NOTHING : ChuteValue::ZERO;
  }
  static inline ChuteValue SeparatedOne(bool b) {
    return b ? ChuteValue::ONE : ChuteValue::NOTHING;
  }

  // Load the level, check that it's structurally OK, and add
  // walls if needed.
  static std::unique_ptr<Level> Load(const ValidationInstance &inst);

  // True if the actual output counts as correct for the sample.
  static bool IsValidOutput(const ValidationSample &sample,
                            std::span<const ChuteValue> actual);

  // Sample a 1 or 0 input (with random position/velocity/etc.) in a
  // standard input region.
  static LevelBody SampleInput(uint64_t seed, int input_pos, bool bit);

  // Clone the level. Sample the input
  static std::pair<ValidationSample, Level>
  LevelWithInputs(const Level &level,
                  const ValidationInstance &inst,
                  uint64_t seed);

  // The inputs must match!
  static std::vector<ChuteValue> ReadOutputs(
      const ValidationInstance &inst,
      const Level &level,
      const Scene &scene);
};

#endif
