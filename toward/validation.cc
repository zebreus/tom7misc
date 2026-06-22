
#include "validation.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "box2d.h"
#include "level.h"
#include "pcg.h"
#include "scene.h"
#include "validation.h"

namespace {
struct AndValidation : public ValidationInstance {
  std::string_view Filename() const override {
    return "standard-and0110.svg";
  }
  int ExpectedInputs() const override { return 4; }
  int ExpectedOutputs() const override { return 1; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b01);
    bool b = !!(seed & 0b10);

    ValidationSample ret;
    // AND has its inputs already separated: 0110.
    ret.input_values.push_back(Validation::SeparatedZero(a));
    ret.input_values.push_back(Validation::SeparatedOne(a));
    ret.input_values.push_back(Validation::SeparatedOne(b));
    ret.input_values.push_back(Validation::SeparatedZero(b));

    // Want a single clean bit out.
    ChuteValue expected = (a && b) ? ChuteValue::ONE : ChuteValue::ZERO;
    ret.valid_outputs = {{expected}};
    return ret;
  }
};

struct SeparatorValidation : public ValidationInstance {
  std::string_view Filename() const override {
    return "standard-separator.svg";
  }
  int ExpectedInputs() const override { return 1; }
  int ExpectedOutputs() const override { return 2; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b01);

    ValidationSample ret;
    ret.input_values.push_back(a ? ChuteValue::ONE : ChuteValue::ZERO);

    // Separated bits out.
    ret.valid_outputs = {
      {Validation::SeparatedZero(a), Validation::SeparatedOne(a)},
    };
    return ret;
  }
};

// I think this is still not 100% because the 1 can bonk the
// 1 on the way down.
struct NotValidation : public ValidationInstance {
  std::string_view Filename() const override {
    return "standard-not.svg";
  }
  int ExpectedInputs() const override { return 1; }
  int ExpectedOutputs() const override { return 1; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b01);

    // Mixed bits in/out.
    ValidationSample ret;
    ret.input_values.push_back(a ? ChuteValue::ONE : ChuteValue::ZERO);
    ret.valid_outputs = {{a ? ChuteValue::ZERO : ChuteValue::ONE}};
    return ret;
  }
};

struct DupSepValidation : public ValidationInstance {
  std::string_view Filename() const override {
    return "standard-dupsep.svg";
  }
  int ExpectedInputs() const override { return 1; }
  int ExpectedOutputs() const override { return 4; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b01);

    ValidationSample ret;
    ret.input_values.push_back(a ? ChuteValue::ONE : ChuteValue::ZERO);

    // Separated and duplicated bits: 0011
    ret.valid_outputs = {
      {Validation::SeparatedZero(a),
       Validation::SeparatedZero(a),
       Validation::SeparatedOne(a),
       Validation::SeparatedOne(a)},
    };
    return ret;
  }
};

struct [[maybe_unused]] XchgValidation : public ValidationInstance {
  std::string_view Filename() const override { return "xchg1.svg"; }
  int ExpectedInputs() const override { return 2; }
  int ExpectedOutputs() const override { return 2; }

  bool AddInputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b01);
    bool b = !!(seed & 0b10);

    ChuteValue av = a ? ChuteValue::ONE : ChuteValue::ZERO;
    ChuteValue bv = b ? ChuteValue::ONE : ChuteValue::ZERO;

    ValidationSample ret;
    ret.input_values.push_back(av);
    ret.input_values.push_back(bv);

    // Swapped.
    ret.valid_outputs = {{bv, av}};
    return ret;
  }
};

struct SepXchgValidation : public ValidationInstance {
  std::string_view Filename() const override {
    return "standard-sepxchg.svg";
  }
  int ExpectedInputs() const override { return 2; }
  int ExpectedOutputs() const override { return 2; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool chirality = !!(seed & 0b01);
    bool a = !!(seed & 0b10);

    // Two cases here. The left input can be a separated one or zero;
    // the right input must be its separated negation.

    ValidationSample ret;
    if (chirality) {
      ChuteValue l = Validation::SeparatedZero(a);
      ChuteValue r = Validation::SeparatedOne(a);
      ret.input_values.push_back(l);
      ret.input_values.push_back(r);
      ret.valid_outputs = {{r, l}};

    } else {
      ChuteValue l = Validation::SeparatedZero(a);
      ChuteValue r = Validation::SeparatedOne(a);
      ret.input_values.push_back(l);
      ret.input_values.push_back(r);
      ret.valid_outputs = {{r, l}};
    }

    return ret;
  }
};

