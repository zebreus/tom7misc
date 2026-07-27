
#include "render-circuit.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "cell-library.h"
#include "circuit.h"
#include "image.h"
#include "layout.h"
#include "level.h"
#include "color-util.h"

static constexpr int PAD_WIDTH = Levels::IN_WIDTH;

static constexpr uint32_t BGCOLOR = 0x000000FF;

static constexpr uint32_t Darken(uint32_t c) {
  const auto &[r, g, b, a] = ColorUtil::Unpack32(c);
  return ColorUtil::Pack32(r >> 1, g >> 1, b >> 1, a);
}

// Pad colors.
static constexpr uint32_t MIXED_OUTPUT = 0x0000FFFF;
static constexpr uint32_t MIXED_INPUT = 0x0000AAFF;
static constexpr uint32_t ZERO_OUTPUT = 0xFF0000FF;
static constexpr uint32_t ZERO_INPUT = 0xAA0000FF;
static constexpr uint32_t ONE_OUTPUT = 0x00FF00FF;
static constexpr uint32_t ONE_INPUT = 0x00AA00FF;

static constexpr uint32_t USEFUL_CELL_COLOR = 0xAAAAAAFF;
static constexpr uint32_t CELL_COLOR = 0x666666FF;

static constexpr uint32_t XCHG_COLOR = 0x888888FF;
static constexpr uint32_t DUP_COLOR = 0xFFFF00FF;

// A wire cell that takes a variable.
static constexpr uint32_t VAR_WIRE_COLOR = 0x332233FF;

static constexpr uint32_t SPACER_COLOR = 0x000022FF;

static constexpr uint32_t PAD_WARNING_COLOR = 0xFF00FFFF;

// If two pads on the same layer are closer than this, draw
// a magenta line between them.
static constexpr int WARN_PAD_THRESHOLD = 16;

static uint32_t GetInputColor(CType type) {
  switch (type) {
    case CType::ZERO: return ZERO_INPUT;
    case CType::ONE: return ONE_INPUT;
    case CType::MIXED: return MIXED_INPUT;
  }
  return MIXED_INPUT;
}

static uint32_t GetOutputColor(CType type) {
  switch (type) {
    case CType::ZERO: return ZERO_OUTPUT;
    case CType::ONE: return ONE_OUTPUT;
    case CType::MIXED: return MIXED_OUTPUT;
  }
  return MIXED_OUTPUT;
}

static bool IsXchg(Gate gate) {
  switch (gate) {
    case Gate::XCHG00:
    case Gate::XCHG01:
    case Gate::XCHG10:
    case Gate::XCHG11:
    case Gate::SELFXCHG01:
    case Gate::SELFXCHG10:
      return true;
    default:
      return false;
  }
}

static bool IsDup(Gate gate) {
  switch (gate) {
    case Gate::DUPSEP0011:
    case Gate::DUP0:
    case Gate::DUP1:
      return true;
    default:
      return false;
  }
}

static void TransferIsVar(Gate gate, bool in0, bool in1,
                          std::vector<bool> &out) {
  if (IsWire(gate)) {
    out.push_back(in0);
    return;
  }
  switch (gate) {
    case Gate::XCHG00:
    case Gate::XCHG01:
    case Gate::XCHG10:
    case Gate::XCHG11:
    case Gate::SELFXCHG01:
    case Gate::SELFXCHG10:
      out.push_back(in1);
      out.push_back(in0);
      break;
    case Gate::DUPSEP0011:
    case Gate::DUP0:
    case Gate::DUP1:
    case Gate::SEPARATOR01:
    case Gate::SEPARATOR10:
      out.push_back(in0);
      out.push_back(in0);
      break;
    case Gate::COMBINE01:
    case Gate::COMBINE10:
      out.push_back(in0 || in1);
      break;
    default:
      for (int i = 0; i < GateArity(gate).second; i++) {
        out.push_back(false);
      }
      break;
  }
}

