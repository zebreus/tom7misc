
#include "cell-library.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "circuit.h"
#include "inline-vector.h"
#include "level.h"

static constexpr bool VERBOSE = false;

namespace {
struct HashCell {
  size_t operator()(const Cell &cell) const {
    size_t ret = cell.gate << 24;
    ret *= 31337;
    uint32_t uv = (uint32_t)cell.v;
    ret ^= uv;
    if (cell.flip) ret ^= 0x5a5a5a5a;
    return ret;
  }
};

struct EqCell {
  bool operator()(const Cell &a, const Cell &b) const {
    return a.gate == b.gate && a.v == b.v && a.flip == b.flip;
  }
};
}  // namespace

using Bias = CellLibrary::Bias;

// Get the input/output types for the gate (unflipped).
static std::pair<InlineVector<CType>, InlineVector<CType>>
GetType(Gate g) {
  switch (g) {
  case SPACER: return {{}, {}};

  case AND0110:
    return {{CType::ZERO, CType::ONE, CType::ONE, CType::ZERO},
            {CType::MIXED}};
  case OR1100:
    return {{CType::ONE, CType::ONE, CType::ZERO, CType::ZERO},
            {CType::MIXED}};
  case NOR1100:
    return {{CType::ONE, CType::ONE, CType::ZERO, CType::ZERO},
            {CType::MIXED}};

  case NAND0011:
    return {{CType::ZERO, CType::ZERO, CType::ONE, CType::ONE},
            {CType::MIXED}};

  case XOR1010:
    return {{CType::ONE, CType::ZERO, CType::ONE, CType::ZERO},
            {CType::MIXED}};

  case XOR1100:
    return {{CType::ONE, CType::ONE, CType::ZERO, CType::ZERO},
            {CType::MIXED}};

  case ITE10:
    return {{CType::MIXED, CType::ONE, CType::ZERO, CType::MIXED},
            {CType::MIXED}};

  case NOT: return {{CType::MIXED}, {CType::MIXED}};
  case NOT0: return {{CType::ZERO}, {CType::ONE}};
  case NOT1: return {{CType::ONE}, {CType::ZERO}};
  case NOT01: return {{CType::ZERO, CType::ONE}, {CType::MIXED}};
  case SEPARATOR01: return {{CType::MIXED}, {CType::ZERO, CType::ONE}};
  case SEPARATOR10: return {{CType::MIXED}, {CType::ONE, CType::ZERO}};
  case SELFXCHG01:
    return {{CType::ZERO, CType::ONE}, {CType::ONE, CType::ZERO}};
  case SELFXCHG10:
    return {{CType::ONE, CType::ZERO}, {CType::ZERO, CType::ONE}};
  case WIREA:
  case WIREB: return {{CType::MIXED}, {CType::MIXED}};
  case WIRE0A:
  case WIRE0B: return {{CType::ZERO}, {CType::ZERO}};
  case WIRE1A:
  case WIRE1B: return {{CType::ONE}, {CType::ONE}};
  case COMBINE01: return {{CType::ZERO, CType::ONE}, {CType::MIXED}};
  case COMBINE10: return {{CType::ONE, CType::ZERO}, {CType::MIXED}};
  case XCHG00: return {{CType::ZERO, CType::ZERO}, {CType::ZERO, CType::ZERO}};
  case XCHG01: return {{CType::ZERO, CType::ONE}, {CType::ONE, CType::ZERO}};
  case XCHG10: return {{CType::ONE, CType::ZERO}, {CType::ZERO, CType::ONE}};
  case XCHG11: return {{CType::ONE, CType::ONE}, {CType::ONE, CType::ONE}};
  case DUPSEP0011:
    return {{CType::MIXED},
            {CType::ZERO, CType::ZERO, CType::ONE, CType::ONE}};
  case DUP0:
    return {{CType::ZERO}, {CType::ZERO, CType::ZERO}};
  case DUP1:
    return {{CType::ONE}, {CType::ONE, CType::ONE}};
  case SINK: return {{CType::MIXED}, {}};
  case CONST0:
  case CONST1: return {{}, {CType::MIXED}};
  default: return {{}, {}};
  }
}


struct CellLibraryImpl {

  // Info about a loaded cell.
  struct InternalInfo {
    int block_width = 0;

    std::vector<CellLibrary::IO> inputs;
    std::vector<CellLibrary::IO> outputs;

    std::unique_ptr<Level> level;
  };

  std::unordered_map<Cell, InternalInfo, HashCell, EqCell> info;