struct Sep00XchgValidation : public ValidationInstance {
  std::string_view Filename() const override {
    return "standard-sep00xchg.svg";
  }
  int ExpectedInputs() const override { return 2; }
  int ExpectedOutputs() const override { return 2; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b10);
    bool b = !!(seed & 0b01);

    ChuteValue sep_a = Validation::SeparatedZero(a);
    ChuteValue sep_b = Validation::SeparatedZero(b);

    ValidationSample ret;
    ret.input_values.push_back(sep_a);
    ret.input_values.push_back(sep_b);

    ret.valid_outputs = {{sep_b, sep_a}};
    return ret;
  }
};

struct Sep01XchgValidation : public ValidationInstance {
  std::string_view Filename() const override {
    return "standard-sep01xchg.svg";
  }
  int ExpectedInputs() const override { return 2; }
  int ExpectedOutputs() const override { return 2; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b10);
    bool b = !!(seed & 0b01);

    ChuteValue sep_a = Validation::SeparatedZero(a);
    ChuteValue sep_b = Validation::SeparatedOne(b);

    ValidationSample ret;
    ret.input_values.push_back(sep_a);
    ret.input_values.push_back(sep_b);

    ret.valid_outputs = {{sep_b, sep_a}};
    return ret;
  }
};

// We should also be able to do something like this by
// flipping along the x-axis.
struct Sep10XchgValidation : public ValidationInstance {
  std::string_view Filename() const override {
    return "standard-sep01xchg.svg";
  }
  int ExpectedInputs() const override { return 2; }
  int ExpectedOutputs() const override { return 2; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b10);
    bool b = !!(seed & 0b01);

    ChuteValue sep_a = Validation::SeparatedOne(a);
    ChuteValue sep_b = Validation::SeparatedZero(b);

    ValidationSample ret;
    ret.input_values.push_back(sep_a);
    ret.input_values.push_back(sep_b);

    ret.valid_outputs = {{sep_b, sep_a}};
    return ret;
  }
};

struct Sep11XchgValidation : public ValidationInstance {
  std::string_view Filename() const override {
    return "standard-sep11xchg.svg";
  }
  int ExpectedInputs() const override { return 2; }
  int ExpectedOutputs() const override { return 2; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {
    bool a = !!(seed & 0b10);
    bool b = !!(seed & 0b01);

    ChuteValue sep_a = Validation::SeparatedOne(a);
    ChuteValue sep_b = Validation::SeparatedOne(b);

    ValidationSample ret;
    ret.input_values.push_back(sep_a);
    ret.input_values.push_back(sep_b);

    ret.valid_outputs = {{sep_b, sep_a}};
    return ret;
  }
};

// All wires have the same behavior, just different geometry.
struct WireValidation : public ValidationInstance {
  WireValidation(std::string_view file) : filename(file) {}
  std::string_view Filename() const override {
    return filename;
  }
  int ExpectedInputs() const override { return 1; }
  int ExpectedOutputs() const override { return 1; }

  bool AddInputWalls() const override { return true; }
  bool AddOutputWalls() const override { return true; }

  ValidationSample OneSample(uint64_t seed) const override {

    // This should work for 0, 1, and nothing.
    // Since nothing is generally trivial (no dynamic bodies)
    // we do this rarely.
    ChuteValue v = ChuteValue::NOTHING;
    if (((seed >> 32) & 127) != 0) {
      bool a = !!(seed & 0b01);
      v = a ? ChuteValue::ONE : ChuteValue::ZERO;
    }

    ValidationSample ret;
    ret.input_values.push_back(v);

    ret.valid_outputs = {{v}};
    return ret;
  }

 private:
  std::string filename;
};


}  // namespace

std::unique_ptr<ValidationInstance> Validation::And() {
  return std::make_unique<AndValidation>();
}

std::unique_ptr<ValidationInstance> Validation::Separator() {
  return std::make_unique<SeparatorValidation>();
}

std::unique_ptr<ValidationInstance> Validation::Not() {
  return std::make_unique<NotValidation>();
}

std::unique_ptr<ValidationInstance> Validation::DupSep() {
  return std::make_unique<DupSepValidation>();
}

