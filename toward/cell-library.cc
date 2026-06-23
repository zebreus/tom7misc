
#include "cell-library.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/print.h"
#include "circuit.h"
#include "hashing.h"
#include "level.h"
#include "ansi.h"

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
  void Load(std::string_view filename, Gate gate, int v) {
    std::unique_ptr<Level> level = Levels::LoadSVG(filename);
    CHECK(level.get() != nullptr) << "Missing/invalid: " << filename;

    InternalInfo entry;

    for (int ix : level->inputs) {
      CellLibrary::IO io;
      io.xblock = ix;
      entry.inputs.push_back(io);
    }
    for (int ox : level->outputs) {
      CellLibrary::IO io;
      io.xblock = ox;
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

    entry.level = std::move(level);

    Cell key{
      .gate = gate,
      .v = v,
      .flip = false,
    };

    info[key] = std::move(entry);
  }

  CellLibraryImpl() {
    Load("cell-not.svg", Gate::NOT, 0);
    Load("cell-dupsep0011.svg", Gate::DUPSEP0011, 0);
    Load("cell-and0110.svg", Gate::AND0110, 0);
    Load("cell-separator.svg", Gate::SEPARATOR, 0);
    Load("cell-sink.svg", Gate::SINK, 0);
    Load("cell-selfxchg.svg", Gate::SELFXCHG, 0);
    Load("cell-xchg00.svg", Gate::XCHG00, 0);
    Load("cell-xchg01.svg", Gate::XCHG01, 0);
    Load("cell-xchg10.svg", Gate::XCHG10, 0);
    Load("cell-xchg11.svg", Gate::XCHG11, 0);

    Load("cell-wirea0.svg", Gate::WIREA, 0);
    Load("cell-wirean1.svg", Gate::WIREA, -1);
    Load("cell-wirean2.svg", Gate::WIREA, -2);
    Load("cell-wirean4.svg", Gate::WIREA, -4);
    Load("cell-wirean8.svg", Gate::WIREA, -8);
    Load("cell-wirean16.svg", Gate::WIREA, -16);
    Load("cell-wirean32.svg", Gate::WIREA, -32);
    Load("cell-wirean64.svg", Gate::WIREA, -64);

    Load("cell-wireb0.svg", Gate::WIREB, 0);
    Load("cell-wirebp1.svg", Gate::WIREB, 1);
    Load("cell-wirebp2.svg", Gate::WIREB, 2);
    Load("cell-wirebp4.svg", Gate::WIREB, 4);
    Load("cell-wirebp8.svg", Gate::WIREB, 8);
    Load("cell-wirebp16.svg", Gate::WIREB, 16);
    Load("cell-wirebp32.svg", Gate::WIREB, 32);
    Load("cell-wirebp64.svg", Gate::WIREB, 64);

    // Spacer is an empty level and can exist at any
    // width; we create it dynamically.
  }

  CellLibrary::Info GetInfo(const Cell &cell) const {
    if (cell.gate == Gate::SPACER) {
      CHECK(cell.v > 0) << "Spacers must be positive width.";
      return CellLibrary::Info{.block_width = cell.v};
    }

    Cell base = cell;
    base.flip = false;
    auto it = info.find(base);
    CHECK(it != info.end()) << "Cell not found in library";

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
    CHECK(it != info.end()) << "Cell not found in library";

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
  CHECK(width > 0) << "Spacer width must be positive";
  return Cell{.gate = Gate::SPACER, .v = width, .flip = false};
}

Cell CellLibrary::WireA(int k) {
  CHECK(k == 0 || k == 1 || k == 2 || k == 4 || k == 8 || k == 16 ||
        k == 32 || k == 64) << "Invalid offset for WireA: " << k;
  return Cell{.gate = Gate::WIREA, .v = -k, .flip = false};
}

Cell CellLibrary::WireB(int k) {
  CHECK(k == 0 || k == 1 || k == 2 || k == 4 || k == 8 || k == 16 ||
        k == 32 || k == 64) << "Invalid offset for WireB: " << k;
  return Cell{.gate = Gate::WIREB, .v = k, .flip = false};
}

CellLibrary::CellLibrary() : impl(new CellLibraryImpl) {}

CellLibrary::~CellLibrary() {}

CellLibrary::Info CellLibrary::GetInfo(const Cell &cell) const {
  return impl->GetInfo(cell);
}

std::unique_ptr<Level> CellLibrary::GetLevel(const Cell &cell) const {
  return impl->GetLevel(cell);
}