  int min_clearance_close = 0;
  int min_clearance_far = 0;

  // Load the geometry from the SVG file. This is the
  // unflipped version. v is usually zero unless the
  // gate is parameterized (spacer and wires).
  //
  // We need to deduce the width from the level geometry,
  // rounding up to the nearest block. Blocks can have
  // vertices on the boundary. We should Print a warning
  // if there is space on the left edge (of one block or
  // more).
  void LoadWithCache(
      // Used during initialization and cleared after. Can be empty.
      std::unordered_map<std::string, std::unique_ptr<Level>> *svg_cache,
      std::string_view filename, Gate gate, int v) {
    std::unique_ptr<Level> level = [&]{
        std::string f(filename);
        auto it = svg_cache->find(f);
        if (it == svg_cache->end()) {
          std::unique_ptr<Level> level = Levels::LoadSVG(filename, false);
          CHECK(level.get() != nullptr) << "Missing/invalid: " << filename;
          (*svg_cache)[f] = std::make_unique<Level>(*level);
          return level;
        } else {
          return std::make_unique<Level>(*it->second);
        }
      }();

    InternalInfo entry;

    auto [in_types, out_types] = GetType(gate);

    for (size_t i = 0; i < level->inputs.size(); i++) {
      CellLibrary::IO io;
      io.xblock = level->inputs[i];
      if (i < in_types.size()) {
        io.type = in_types[i];
      }
      // Should maybe also have props here?
      entry.inputs.push_back(io);
    }
    for (size_t i = 0; i < level->outputs.size(); i++) {
      CellLibrary::IO io;
      io.xblock = level->outputs[i];
      if (i < out_types.size()) {
        io.type = out_types[i];
      }
      entry.outputs.push_back(io);
    }

    float min_x = 1e9f, max_x = -1e9f;
    auto ObserveX = [&](float x) {
      if (x < min_x) min_x = x;
      if (x > max_x) max_x = x;
    };

    // Inspect the mesh to find the bounding box.
    for (const LevelBody &body : level->bodies) {
      float px = body.pos.x;
      for (const auto &p : body.mesh.vertices) ObserveX(px + p.x);
    }

    for (int ix : level->inputs) {
      ObserveX(ix * Levels::BLOCK_SIZE);
      ObserveX((ix + Levels::IN_WIDTH) * Levels::BLOCK_SIZE);
    }
    for (int ox : level->outputs) {
      ObserveX(ox * Levels::BLOCK_SIZE);
      ObserveX((ox + Levels::OUT_WIDTH) * Levels::BLOCK_SIZE);
    }

    if (min_x > max_x) {
      min_x = 0.0f;
      max_x = 0.0f;
    }

    int min_block = (int)std::round(min_x / Levels::BLOCK_SIZE);
    if (min_block != 0) {
      Print(AORANGE("Warning") ": left edge not flush ({}) in {}\n",
            min_block,
            filename);
    }

    entry.block_width = (int)std::ceil(max_x / Levels::BLOCK_SIZE - 0.01f);
    if (entry.block_width < 0) entry.block_width = 0;

    CHECK(entry.block_width >= 0) << "Negative block width computed";
    if (VERBOSE) {
      Print("Loaded {} (gate {}, v {}): min_x={:.2f}, max_x={:.2f}, width={}\n",
            filename, (int)gate, v, min_x, max_x, entry.block_width);
    }

    entry.level = std::make_unique<Level>(*level);

    Cell key(gate, v);

    info[key] = std::move(entry);
  }