ImageRGBA RenderCircuit(const CellLibrary &library,
                        const Circuit &circuit,
                        std::vector<bool> is_var) {
  int max_w = 0;
  for (const Layer &layer : circuit.layers) {
    int w = 0;
    for (const Cell &cell : layer) {
      w += library.GetInfo(cell).block_width;
    }
    max_w = std::max(w, max_w);
  }

  const int layer_height = 32;
  const int pad_height = 5;
  const int char_width = ImageRGBA::TEXT_WIDTH;
  const int char_height = ImageRGBA::TEXT_HEIGHT;

  int img_w = max_w;
  int img_h = (int)circuit.layers.size() * layer_height;

  if (img_w == 0 || img_h == 0) {
    return ImageRGBA(img_w > 0 ? img_w : 1, img_h > 0 ? img_h : 1);
  }

  ImageRGBA img(img_w, img_h);
  img.Clear32(BGCOLOR);

  int cy = 0;
  for (const Layer &layer : circuit.layers) {
    std::vector<bool> next_is_var;
    int input_idx = 0;

    int cx = 0;
    int prev_input_x = -1;
    for (const Cell &cell : layer) {
      CellLibrary::Info info = library.GetInfo(cell);
      int bw = info.block_width;

      int num_inputs = (int)info.inputs.size();
      bool in0 = false, in1 = false;
      if (num_inputs > 0 && input_idx < (int)is_var.size()) {
        in0 = is_var[input_idx];
      }
      if (num_inputs > 1 && input_idx + 1 < (int)is_var.size()) {
        in1 = is_var[input_idx + 1];
      }
      bool cell_is_var = in0 || in1;
      TransferIsVar(cell.gate, in0, in1, next_is_var);
      input_idx += num_inputs;

      if (bw > 0) {
        if (cell.gate == Gate::SPACER) {
          img.FillRect32(cx, cy + layer_height / 2, bw, 1, SPACER_COLOR);
        } else {
          int cell_y = cy + pad_height;
          int cell_h = layer_height - 2 * pad_height;
          // For the larger rendering, we don't use USEFUL_CELL_COLOR;
          // the text on the cell is how we visually distinguish it.
          uint32_t cc = CELL_COLOR;
          if (IsWire(cell.gate) && cell_is_var) {
            cc = VAR_WIRE_COLOR;
          } else if (IsXchg(cell.gate)) {
            cc = XCHG_COLOR;
          } else if (IsDup(cell.gate)) {
            cc = DUP_COLOR;
          }

          img.FillRect32(cx, cell_y, bw, cell_h, Darken(cc));
          if (bw > 2 && cell_h > 2) {
            img.FillRect32(cx + 1, cell_y + 1, bw - 2, cell_h - 2, cc);
          }

          for (const CellLibrary::IO &io : info.inputs) {
            int px = cx + io.xblock;
            uint32_t ic = GetInputColor(io.type);
            if (cell_is_var) ic = Darken(ic);
            img.FillRect32(px, cy, PAD_WIDTH, pad_height, ic);
            if (prev_input_x >= 0) {
              int dist = px - (prev_input_x + PAD_WIDTH);
              if (dist > 0 && dist < WARN_PAD_THRESHOLD) {
                img.FillRect32(prev_input_x + PAD_WIDTH, cy, dist, 3,
                               PAD_WARNING_COLOR);
              }
            }
            prev_input_x = px;
          }

          for (const CellLibrary::IO &io : info.outputs) {
            int px = cx + io.xblock;
            int py = cy + layer_height - pad_height;
            uint32_t oc = GetOutputColor(io.type);
            if (cell_is_var) oc = Darken(oc);
            img.FillRect32(px, py, PAD_WIDTH, pad_height, oc);
          }

          if (!IsWire(cell.gate)) {
            std::string_view name = GateString(cell.gate);
            int max_chars = bw / char_width;
            if (max_chars > 0) {
              if ((int)name.size() > max_chars) {
                name = name.substr(0, max_chars);
              }
              int text_x = cx + (bw - (int)name.size() * char_width) / 2;
              int text_y = cy + (layer_height - char_height) / 2;
              img.BlendText32(text_x, text_y, 0xFFFFFFFF, name);
            }
          }
        }
      }

      cx += bw;
    }
    is_var = std::move(next_is_var);
    cy += layer_height;
  }

  return img;
}

