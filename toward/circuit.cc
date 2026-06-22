
#include "circuit.h"

#include <utility>
#include <cstdint>
#include <span>
#include <vector>

#include "prop.h"
#include "base/logging.h"


int CellWidth() {
  // TODO: Need catalog of inputs from SVG files; derive widths.

  LOG(FATAL) << "Unimplemented";
  return 0;
}

std::pair<int, int> GateSize(Gate g) {
  switch (g) {
  case SPACER: return {0, 0};
  case AND0110: return {4, 1};
  case NOT: return {1, 1};
  case SEPARATOR: return {1, 2};
  case WIRE: return {1, 1};
  case XCHG00:
  case XCHG01:
  case XCHG10:
  case XCHG11: return {2, 2};
  case DUPSEP0011: return {1, 4};
  case SINK: return {1, 0};
  case CONST0:
  case CONST1: return {0, 1};
  default:
    LOG(FATAL) << "Unimplemented gate in GateSize?";
    return {0, 0};
  }
}

std::pair<int, int> LayerSize(const Layer &layer) {
  int inputs = 0, outputs = 0;
  for (const Cell &c : layer) {
    const auto &[i, o] = GateSize(c.gate);
    inputs += i;
    outputs += o;
  }
  return std::make_pair(inputs, outputs);
}

std::vector<Func> TransformCell(const Cell &cell,
                                std::span<const Func> in) {
  const auto &[num_in, num_out] = GateSize(cell.gate);
  CHECK(in.size() == num_in);
  std::vector<Func> out;
  out.resize(num_out);

  auto InputIdx = [&](int idx) {
      if (cell.flip) {
        return num_in - 1 - idx;
      } else {
        return idx;
      }
    };

  auto OutputIdx = [&](int idx) {
      if (cell.flip) {
        return num_out - 1 - idx;
      } else {
        return idx;
      }
    };

  switch (cell.gate) {
  case SPACER: {
    // Nothing.
    break;
  }

  case AND0110: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    const Func &fc = in[InputIdx(2)];
    const Func &fd = in[InputIdx(3)];

    // We can assume fa.prop = -fb.prop,
    // fc.prop = -fd.prop.

    CHECK(fa.type == CType::ZERO);
    CHECK(fb.type == CType::ONE);
    CHECK(fc.type == CType::ONE);
    CHECK(fd.type == CType::ZERO);

    out[OutputIdx(0)] = Func{
      .prop = fa.prop & fc.prop,
      .type = CType::MIXED,
    };

    break;
  }

  case NOT: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::MIXED);
    out[OutputIdx(0)] = Func{.prop = -f.prop, .type = CType::MIXED};
    break;
  }

  case SEPARATOR: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::MIXED);
    out[OutputIdx(0)] = Func{.prop = f.prop, .type = CType::ZERO};
    out[OutputIdx(1)] = Func{.prop = f.prop, .type = CType::ONE};
    break;
  }

  case WIRE: {
    out[OutputIdx(0)] = in[InputIdx(0)];
    break;
  }

  case XCHG00: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    CHECK(fa.type == CType::ZERO);
    CHECK(fb.type == CType::ZERO);

    out[OutputIdx(0)] = Func{.prop = fb.prop, .type = CType::ZERO};
    out[OutputIdx(1)] = Func{.prop = fa.prop, .type = CType::ZERO};
    break;
  }

  case XCHG01: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    CHECK(fa.type == CType::ZERO);
    CHECK(fb.type == CType::ONE);

    out[OutputIdx(0)] = Func{.prop = fb.prop, .type = CType::ONE};
    out[OutputIdx(1)] = Func{.prop = fa.prop, .type = CType::ZERO};
    break;
  }

  case XCHG10: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    CHECK(fa.type == CType::ONE);
    CHECK(fb.type == CType::ZERO);

    out[OutputIdx(0)] = Func{.prop = fb.prop, .type = CType::ZERO};
    out[OutputIdx(1)] = Func{.prop = fa.prop, .type = CType::ONE};
    break;
  }

  case XCHG11: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    CHECK(fa.type == CType::ONE);
    CHECK(fb.type == CType::ONE);

    out[OutputIdx(0)] = Func{.prop = fb.prop, .type = CType::ONE};
    out[OutputIdx(1)] = Func{.prop = fa.prop, .type = CType::ONE};
    break;
  }

  case DUPSEP0011: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::MIXED);
    out[OutputIdx(0)] = Func{.prop = f.prop, .type = CType::ZERO};
    out[OutputIdx(1)] = Func{.prop = f.prop, .type = CType::ZERO};
    out[OutputIdx(2)] = Func{.prop = f.prop, .type = CType::ONE};
    out[OutputIdx(3)] = Func{.prop = f.prop, .type = CType::ONE};
    break;
  }

  case SINK: {
    // Nothing.
    break;
  }

  case CONST0: {
    out[OutputIdx(0)] = {.prop = False(), .type = CType::MIXED};
    break;
  }

  case CONST1: {
    out[OutputIdx(0)] = {.prop = True(), .type = CType::MIXED};
    break;
  }

  default:
    LOG(FATAL) << "Unimplemented gate in TransformCell?";
  }

  CHECK(out.size() == num_out);
  return out;
}

std::vector<Func> Transform(const Layer &layer,
                            const std::vector<Func> &in) {
  const auto &[num_inputs, num_outputs] = LayerSize(layer);
  CHECK(num_inputs == in.size());
  std::vector<Func> out;
  out.reserve(num_outputs);

  int in_idx = 0;
  int out_idx = 0;
  for (const Cell &cell : layer) {
    const auto &[cell_num_in, cell_num_out] = GateSize(cell.gate);

    std::span<const Func> cell_in(in.data() + in_idx, cell_num_in);
    std::vector<Func> cell_out = TransformCell(cell, cell_in);
    out.insert(out.end(), cell_out.begin(), cell_out.end());

    in_idx += cell_num_in;
    out_idx += cell_num_out;
    CHECK(out_idx == out.size());
  }

  CHECK(out.size() == num_outputs);
  return out;
}