  CellLibraryImpl() {
    // Some SVGs are used multiple times.
    std::unordered_map<std::string, std::unique_ptr<Level>> svg_cache;
    auto Load = [&](std::string_view s, Gate g, int v) {
        return LoadWithCache(&svg_cache, s, g, v);
      };

    Load("cell-not.svg", Gate::NOT, 0);
    Load("cell-not0.svg", Gate::NOT0, 0);
    Load("cell-not1.svg", Gate::NOT1, 0);
    Load("cell-not01.svg", Gate::NOT01, 0);
    Load("cell-dupsep0011.svg", Gate::DUPSEP0011, 0);
    Load("cell-dup0.svg", Gate::DUP0, 0);
    Load("cell-dup1.svg", Gate::DUP1, 0);
    Load("cell-and0110.svg", Gate::AND0110, 0);
    Load("cell-nand0011.svg", Gate::NAND0011, 0);
    Load("cell-xor1010.svg", Gate::XOR1010, 0);
    Load("cell-xor1100.svg", Gate::XOR1100, 0);
    Load("cell-ite10.svg", Gate::ITE10, 0);
    Load("cell-or1100.svg", Gate::OR1100, 0);
    Load("cell-nor1100.svg", Gate::NOR1100, 0);
    Load("cell-separator01.svg", Gate::SEPARATOR01, 0);
    Load("cell-separator10.svg", Gate::SEPARATOR10, 0);
    Load("cell-sink.svg", Gate::SINK, 0);
    Load("cell-const0.svg", Gate::CONST0, 0);
    Load("cell-const1.svg", Gate::CONST1, 0);
    // Same level works for both. We just need to
    // be able to give two types.
    Load("cell-selfxchg.svg", Gate::SELFXCHG01, 0);
    Load("cell-selfxchg.svg", Gate::SELFXCHG10, 0);
    Load("cell-xchg00.svg", Gate::XCHG00, 0);
    Load("cell-xchg01.svg", Gate::XCHG01, 0);
    Load("cell-xchg10.svg", Gate::XCHG10, 0);
    Load("cell-xchg11.svg", Gate::XCHG11, 0);

    // Same level works for both. We just need to
    // be able to give two types.
    Load("cell-combine.svg", Gate::COMBINE01, 0);
    Load("cell-combine.svg", Gate::COMBINE10, 0);

    for (CType t : { CType::MIXED, CType::ZERO, CType::ONE }) {
      Gate ga =
        (t == CType::MIXED) ? Gate::WIREA :
        (t == CType::ONE) ? Gate::WIRE1A : Gate::WIRE0A;
      Gate gb =
        (t == CType::MIXED) ? Gate::WIREB :
        (t == CType::ONE) ? Gate::WIRE1B : Gate::WIRE0B;

      // TODO: For size zero wires, flipping retains zero displacement,
      // so "A" and "B" are actually redundant. Maybe we should have a
      // center-bias version?

      for (int offset : CellLibrary::WIRE_SIZES) {
        if (offset < CellLibrary::SMALL_WIRE) {
          Load(std::format("cell-wire{}a.svg", offset), ga, offset);
          Load(std::format("cell-wire{}b.svg", offset), gb, offset);
        } else {
          // Large wires are stored as the A variant.
          Load(std::format("cell-wire{}.svg", offset), ga, offset);
        }
      }
    }

    // Spacer is an empty level and can exist at any
    // width; we create it dynamically.

    ComputeMinClearance();
  }

  // Compute the minimum clearance to guarantee we can attach a wire;
  // initializes the close and far min_clearance values. (This could
  // probably just look at the small-valued wires, but we might as
  // well just be comprehensive.)
  void ComputeMinClearance() {
    int max_close = 0;
    int max_far = 0;

    // XXX Wire geometry doesn't really differ by type. We should
    // probably check this though?
    for (CType type : {CType::MIXED, CType::ZERO, CType::ONE}) {
      int best_close = 1e9;
      int best_far = 1e9;
      for (int k : CellLibrary::WIRE_SIZES) {
        for (bool flip : {false, true}) {
          for (Bias bias : {Bias::LEFT, Bias::RIGHT}) {
            Cell cell = CellLibrary::Wire(k, bias, type);
            cell.flip = flip;
            CellLibrary::Info info = GetInfo(cell);
            CHECK(info.outputs.size() == 1);

            int out_x = info.outputs[0].xblock;
            int left_clearance = out_x;
            int right_clearance = info.block_width - out_x - Levels::OUT_WIDTH;

            int close = std::min(left_clearance, right_clearance);
            int far = std::max(left_clearance, right_clearance);

            // Find the wire requiring the smallest close clearance.
            // If tied, pick the one with the smallest far clearance.
            if (close < best_close || (close == best_close && far < best_far)) {
              best_close = close;
              best_far = far;
            }
          }
        }
      }
      if (best_close > max_close) {
        max_close = best_close;
      }
      if (best_far > max_far) {
        max_far = best_far;
      }
    }

    min_clearance_close = max_close;
    min_clearance_far = max_far;
  }