std::unique_ptr<ValidationInstance> Validation::SepXchg() {
  return std::make_unique<SepXchgValidation>();
}

std::unique_ptr<ValidationInstance> Validation::Sep00Xchg() {
  return std::make_unique<Sep00XchgValidation>();
}

std::unique_ptr<ValidationInstance> Validation::Sep01Xchg() {
  return std::make_unique<Sep01XchgValidation>();
}

std::unique_ptr<ValidationInstance> Validation::Sep10Xchg() {
  return std::make_unique<Sep01XchgValidation>();
}

std::unique_ptr<ValidationInstance> Validation::Sep11Xchg() {
  return std::make_unique<Sep11XchgValidation>();
}

std::unique_ptr<ValidationInstance> Validation::WireA0() {
  return std::make_unique<WireValidation>("wire-a0.svg");
}

std::unique_ptr<ValidationInstance> Validation::WireAN1() {
  return std::make_unique<WireValidation>("wire-an1.svg");
}

std::unique_ptr<ValidationInstance> Validation::WireAN2() {
  return std::make_unique<WireValidation>("wire-an2.svg");
}

std::unique_ptr<ValidationInstance> Validation::WireAN4() {
  return std::make_unique<WireValidation>("wire-an4.svg");
}

std::unique_ptr<ValidationInstance> Validation::WireAN8() {
  return std::make_unique<WireValidation>("wire-an8.svg");
}

std::unique_ptr<ValidationInstance> Validation::WireAN16() {
  return std::make_unique<WireValidation>("wire-an16.svg");
}

std::unique_ptr<ValidationInstance> Validation::WireAN32() {
  return std::make_unique<WireValidation>("wire-an32.svg");
}


bool Validation::IsValidOutput(const ValidationSample &sample,
                               std::span<const ChuteValue> actual) {
  for (const auto &valid : sample.valid_outputs) {
    CHECK(valid.size() == actual.size());
    for (size_t out_idx = 0; out_idx < valid.size(); out_idx++) {
      if (valid[out_idx] != ChuteValue::GARBAGE &&
          actual[out_idx] != valid[out_idx]) {
        goto next;
      }
    }

    return true;

  next:;
  }

  return false;
}

// TODO: This should also sample the one bit at different angles,
// but we need to account for the fact that this changes the valid
// x positions.
LevelBody Validation::SampleInput(uint64_t seed, int input_left, bool bit) {
  PCG32 pcg(seed);
  LevelBody body = bit ? Levels::One() : Levels::Zero();
  body.color = bit ? 0x00FF00FF : 0xFF0000FF;
  body.dynamic = true;
  body.vel = vec2f(pcg.Double() * 2.0f - 1.0f, pcg.Double() * 2.0f - 1.0f);
  body.avel = pcg.Double() * 2.0f - 1.0f;

  float bitw = bit ? Levels::BLOCK_SIZE : 4.0f * Levels::BLOCK_SIZE;
  constexpr float bith = 4.0f * Levels::BLOCK_SIZE;

  constexpr float in_width = Levels::IN_WIDTH * Levels::BLOCK_SIZE;
  constexpr float in_height = Levels::IN_HEIGHT * Levels::BLOCK_SIZE;

  float sample_width = (in_width - bitw) * 0.98f;
  float sample_height = (in_height - bith) * 0.98f;
  vec2f offset = {
    .x = (float)(0.02f + bitw * 0.5f + pcg.Double() * sample_width),
    .y = (float)(0.02f + bith * 0.5f + pcg.Double() * sample_height),
  };

  vec2f in_topleft = {
    .x = input_left * Levels::BLOCK_SIZE,
    .y = Levels::IN_Y * Levels::BLOCK_SIZE,
  };

  body.pos = in_topleft + offset;

  return body;
}

