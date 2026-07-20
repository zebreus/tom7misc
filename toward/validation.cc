
#include "validation.h"

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "box2d.h"
#include "cell-library.h"
#include "circuit.h"
#include "level.h"
#include "pcg.h"
#include "prop.h"
#include "scene.h"

namespace {
struct GenericCellValidation : public ValidationInstance {
  std::string name;
  std::unique_ptr<Level> level;
  std::vector<Func> in_funcs;
  std::vector<Func> out_funcs;
  int max_var = -1;

  GenericCellValidation(const CellLibrary &library,
                        const Cell &cell, std::span<const Prop> args) {
    CellLibrary::Info info = library.GetInfo(cell);
    level = library.GetLevel(cell);
    CHECK(level.get() != nullptr);
    CHECK(args.size() == info.inputs.size());

    for (size_t i = 0; i < args.size(); i++) {
      in_funcs.push_back(Func{
          .prop = args[i],
          .type = info.inputs[i].type,
        });
      for (int v : PropVars(args[i])) {
        max_var = std::max(max_var, v);
      }
    }

    out_funcs = TransformCell(cell, in_funcs);
    CHECK(out_funcs.size() == info.outputs.size());

    name = CellString(cell);
  }

  std::string_view Name() const override { return name; }
  Level InitialLevel() const override { return *level; }
  int ExpectedInputs() const override { return (int)in_funcs.size(); }
  int ExpectedOutputs() const override { return (int)out_funcs.size(); }

  ValidationSample OneSample(uint64_t seed) const override {
    PCG32 pcg(seed);
    uint64_t bits = pcg.Rand64();

    CHECK(max_var < 60) << "This is designed for a small number of "
      "variables!";
    std::vector<bool> assignments(max_var + 1, false);
    for (int i = 0; i <= max_var; i++) {
      assignments[i] = !!((bits >> i) & 1);
    }

    ValidationSample ret;
    for (size_t i = 0; i < in_funcs.size(); i++) {
      bool val = EvaluateProp(assignments, in_funcs[i].prop);
      if (in_funcs[i].type == CType::MIXED) {
        ret.input_values.push_back(val ? ChuteValue::ONE : ChuteValue::ZERO);
      } else if (in_funcs[i].type == CType::ZERO) {
        ret.input_values.push_back(Validation::SeparatedZero(val));
      } else if (in_funcs[i].type == CType::ONE) {
        ret.input_values.push_back(Validation::SeparatedOne(val));
      }
    }

    std::vector<ChuteValue> expected;
    for (size_t i = 0; i < out_funcs.size(); i++) {
      bool val = EvaluateProp(assignments, out_funcs[i].prop);
      if (out_funcs[i].type == CType::MIXED) {
        expected.push_back(val ? ChuteValue::ONE : ChuteValue::ZERO);
      } else if (out_funcs[i].type == CType::ZERO) {
        expected.push_back(Validation::SeparatedZero(val));
      } else if (out_funcs[i].type == CType::ONE) {
        expected.push_back(Validation::SeparatedOne(val));
      }
    }
    ret.valid_outputs.push_back(std::move(expected));

    return ret;
  }
};

}  // namespace

std::unique_ptr<ValidationInstance>
Validation::ValidateCell(const CellLibrary &library,
                         const Cell &cell, std::span<const Prop> args) {
  return std::make_unique<GenericCellValidation>(library, cell, args);
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
  std::unique_ptr<Level> level = std::make_unique<Level>(inst.InitialLevel());

  CHECK(level->inputs.size() == (size_t)inst.ExpectedInputs() &&
        level->outputs.size() == (size_t)inst.ExpectedOutputs());

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

  // The addition of the side walls for inputs and outputs above is
  // standard, but when validating we additionally need to stop
  // objects from leaving the bottom of the output cup. We add an
  // artificial bottom piece.
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
