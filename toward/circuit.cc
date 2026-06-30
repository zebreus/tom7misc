
#include "circuit.h"

#include <string>
#include <string_view>
#include <utility>
#include <cstdint>
#include <span>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "prop.h"

Cell::Cell(Gate g, int v, bool flip) :
  gate(g), v(v), flip(flip) {
  CHECK(v >= 0) << "Currently, cell values are always non-negative, "
    "even for flipped wires.";
}


std::string_view TypeString(CType t) {
  switch (t) {
  case CType::ONE: return "ONE";
  case CType::ZERO: return "ZERO";
  case CType::MIXED: return "MIXED";
  default: return "?? BAD CTYPE ??";
  }
}

std::string_view GateString(Gate g) {
  switch (g) {
  case SPACER: return "SPACER";
  case AND0110: return "AND0110";
  case OR1100: return "OR1100";
  case NOT: return "NOT";
  case NOT01: return "NOT01";
  case NOT0: return "NOT0";
  case NOT1: return "NOT1";
  case SEPARATOR01: return "SEPARATOR01";
  case SEPARATOR10: return "SEPARATOR10";
  case SELFXCHG01: return "SELFXCHG01";
  case SELFXCHG10: return "SELFXCHG10";
  case WIREA: return "WIREA";
  case WIREB: return "WIREB";
  case WIRE0A: return "WIRE0A";
  case WIRE0B: return "WIRE0B";
  case WIRE1A: return "WIRE1A";
  case WIRE1B: return "WIRE1B";
  case COMBINE01: return "COMBINE01";
  case COMBINE10: return "COMBINE10";
  case XCHG00: return "XCHG00";
  case XCHG01: return "XCHG01";
  case XCHG10: return "XCHG10";
  case XCHG11: return "XCHG11";
  case DUPSEP0011: return "DUPSEP0011";
  case DUP0: return "DUP0";
  case DUP1: return "DUP1";
  case SINK: return "SINK";
  case CONST0: return "CONST0";
  case CONST1: return "CONST1";
  default: return "???BAD GATE???";
  }
}

bool IsWire(Gate gate) {
  switch (gate) {
  case WIREA:
  case WIREB:
  case WIRE0A:
  case WIRE0B:
  case WIRE1A:
  case WIRE1B:
    return true;
  default:
    return false;
  }
}

std::string CellString(const Cell &cell) {
  std::string ret(GateString(cell.gate));
  if (cell.gate == SPACER || IsWire(cell.gate)) {
    AppendFormat(&ret, "({})", cell.v);
  }

  if (cell.flip) {
    ret += ".FLIP";
  }

  return ret;
}

std::pair<int, int> GateArity(Gate g) {
  switch (g) {
  case SPACER: return {0, 0};
  case AND0110: return {4, 1};
  case OR1100: return {4, 1};
  case NOT: return {1, 1};
  case NOT0:
  case NOT1: return {1, 1};
  case NOT01: return {2, 1};
  case SEPARATOR01: return {1, 2};
  case SEPARATOR10: return {1, 2};
  case SELFXCHG01: return {2, 2};
  case SELFXCHG10: return {2, 2};
  case WIREA:
  case WIREB:
  case WIRE0A:
  case WIRE0B:
  case WIRE1A:
  case WIRE1B:
    return {1, 1};
  case COMBINE01:
  case COMBINE10: return {2, 1};
  case XCHG00:
  case XCHG01:
  case XCHG10:
  case XCHG11: return {2, 2};
  case DUPSEP0011: return {1, 4};
  case DUP0:
  case DUP1: return {1, 2};
  case SINK: return {1, 0};
  case CONST0:
  case CONST1: return {0, 1};
  default:
    LOG(FATAL) << "Unimplemented gate in GateSize? "
               << GateString(g);
    return {0, 0};
  }
}

std::pair<int, int> LayerArity(const Layer &layer) {
  int inputs = 0, outputs = 0;
  for (const Cell &c : layer) {
    const auto &[i, o] = GateArity(c.gate);
    inputs += i;
    outputs += o;
  }
  return std::make_pair(inputs, outputs);
}

