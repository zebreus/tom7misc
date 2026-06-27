
#include "cell-library.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "circuit.h"
#include "inline-vector.h"
#include "level.h"

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

// Get the input/output types for the gate (unflipped).
static std::pair<InlineVector<CType>, InlineVector<CType>>
GetType(Gate g) {
  switch (g) {
  case SPACER: return {{}, {}};
  case AND0110:
    return {{CType::ZERO, CType::ONE, CType::ONE, CType::ZERO},
            {CType::MIXED}};
  case NOT: return {{CType::MIXED}, {CType::MIXED}};
  case NOT0: return {{CType::ZERO}, {CType::ONE}};
  case NOT1: return {{CType::ONE}, {CType::ZERO}};
  case NOT01: return {{CType::ZERO, CType::ONE}, {CType::MIXED}};
  case SEPARATOR: return {{CType::MIXED}, {CType::ZERO, CType::ONE}};
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
          std::unique_ptr<Level> level = Levels::LoadSVG(filename);
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
    Load("cell-separator.svg", Gate::SEPARATOR, 0);
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
      Gate g =
        (t == CType::MIXED) ? Gate::WIREA :
        (t == CType::ONE) ? Gate::WIRE1A : Gate::WIRE0A;

      Load("cell-wirea0.svg", g, 0);
      Load("cell-wirean1.svg", g, -1);
      Load("cell-wirean2.svg", g, -2);
      Load("cell-wirean4.svg", g, -4);
      Load("cell-wirean8.svg", g, -8);
      Load("cell-wirean16.svg", g, -16);
      Load("cell-wirean32.svg", g, -32);
      Load("cell-wirean64.svg", g, -64);
    }

    for (CType t : { CType::MIXED, CType::ZERO, CType::ONE }) {
      Gate g =
        (t == CType::MIXED) ? Gate::WIREB :
        (t == CType::ONE) ? Gate::WIRE1B : Gate::WIRE0B;

      Load("cell-wireb0.svg", g, 0);
      Load("cell-wirebp1.svg", g, 1);
      Load("cell-wirebp2.svg", g, 2);
      Load("cell-wirebp4.svg", g, 4);
      Load("cell-wirebp8.svg", g, 8);
      Load("cell-wirebp16.svg", g, 16);
      Load("cell-wirebp32.svg", g, 32);
      Load("cell-wirebp64.svg", g, 64);
    }

    // Spacer is an empty level and can exist at any
    // width; we create it dynamically.
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
                            << CellString(cell);


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
      int bw = it->second.block_width;

      for (int &in : result->inputs) {
        in = bw - Levels::IN_WIDTH - in;
      }
      std::reverse(result->inputs.begin(), result->inputs.end());

      for (int &out : result->outputs) {
        out = bw - Levels::OUT_WIDTH - out;
      }
      std::reverse(result->outputs.begin(), result->outputs.end());

      float total_width = bw * Levels::BLOCK_SIZE;
      for (LevelBody &body : result->bodies) {
        body.pos.x = total_width - body.pos.x;
        body.vel.x = -body.vel.x;
        body.angle = -body.angle;
        body.avel = -body.avel;
        for (auto &v : body.mesh.vertices) {
          v.x = -v.x;
        }
        // Reverse vertices to maintain winding order
        std::reverse(body.mesh.vertices.begin(), body.mesh.vertices.end());
      }
    }

    return result;
  }

};


Cell CellLibrary::Spacer(int width) {
  CHECK(width > 0) << "Spacer width must be positive: " << width;
  return Cell(Gate::SPACER, width);
}

Cell CellLibrary::WireA(int k, CType t) {
  CHECK(k == 0 || k == 1 || k == 2 || k == 4 || k == 8 || k == 16 ||
        k == 32 || k == 64) << "Invalid offset for WireA: " << k;
  Gate g =
    (t == CType::MIXED) ? Gate::WIREA :
    (t == CType::ONE) ? Gate::WIRE1A : Gate::WIRE0A;
  return Cell(g, -k);
}

Cell CellLibrary::WireB(int k, CType t) {
  CHECK(k == 0 || k == 1 || k == 2 || k == 4 || k == 8 || k == 16 ||
        k == 32 || k == 64) << "Invalid offset for WireB: " << k;
  Gate g =
    (t == CType::MIXED) ? Gate::WIREB :
    (t == CType::ONE) ? Gate::WIRE1B : Gate::WIRE0B;
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