  CellLibrary::Info GetInfo(const Cell &cell) const {
    if (cell.gate == Gate::SPACER) {
      CHECK(cell.v > 0) << "Spacers must be positive width: "
                        << CellString(cell);
      return CellLibrary::Info{.block_width = cell.v};
    }

    Cell base = cell;
    base.flip = false;
    auto it = info.find(base);
    CHECK(it != info.end()) << "Cell not found in library: "
                            << CellString(cell) << " (via "
                            << CellString(base) << ")";


    CellLibrary::Info result;
    result.block_width = it->second.block_width;

    if (!cell.flip) {
      result.inputs = it->second.inputs;
      result.outputs = it->second.outputs;
    } else {
      for (const auto &in : it->second.inputs) {
        CellLibrary::IO flipped = in;
        flipped.xblock = result.block_width - Levels::IN_WIDTH - in.xblock;
        result.inputs.push_back(flipped);
      }
      for (const auto &out : it->second.outputs) {
        CellLibrary::IO flipped = out;
        flipped.xblock = result.block_width - Levels::OUT_WIDTH - out.xblock;
        result.outputs.push_back(flipped);
      }
      std::reverse(result.inputs.begin(), result.inputs.end());
      std::reverse(result.outputs.begin(), result.outputs.end());
    }

    return result;
  }

  std::unique_ptr<Level> GetLevel(const Cell &cell) const {
    if (cell.gate == Gate::SPACER) {
      // Default instance is empty, like we want.
      return std::make_unique<Level>();
    }

    Cell base = cell;
    base.flip = false;
    auto it = info.find(base);
    CHECK(it != info.end()) << "Cell not found in library: "
                            << CellString(cell);

    auto result = std::make_unique<Level>(*it->second.level);

    if (cell.flip) {
      Levels::FlipLevel(result.get(), it->second.block_width);
    }

    return result;
  }

};


Cell CellLibrary::Spacer(int width) {
  CHECK(width > 0) << "Spacer width must be positive: " << width;
  return Cell(Gate::SPACER, width);
}

bool CellLibrary::ValidWireSize(int s) {
  for (int ss : WIRE_SIZES)
    if (s == ss)
      return true;
  return false;
}

Cell CellLibrary::Wire(int k, Bias b, CType t) {
  CHECK(ValidWireSize(k)) << "Invalid offset for Wire: " << k;
  Gate g = [&]{
      if (k < SMALL_WIRE) {
        if (b == Bias::RIGHT) {
          return (t == CType::MIXED) ? Gate::WIREA :
            (t == CType::ONE) ? Gate::WIRE1A : Gate::WIRE0A;
        } else {
          return (t == CType::MIXED) ? Gate::WIREB :
            (t == CType::ONE) ? Gate::WIRE1B : Gate::WIRE0B;
        }
      } else {
        return (t == CType::MIXED) ? Gate::WIREA :
          (t == CType::ONE) ? Gate::WIRE1A : Gate::WIRE0A;
      }
    }();

  return Cell(g, k);
}

CellLibrary::CellLibrary() : impl(new CellLibraryImpl) {}

CellLibrary::~CellLibrary() {}

CellLibrary::Info CellLibrary::GetInfo(const Cell &cell) const {
  return impl->GetInfo(cell);
}

std::unique_ptr<Level> CellLibrary::GetLevel(const Cell &cell) const {
  return impl->GetLevel(cell);
}

std::string CellLibrary::DebugString(const Circuit &circuit) const {
  std::string out;

  for (size_t i = 0; i < circuit.layers.size(); i++) {
    AppendFormat(&out, "Layer {}:\n", i);
    const Layer &layer = circuit.layers[i];
    int current_x = 0;

    for (const Cell &cell : layer) {
      CellLibrary::Info info = GetInfo(cell);
      AppendFormat(&out, "  {} (width: {})\n",
                   CellString(cell), info.block_width);

      for (const CellLibrary::IO &in : info.inputs) {
        AppendFormat(&out, "    Input at x={} (type {})\n",
                     current_x + in.xblock, TypeString(in.type));
      }
      for (const CellLibrary::IO &out_io : info.outputs) {
        AppendFormat(&out, "    Output at x={} (type {})\n",
                     current_x + out_io.xblock, TypeString(out_io.type));
      }

      current_x += info.block_width;
    }
  }

  return out;
}

std::string CellLibrary::InfoString(const Info &info) {
  std::string s;
  AppendFormat(&s, "Cell with width {}:\n",
               info.block_width);

  for (const CellLibrary::IO &in : info.inputs) {
    AppendFormat(&s, "  Input at x={} (type {})\n",
                 in.xblock, TypeString(in.type));
  }
  for (const CellLibrary::IO &out : info.outputs) {
    AppendFormat(&s, "  Output at x={} (type {})\n",
                 out.xblock, TypeString(out.type));
  }

  return s;
}

int CellLibrary::MinClearanceClose() const {
  return impl->min_clearance_close;
}

int CellLibrary::MinClearanceFar() const {
  return impl->min_clearance_far;
}

