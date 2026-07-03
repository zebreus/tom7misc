
#include "render-circuit.h"

#include <algorithm>
#include <string_view>

#include "cell-library.h"
#include "circuit.h"
#include "image.h"
#include "level.h"
#include "bounds.h"

static constexpr int PAD_WIDTH = Levels::IN_WIDTH;

static constexpr uint32_t BGCOLOR = 0x000000FF;

// Pad colors.
static constexpr uint32_t MIXED_OUTPUT = 0x0000FFFF;
static constexpr uint32_t MIXED_INPUT = 0x0000AAFF;
static constexpr uint32_t ZERO_OUTPUT = 0xFF0000FF;
static constexpr uint32_t ZERO_INPUT = 0xAA0000FF;
static constexpr uint32_t ONE_OUTPUT = 0x00FF00FF;
static constexpr uint32_t ONE_INPUT = 0x00AA00FF;

static constexpr uint32_t USEFUL_CELL_COLOR = 0xAAAAAAFF;
static constexpr uint32_t CELL_COLOR = 0x666666FF;
static constexpr uint32_t CELL_BORDER_COLOR = 0x333333FF;

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

ImageRGBA RenderCircuit(const CellLibrary &library,
                        const Circuit &circuit) {
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
    int cx = 0;
    int prev_input_x = -1;
    for (const Cell &cell : layer) {
      CellLibrary::Info info = library.GetInfo(cell);
      int bw = info.block_width;

      if (bw > 0) {
        if (cell.gate == Gate::SPACER) {
          img.FillRect32(cx, cy + layer_height / 2, bw, 1, SPACER_COLOR);
        } else {
          int cell_y = cy + pad_height;
          int cell_h = layer_height - 2 * pad_height;
          img.FillRect32(cx, cell_y, bw, cell_h, CELL_BORDER_COLOR);
          if (bw > 2 && cell_h > 2) {
            img.FillRect32(cx + 1, cell_y + 1, bw - 2, cell_h - 2, CELL_COLOR);
          }

          for (const CellLibrary::IO &io : info.inputs) {
            int px = cx + io.xblock;
            img.FillRect32(px, cy, PAD_WIDTH, pad_height,
                           GetInputColor(io.type));
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
            img.FillRect32(px, py, PAD_WIDTH, pad_height,
                           GetOutputColor(io.type));
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
    cy += layer_height;
  }

  return img;
}

ImageRGBA RenderCircuitMini(const CellLibrary &library,
                            const Circuit &circuit) {
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
  int img_h = (int)circuit.layers.size() * layer_height;

  if (img_w == 0 || img_h == 0) {
    return ImageRGBA(img_w > 0 ? img_w : 1, img_h > 0 ? img_h : 1);
  }

  ImageRGBA img(img_w, img_h);
  img.Clear32(BGCOLOR);

  int cy = 0;
  for (const Layer &layer : circuit.layers) {
    int cx = 0;
    for (const Cell &cell : layer) {
      CellLibrary::Info info = library.GetInfo(cell);
      int bw = info.block_width;

      if (bw > 0 && cell.gate != Gate::SPACER) {
        int px = (cx + 2) / 5;
        int next_px = (cx + bw + 2) / 5;
        int pw = std::max(1, next_px - px);

        uint32_t cc = IsWire(cell.gate) ? CELL_COLOR : USEFUL_CELL_COLOR;

        int cell_y = cy + pad_height;
        int cell_h = layer_height - 2 * pad_height;
        img.FillRect32(px, cell_y, pw, cell_h, CELL_BORDER_COLOR);
        if (pw > 2 && cell_h > 2) {
          img.FillRect32(px + 1, cell_y + 1, pw - 2, cell_h - 2, cc);
        }

        for (const CellLibrary::IO &io : info.inputs) {
          int pad_x = (cx + io.xblock + 2) / 5;
          pad_x = std::clamp(pad_x, px, px + pw - 1);
          img.FillRect32(pad_x, cy, 1, pad_height,
                         GetInputColor(io.type));
        }

        for (const CellLibrary::IO &io : info.outputs) {
          int pad_x = (cx + io.xblock + 2) / 5;
          pad_x = std::clamp(pad_x, px, px + pw - 1);
          int py = cy + layer_height - pad_height;
          img.FillRect32(pad_x, py, 1, pad_height,
                         GetOutputColor(io.type));
        }
      }

      cx += bw;
    }
    cy += layer_height;
  }

  return img;
}