std::unique_ptr<Level> Validation::Load(const ValidationInstance &inst) {
  std::unique_ptr<Level> level = Levels::LoadSVG(inst.Filename());
  CHECK(level.get() != nullptr) << inst.Filename();

  CHECK(level->inputs.size() == (size_t)inst.ExpectedInputs() &&
        level->outputs.size() == (size_t)inst.ExpectedOutputs());

  if (inst.AddInputWalls()) {
    // Add vertical walls on the sides of the inputs.
    for (int in_left : level->inputs) {
      int blockheight = Levels::IN_HEIGHT;
      for (int s : { -1, Levels::IN_WIDTH }) {
        vec2f pos = {
          .x = (in_left + s + 0.5f) * Levels::BLOCK_SIZE,
          .y = (Levels::IN_Y + Levels::IN_HEIGHT / 2.0f) * Levels::BLOCK_SIZE,
        };
        LevelBody wall = Levels::WallRect(pos, 1, blockheight);
        wall.color = 0x888888FF;
        level->bodies.push_back(std::move(wall));
      }
    }
  }

  if (inst.AddOutputWalls()) {
    // Add vertical walls on the sides of the outputs.
    for (int out_left : level->outputs) {
      int blockheight = Levels::OUT_HEIGHT;
      for (int s : { -1, Levels::OUT_WIDTH }) {
        vec2f pos = {
          .x = (out_left + s + 0.5f) * Levels::BLOCK_SIZE,
          .y = (Levels::OUT_Y + Levels::OUT_HEIGHT / 2.0f) * Levels::BLOCK_SIZE,
        };
        LevelBody wall = Levels::WallRect(pos, 1, blockheight);
        wall.color = 0x888888FF;
        level->bodies.push_back(std::move(wall));
      }
    }
  }

  // We need to stop objects from leaving the bottom of the output cup.
  // The levels have rails modeled on the left and right sides, but
  // we add an artificial bottom piece during validation.
  for (int out_left : level->outputs) {
    int blockwidth = Levels::OUT_WIDTH + 2;
    vec2f pos = {
      .x = (out_left + Levels::OUT_WIDTH / 2.0f) * Levels::BLOCK_SIZE,
      .y = (Levels::OUT_Y + Levels::OUT_HEIGHT + 0.5f) * Levels::BLOCK_SIZE,
    };
    LevelBody cup_bottom = Levels::WallRect(pos, blockwidth, 1);
    cup_bottom.color = 0x888888FF;
    level->bodies.push_back(std::move(cup_bottom));
  }

  return level;
}

std::pair<ValidationSample, Level> Validation::LevelWithInputs(
    const Level &level_in,
    const ValidationInstance &inst,
    uint64_t seed) {
  PCG32 pcg(seed);
  ValidationSample sample = inst.OneSample(pcg.Rand64());
  Level level = level_in;

  for (int i = 0; i < inst.ExpectedInputs(); i++) {
    ChuteValue v = sample.input_values[i];
    if (v == ChuteValue::NOTHING || v == ChuteValue::GARBAGE) {
      continue;
    }

    bool bit = (v == ChuteValue::ONE);

    LevelBody body = Validation::SampleInput(
        pcg.Rand64(), level.inputs[i], bit);

    level.bodies.emplace_back(std::move(body));
  }

  return std::make_pair(std::move(sample), std::move(level));
}


std::vector<ChuteValue> Validation::ReadOutputs(
    const ValidationInstance &inst,
    const Level &level,
    const Scene &scene) {

  std::vector<ChuteValue> actual_outputs(inst.ExpectedOutputs(),
                                         ChuteValue::NOTHING);

  for (const Scene::Obj &obj : scene.objects) {
    if (!obj.user_data.has_value())
      continue;
    const LevelBody &lbody = level.bodies[obj.user_data.value()];
    if (!lbody.dynamic)
      continue;

    ChuteValue type = ChuteValue::GARBAGE;
    if (lbody.item.has_value()) {
      type = (lbody.item.value() == LevelItem::ONE) ? ChuteValue::ONE
                                                    : ChuteValue::ZERO;
    }

    b2Vec2 pos = b2Body_GetPosition(obj.body_id);

    constexpr float out_width = Levels::OUT_WIDTH * Levels::BLOCK_SIZE;
    constexpr float out_height = Levels::OUT_HEIGHT * Levels::BLOCK_SIZE;
    for (int out_idx = 0; out_idx < inst.ExpectedOutputs(); out_idx++) {
      float out_left = level.outputs[out_idx] * Levels::BLOCK_SIZE;
      float out_top = Levels::OUT_Y * Levels::BLOCK_SIZE;

      if (pos.x >= out_left && pos.x <= out_left + out_width &&
          pos.y >= out_top && pos.y <= out_top + out_height) {
        if (actual_outputs[out_idx] == ChuteValue::NOTHING) {
          actual_outputs[out_idx] = type;
        } else {
          actual_outputs[out_idx] = ChuteValue::GARBAGE;
        }
      }
    }
  }

  return actual_outputs;
}