std::vector<Func> TransformCell(const Cell &cell,
                                std::span<const Func> in) {
  const auto &[num_in, num_out] = GateArity(cell.gate);
  CHECK(in.size() == num_in) << CellString(cell)
                             << " in.size: " << in.size()
                             << " num_in: " << num_in;
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

  case OR1100: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    const Func &fc = in[InputIdx(2)];
    const Func &fd = in[InputIdx(3)];

    // We can assume fa.prop = fc.prop,
    // fb.prop = fd.prop.

    CHECK(fa.type == CType::ONE);
    CHECK(fb.type == CType::ONE);
    CHECK(fc.type == CType::ZERO);
    CHECK(fd.type == CType::ZERO);

    out[OutputIdx(0)] = Func{
      .prop = fa.prop | fb.prop,
      .type = CType::MIXED,
    };

    break;
  }

  case AND0110: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    const Func &fc = in[InputIdx(2)];
    const Func &fd = in[InputIdx(3)];

    // We can assume fa.prop = fb.prop,
    // fc.prop = fd.prop.

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

  case SELFXCHG01: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    // Assume fa.prop = fb.prop.

    CHECK(fa.type == CType::ZERO);
    CHECK(fb.type == CType::ONE);
    out[OutputIdx(0)] = Func{
      .prop = fa.prop,
      .type = CType::ONE,
    };
    out[OutputIdx(1)] = Func{
      .prop = fa.prop,
      .type = CType::ZERO,
    };

    break;
  }

  case SELFXCHG10: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    // Assume fa.prop = fb.prop.

    CHECK(fa.type == CType::ONE);
    CHECK(fb.type == CType::ZERO);
    out[OutputIdx(0)] = Func{
      .prop = fa.prop,
      .type = CType::ZERO,
    };
    out[OutputIdx(1)] = Func{
      .prop = fa.prop,
      .type = CType::ONE,
    };

    break;
  }

  case NOT: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::MIXED);
    out[OutputIdx(0)] = Func{.prop = -f.prop, .type = CType::MIXED};
    break;
  }

  case NOT0: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::ZERO);
    out[OutputIdx(0)] = Func{.prop = -f.prop, .type = CType::ONE};
    break;
  }

  case NOT1: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::ONE) << TypeString(f.type);
    out[OutputIdx(0)] = Func{.prop = -f.prop, .type = CType::ZERO};
    break;
  }

  case NOT01: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    // fa = fb

    CHECK(fa.type == CType::ZERO);
    CHECK(fb.type == CType::ONE);
    out[OutputIdx(0)] = Func{.prop = -fa.prop, .type = CType::MIXED};
    break;
  }

  case SEPARATOR01: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::MIXED);
    out[OutputIdx(0)] = Func{.prop = f.prop, .type = CType::ZERO};
    out[OutputIdx(1)] = Func{.prop = f.prop, .type = CType::ONE};
    break;
  }

  case SEPARATOR10: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::MIXED);
    out[OutputIdx(0)] = Func{.prop = f.prop, .type = CType::ONE};
    out[OutputIdx(1)] = Func{.prop = f.prop, .type = CType::ZERO};
    break;
  }

  case WIREA:
  case WIREB: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::MIXED);

    out[OutputIdx(0)] = f;
    break;
  }

  case WIRE0A:
  case WIRE0B: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::ZERO);

    out[OutputIdx(0)] = f;
    break;
  }

  case WIRE1A:
  case WIRE1B: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::ONE);

    out[OutputIdx(0)] = f;
    break;
  }

  case COMBINE01: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    // fa = fb
    CHECK(fa.type == CType::ZERO);
    CHECK(fb.type == CType::ONE);

    out[OutputIdx(0)] = Func{.prop = fa.prop, .type = CType::MIXED};
    break;
  }

  case COMBINE10: {
    const Func &fa = in[InputIdx(0)];
    const Func &fb = in[InputIdx(1)];
    // fa = fb
    CHECK(fa.type == CType::ONE);
    CHECK(fb.type == CType::ZERO);

    out[OutputIdx(0)] = Func{.prop = fa.prop, .type = CType::MIXED};
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

  case DUP0: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::ZERO);
    out[OutputIdx(0)] = Func{.prop = f.prop, .type = CType::ZERO};
    out[OutputIdx(1)] = Func{.prop = f.prop, .type = CType::ZERO};
    break;
  }

  case DUP1: {
    const Func &f = in[InputIdx(0)];
    CHECK(f.type == CType::ONE);
    out[OutputIdx(0)] = Func{.prop = f.prop, .type = CType::ONE};
    out[OutputIdx(1)] = Func{.prop = f.prop, .type = CType::ONE};
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
    LOG(FATAL) << "Unimplemented gate in TransformCell? "
               << CellString(cell);
  }

  CHECK(out.size() == num_out);
  return out;
}

std::vector<Func> Transform(const Layer &layer,
                            const std::vector<Func> &in) {
  const auto &[num_inputs, num_outputs] = LayerArity(layer);
  CHECK(num_inputs == in.size());
  std::vector<Func> out;
  out.reserve(num_outputs);

  int in_idx = 0;
  int out_idx = 0;
  for (const Cell &cell : layer) {
    const auto &[cell_num_in, cell_num_out] = GateArity(cell.gate);

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