ImageRGBA RenderLayout(const CellLibrary &library,
                       const Layout &layout) {
  // All vars on the input layer by definition.
  std::vector<bool> is_var(layout.input_vars.size(), true);
  return RenderCircuit(library, layout.circuit, is_var);
}


ImageRGBA RenderCircuitMini(
    const CellLibrary &library,
    const Circuit &circuit,
    std::vector<bool> is_var) {
  int max_w = 0;
  for (const Layer &layer : circuit.layers) {
    int w = 0;
    for (const Cell &cell : layer) {
      w += library.GetInfo(cell).block_width;
    }
    max_w = std::max(w, max_w);
  }

  const int layer_height = 6;
  const int pad_height = 1;

  int img_w = (max_w + 2) / 5;
  int num_layers = (int)circuit.layers.size();
  // Ensure the image is tall enough for a 16:9 (1920x1080) aspect ratio.
  int min_layers = (img_w * 9 + 16 * layer_height - 1) / (16 * layer_height);
  num_layers = std::max(num_layers, min_layers);
  int img_h = num_layers * layer_height;

  if (img_w == 0 || img_h == 0) {
    return ImageRGBA(img_w > 0 ? img_w : 1, img_h > 0 ? img_h : 1);
  }

  ImageRGBA img(img_w, img_h);
  img.Clear32(BGCOLOR);

  int cy = 0;
  for (const Layer &layer : circuit.layers) {
    std::vector<bool> next_is_var;
    int input_idx = 0;

    int cx = 0;
    for (const Cell &cell : layer) {
      CellLibrary::Info info = library.GetInfo(cell);
      int bw = info.block_width;

      int num_inputs = (int)info.inputs.size();
      bool var0 = false, var1 = false;
      if (num_inputs > 0 && input_idx < (int)is_var.size()) {
        var0 = is_var[input_idx];
      }
      if (num_inputs > 1 && input_idx + 1 < (int)is_var.size()) {
        var1 = is_var[input_idx + 1];
      }
      bool cell_is_var = var0 || var1;
      TransferIsVar(cell.gate, var0, var1, next_is_var);
      input_idx += num_inputs;

      if (bw > 0 && cell.gate != Gate::SPACER) {
        int px = (cx + 2) / 5;
        int next_px = (cx + bw + 2) / 5;
        int pw = std::max(1, next_px - px);

        uint32_t cc = USEFUL_CELL_COLOR;
        if (IsWire(cell.gate)) {
          cc = cell_is_var ? VAR_WIRE_COLOR : CELL_COLOR;
        } else if (IsXchg(cell.gate)) {
          cc = XCHG_COLOR;
        } else if (IsDup(cell.gate)) {
          cc = DUP_COLOR;
        }

        int cell_y = cy + pad_height;
        int cell_h = layer_height - 2 * pad_height;
        img.FillRect32(px, cell_y, pw, cell_h, Darken(cc));
        if (pw > 2 && cell_h > 2) {
          img.FillRect32(px + 1, cell_y + 1, pw - 2, cell_h - 2, cc);
        }

        for (const CellLibrary::IO &io : info.inputs) {
          int pad_x = (cx + io.xblock + 2) / 5;
          pad_x = std::clamp(pad_x, px, px + pw - 1);
          uint32_t ic = GetInputColor(io.type);
          if (cell_is_var) ic = Darken(ic);
          img.FillRect32(pad_x, cy, 1, pad_height, ic);
        }

        for (const CellLibrary::IO &io : info.outputs) {
          int pad_x = (cx + io.xblock + 2) / 5;
          pad_x = std::clamp(pad_x, px, px + pw - 1);
          int py = cy + layer_height - pad_height;
          uint32_t oc = GetOutputColor(io.type);
          if (cell_is_var) oc = Darken(oc);
          img.FillRect32(pad_x, py, 1, pad_height, oc);
        }
      }

      cx += bw;
    }
    is_var = std::move(next_is_var);
    cy += layer_height;
  }

  return img;
}
