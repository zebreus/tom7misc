#include "circuit-sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "arcfour.h"
#include "base/logging.h"
#include "cell-library.h"
#include "circuit.h"
#include "color-util.h"
#include "drc.h"
#include "layout.h"
#include "level.h"
#include "randutil.h"
#include "rendering.h"
#include "scene.h"
#include "toward-util.h"
#include "util.h"

vec2f CircuitSim::ViewPosMax() const {
  return view_pos + vec2f{
    .x = Scene::WIDTH / view_zoom,
    .y = Scene::HEIGHT / view_zoom,
  };
}

vec2f CircuitSim::ScreenToWorld(int x, int y) const {
  vec2f bottom_right = ViewPosMax();
  return rendering->ScreenToWorld(view_pos, bottom_right, x, y);
}

void CircuitSim::Pan(int x, int y, int dx, int dy) {
  vec2f old_pos = ScreenToWorld(x - dx, y - dy);
  vec2f new_pos = ScreenToWorld(x, y);

  // Shift view_pos to keep the world coordinate under the cursor the same.
  view_pos.x += old_pos.x - new_pos.x;
  view_pos.y += old_pos.y - new_pos.y;
}

void CircuitSim::Zoom(int x, int y, bool up) {
  vec2f old_pos = ScreenToWorld(x, y);

  if (up) {
    view_zoom *= 1.25f;
  } else {
    view_zoom /= 1.25f;
  }

  vec2f new_pos = ScreenToWorld(x, y);

  // Shift view_pos to keep the world coordinate under the cursor the same.
  view_pos.x += old_pos.x - new_pos.x;
  view_pos.y += old_pos.y - new_pos.y;
}

// Ensure that the node is active, lazily loading if needed.
void CircuitSim::ActivateNode(Node &node) {
  if (node.level.get() == nullptr) {
    node.level = library.GetLevel(node.cell);
    Levels::AddChutes(node.level.get(), 0x339933FF, 0x993333FF);
  }
  CHECK(node.level.get());

  if (node.scene.get() == nullptr) {
    // Begin hibernating so that we can draw it without simulating it.
    node.scene = Levels::CreateScene(*node.level, true);
    CHECK(node.scene.get() != nullptr) << "Bug: Should always succeed "
      "when creating a hibernating scene.";

    for (int i = 0; i < node.scene->objects.size(); i++) {
      const Scene::Obj &obj = node.scene->objects[i];
      if (!node.scene->IsSimulated(obj)) continue;
      CHECK(obj.user_data.has_value()) << "Object " << i << " does "
        "not correspond to a LevelBody? We could just skip them, "
        "but we expect these all to come from the level today!";

      const int lb = obj.user_data.value();
      CHECK(lb >= 0 && lb < node.level->bodies.size());
      const LevelBody &body = node.level->bodies[lb];
      if (body.item.has_value()) {
        node.items.push_back(i);
      }
    }
  }

  CHECK(node.scene.get());
}

CircuitSim::CircuitSim(const CellLibrary &library,
                       Rendering *rendering,
                       std::string_view layout_file) :
  library(library),
  rendering(rendering),
  rc("sim") {
  std::string content = Util::ReadFile(layout_file);
  CHECK(!content.empty()) << layout_file << " missing?";

  std::optional<Layout> olay = LayoutEngine::Parse(content);
  CHECK(olay.has_value()) << "Could not parse " << layout_file;
  layout = std::move(olay.value());

  if (CircuitSize(layout.circuit) < 128) {
    Print("Layout:\n{}\n", LayoutEngine::ToString(layout));
  }

  // Maybe option to skip this in slideshow mode?
  DRC::CheckLayout(library, layout_file, layout);

  Reset();
}

CircuitSim::CircuitSim(const CellLibrary &library,
                       Rendering *rendering,
                       Layout layout) :
  library(library),
  rendering(rendering),
  layout(std::move(layout)),
  rc("sim") {

  // Maybe option to skip this in slideshow mode?
  DRC::CheckLayout(library, "CircuitSim argument", layout);

  Reset();
}

void CircuitSim::GoToTopLeftCell() {
  if (!sim.empty() && !sim[0].empty()) {
    view_pos.x = sim[0][0].xpos * Levels::BLOCK_SIZE;
    view_pos.y = 0.0f;
    view_zoom = 1.0f;
  }
}

static uint32_t ComputeLODColor(const CellLibrary &library,
                                const Cell &cell) {
  int width = library.GetInfo(cell).block_width;
  std::unique_ptr<Level> lvl = library.GetLevel(cell);
  Levels::AddChutes(lvl.get(), 0x339933FF, 0x993333FF);
  std::unique_ptr<Scene> sc = Levels::CreateScene(*lvl, true);
  CHECK(sc.get() != nullptr);

  float sum_r = 0.0f, sum_g = 0.0f, sum_b = 0.0f, sum_a = 0.0f;
  for (const Rendering::Triangle &t : sc->GetTriangles()) {
    float area = 0.5f * std::abs(
      t.a.x * (t.b.y - t.c.y) +
      t.b.x * (t.c.y - t.a.y) +
      t.c.x * (t.a.y - t.b.y));
    auto [r, g, b, a] = ColorUtil::U32ToFloats(t.rgba);
    sum_r += r * a * area;
    sum_g += g * a * area;
    sum_b += b * a * area;
    sum_a += a * area;
  }

  static constexpr int ROW_HEIGHT_BLOCKS = Levels::OUT_Y + 1;
  static constexpr float ROW_HEIGHT = ROW_HEIGHT_BLOCKS * Levels::BLOCK_SIZE;

  float total_area = width * Levels::BLOCK_SIZE * ROW_HEIGHT;
  if (total_area > 0.0f && sum_a > 0.0f) {
    const auto &[h, s, v] = ColorUtil::RGBToHSV(sum_r / sum_a,
                                                sum_g / sum_a,
                                                sum_b / sum_a);
    // Unprincipled, but: Boost saturation and value. Without this it
    // just gets way too dark, since most cells are mostly background.
    const auto &[rr, gg, bb] = ColorUtil::HSVToRGB(h, sqrt(s), sqrt(v));

    const float aa = sum_a / total_area;
    return ColorUtil::FloatsTo32(rr, gg, bb, aa);
  }
  return 0;
}

void CircuitSim::Reset() {
  sim.clear();
  active_nodes.clear();
  ticks = 0;

  std::unordered_map<Cell, uint32_t, CellHash> lod_cache;

  for (const Layer &layer : layout.circuit.layers) {
    std::vector<Node> row;
    int xpos = 0;
    for (const Cell &cell : layer) {
      int width = library.GetInfo(cell).block_width;
      if (cell.gate != Gate::SPACER) {
        Node node{
          .in_queue = false,
          .xpos = xpos,
          .cell = cell,
        };

        auto it = lod_cache.find(cell);
        if (it != lod_cache.end()) {
          node.lod_color = it->second;
        } else {
          node.lod_color = ComputeLODColor(library, cell);
          lod_cache[cell] = node.lod_color;
        }

        row.push_back(std::move(node));
      }
      xpos += width;
    }
    sim.push_back(std::move(row));
  }

  for (size_t r = 0; r < sim.size(); r++) {
    size_t next_c = 0;
    int next_in = 0;

    for (size_t c = 0; c < sim[r].size(); c++) {
      int node_outputs = GateArity(sim[r][c].cell.gate).second;
      for (int i = 0; i < node_outputs; i++) {
        std::pair<size_t, int> match = {(size_t)-1, -1};
        if (r + 1 < sim.size()) {
          while (next_c < sim[r + 1].size()) {
            int node_inputs = GateArity(sim[r + 1][next_c].cell.gate).first;
            if (next_in < node_inputs) {
              match = {next_c, next_in};
              next_in++;
              break;
            }
            next_c++;
            next_in = 0;
          }
          CHECK(match.first != (size_t)-1) << "Failed to find matching input";
          sim[r][c].matching_inputs.push_back(match);
        }
      }
    }
  }
}


// Insert a bit body into one of the node's inputs.
void CircuitSim::AddInput(size_t r, size_t c,
                          int input_idx,
                          bool one,
                          // Position of the body (relative to the
                          // output region's top-left corner).
                          vec2f output_pos,
                          float angle,
                          vec2f vel,
                          float avel) {
  Node *node = &sim[r][c];
  ActivateNode(*node);

  CHECK(input_idx >= 0 && input_idx < node->level->inputs.size());

  LevelBody body = one ? Levels::One() : Levels::Zero();

  // Transform relative output_pos to the input region's coordinate space.
  // The input region overlaps the bottom of the taller output region.
  body.pos.x = output_pos.x + node->level->inputs[input_idx] *
    Levels::BLOCK_SIZE;
  body.pos.y = output_pos.y - (Levels::OUT_HEIGHT - Levels::IN_HEIGHT) *
    Levels::BLOCK_SIZE + Levels::IN_Y * Levels::BLOCK_SIZE;
  body.angle = angle;
  body.vel = vel;
  body.avel = avel;

  // Scene objects link back to level bodies with the user_data.
  const uint64_t body_idx = node->level->bodies.size();
  node->level->bodies.push_back(std::move(body));

  Levels::AddBodyToScene(node->scene.get(),
                         node->level->bodies.back(),
                         {body_idx});

  // The newly created body is the last one in the scene's simulated objects.
  node->items.push_back(static_cast<int>(node->scene->objects.size() - 1));

  if (!node->in_queue) {
    node->in_queue = true;
    active_nodes.push_back({r, c});
  }
}

// Takes an index into the items vector and removes it. Marks as
// deleted the corresponding Obj from the scene, and LevelBody from the level.
void CircuitSim::DeleteItem(Node *node, int item_idx) {
  CHECK(item_idx >= 0 && item_idx < node->items.size());
  const int obj_idx = node->items[item_idx];
  const uint64_t body_idx = node->scene->objects[obj_idx].user_data.value();

  node->scene->Detach(obj_idx);
  node->level->bodies[body_idx].deleted = true;

  node->items.erase(node->items.begin() + item_idx);
}

// Wires in the circuit connect outputs of row r sequentially to inputs of
// row r + 1. Given a node's column and its local output index, this returns
// the column of the connected node in the next row and its local input index.
std::pair<size_t, int> CircuitSim::FindMatchingInput(
    size_t r, size_t c, int local_out_idx) const {
  CHECK(r + 1 < sim.size());
  CHECK(c < sim[r].size());
  const Node &node = sim[r][c];
  CHECK(local_out_idx >= 0 &&
        (size_t)local_out_idx < node.matching_inputs.size());

  std::pair<size_t, int> match = node.matching_inputs[local_out_idx];
  CHECK(match.first != (size_t)-1);
  return match;
}

// Returns the index of the output that the item's center is inside,
// if any.
std::optional<int> CircuitSim::ItemInsideOutput(Node *node, int item_idx) {
  CHECK(node != nullptr);
  CHECK(node->scene != nullptr);
  CHECK(node->level != nullptr);
  CHECK(item_idx >= 0 && item_idx < node->items.size());

  const int obj_idx = node->items[item_idx];
  const Scene::Obj &obj = node->scene->objects[obj_idx];
  vec2f pos = node->scene->GetPosition(obj);

  const float out_y = Levels::OUT_Y * Levels::BLOCK_SIZE;
  const float out_w = Levels::OUT_WIDTH * Levels::BLOCK_SIZE;
  const float out_h = Levels::OUT_HEIGHT * Levels::BLOCK_SIZE;

  for (int output_idx = 0; output_idx < node->level->outputs.size();
       output_idx++) {
    float out_x = node->level->outputs[output_idx] * Levels::BLOCK_SIZE;
    if (pos.x >= out_x && pos.x <= out_x + out_w &&
        pos.y >= out_y && pos.y <= out_y + out_h) {
      return {output_idx};
    }
  }

  return std::nullopt;
}

void CircuitSim::StepSimulation() {
  ticks++;

  size_t queue_size = active_nodes.size();
  for (size_t k = 0; k < queue_size; k++) {
    auto [r, c] = active_nodes.front();
    active_nodes.pop_front();
    Node *node = &sim[r][c];

    if (node->scene == nullptr) {
      node->in_queue = false;
      continue;
    }

    if (node->scene->Hibernating()) {
      if (!node->scene->Unhibernate()) {
        active_nodes.push_back({r, c});
        continue;
      }
    }

    if (!node->scene->AllAsleep()) {
      node->scene->Update();

      // Look to see if any item has entered an output region.
      // If so, we should remove it from the node, and insert
      // a new bit (AddInput) into the connected node's input.
      for (size_t i = 0; i < node->items.size(); ) {
        std::optional<int> out_idx = ItemInsideOutput(node, i);
        if (out_idx.has_value()) {
          int item_idx = i;
          int obj_idx = node->items[item_idx];
          const Scene::Obj &obj = node->scene->objects[obj_idx];

          bool is_one =
            node->level->bodies[obj.user_data.value()].item.value() ==
            LevelItem::ONE;
          vec2f pos = node->scene->GetPosition(obj);
          vec2f vel = node->scene->GetVelocity(obj);
          float angle = node->scene->GetAngle(obj);
          float avel = node->scene->GetAngularVelocity(obj);

          float out_x = node->level->outputs[out_idx.value()] *
            Levels::BLOCK_SIZE;
          float out_y = Levels::OUT_Y * Levels::BLOCK_SIZE;
          vec2f rel_pos = {pos.x - out_x, pos.y - out_y};

          DeleteItem(node, item_idx);

          if (r + 1 < sim.size()) {
            std::pair<size_t, int> next_in =
                FindMatchingInput(r, c, out_idx.value());
            AddInput(r + 1, next_in.first, next_in.second, is_one,
                     rel_pos, angle, vel, avel);
          }
        } else {
          i++;
        }
      }
    }

    if (node->scene->AllAsleep()) {
      node->scene->Hibernate();
      node->in_queue = false;
    } else {
      active_nodes.push_back({r, c});
    }
  }
}

void CircuitSim::FillVisibleTriangles(std::vector<Rendering::Triangle> *tri) {
  // Cutoff in pixels (assuming 100 pixels per world unit) for level-of-detail.
  // When a cell's drawn width is smaller than this, it is rendered as a single
  // rectangle of its average color.
  static constexpr float LOD_CUTOFF_PIXELS = 1.0f;

  // We assume that the geometry in a scene does not extend more than 5%
  // of the scene width.
  static constexpr float MAX_MARGIN = Scene::WIDTH * 0.05;
  if (sim.empty()) return;

  vec2f vmin = view_pos;
  vec2f vmax = ViewPosMax();

  static_assert(Levels::OUT_HEIGHT == Levels::IN_HEIGHT + 1);
  // We include one block of the output chute, since it is taller than
  // the input chute.
  static constexpr int ROW_HEIGHT_BLOCKS = Levels::OUT_Y + 1;
  static constexpr float ROW_HEIGHT = ROW_HEIGHT_BLOCKS * Levels::BLOCK_SIZE;

  int low_r = 0, high_r = sim.size();
  while (low_r < high_r) {
    int mid = low_r + (high_r - low_r) / 2;
    float render_y = (float)(mid) * ROW_HEIGHT;
    // Skip rows that are entirely above the top of the viewport (vmin.y).
    if (render_y + ROW_HEIGHT + MAX_MARGIN < vmin.y) {
      low_r = mid + 1;
    } else {
      high_r = mid;
    }
  }

  for (size_t r = low_r; r < sim.size(); r++) {
    float render_y = (float)(r) * ROW_HEIGHT;
    // If this row is entirely below the bottom of the viewport (vmax.y), stop.
    if (render_y - MAX_MARGIN > vmax.y) break;

    std::vector<Node> &row = sim[r];
    if (row.empty()) continue;

    int low_c = 0, high_c = row.size();
    while (low_c < high_c) {
      int mid = low_c + (high_c - low_c) / 2;
      float node_max_x = row[mid].xpos * Levels::BLOCK_SIZE +
                         Scene::WIDTH + MAX_MARGIN;
      if (node_max_x < vmin.x) {
        low_c = mid + 1;
      } else {
        high_c = mid;
      }
    }

    for (size_t c = low_c; c < row.size(); c++) {
      Node &node = row[c];
      float node_min_x = node.xpos * Levels::BLOCK_SIZE - MAX_MARGIN;
      if (node_min_x > vmax.x) break;

      float cell_width =
          library.GetInfo(node.cell).block_width * Levels::BLOCK_SIZE;

      if (cell_width * view_zoom * 100.0f < LOD_CUTOFF_PIXELS) {
        if ((node.lod_color & 255) != 0) {
          vec2f offset = {
            .x = node.xpos * Levels::BLOCK_SIZE,
            .y = render_y,
          };

          uint32_t c = node.lod_color;
          // Treat not-yet activated nodes as hibernating.
          bool hibernating = node.scene.get() == nullptr ||
            node.scene->Hibernating();
          if (hibernating) {
            c = ColorUtil::Composite32(0x00000055, c);
          }

          Rendering::Triangle t1, t2;
          t1.rgba = c;
          t2.rgba = c;

          t1.a = offset;
          t1.b = offset + vec2f{cell_width, 0.0f};
          t1.c = offset + vec2f{cell_width, ROW_HEIGHT};

          t2.a = offset;
          t2.b = offset + vec2f{cell_width, ROW_HEIGHT};
          t2.c = offset + vec2f{0.0f, ROW_HEIGHT};

          tri->push_back(t1);
          tri->push_back(t2);
        }
        continue;
      }

      // Need to activate the node to draw it with detail.
      ActivateNode(node);
      if (node.scene != nullptr) {
        vec2f offset = {
          .x = node.xpos * Levels::BLOCK_SIZE,
          .y = render_y,
        };

        bool hibernating = node.scene->Hibernating();

        for (Rendering::Triangle t : node.scene->GetTriangles()) {
          if (hibernating) {
            t.rgba = ColorUtil::Composite32(0x00000055, t.rgba);
          }
          t.a += offset;
          t.b += offset;
          t.c += offset;
          tri->push_back(t);
        }
      }
    }
  }
}

void CircuitSim::InjectRandomAssignment() {
  if (sim.empty() || sim[0].empty()) return;

  // Create a random assignment of variables. We must be consistent
  // about a variable's value!
  std::unordered_map<int, bool> assignment;
  for (const auto &[var_id, type] : layout.input_vars) {
    if (!assignment.contains(var_id)) {
      assignment[var_id] = rc.Byte() & 1;
    }
  }

  size_t global_in_idx = 0;
  for (size_t c = 0; c < sim[0].size(); c++) {
    Node &node = sim[0][c];
    int node_inputs = GateArity(node.cell.gate).first;

    for (int i = 0; i < node_inputs; i++) {
      if (global_in_idx >= layout.input_vars.size()) {
        break;
      }

      const auto &[var_id, type] = layout.input_vars[global_in_idx];
      bool val = assignment[var_id];

      bool insert = false;
      bool is_one = false;

      if (type == CType::MIXED) {
        insert = true;
        is_one = val;

      } else if (type == CType::ONE) {
        if (val) {
          insert = true;
          is_one = true;
        }

      } else if (type == CType::ZERO) {
        if (!val) {
          insert = true;
          is_one = false;
        }
      }

      if (insert) {
        vec2f pos = {
          .x = Levels::IN_WIDTH * 0.5f * Levels::BLOCK_SIZE,
          .y = (Levels::IN_HEIGHT * 0.5f + Levels::OUT_HEIGHT -
                Levels::IN_HEIGHT) * Levels::BLOCK_SIZE,
        };
        vec2f vel = {(float)(RandDouble(&rc) * 2.0 - 1.0),
                     (float)(RandDouble(&rc) * 2.0 - 1.0)};
        float avel = (float)(RandDouble(&rc) * 4.0 - 2.0);
        AddInput(0, c, i, is_one, pos, 0.0f, vel, avel);
      }

      global_in_idx++;
    }
  }
}

std::optional<CircuitSim::NodeLocation> CircuitSim::GetNodeAt(vec2f pos) const {
  if (pos.y < 0.0f || pos.x < 0.0f) {
    return std::nullopt;
  }

  static constexpr int ROW_HEIGHT_BLOCKS = Levels::OUT_Y + 1;
  static constexpr float ROW_HEIGHT = ROW_HEIGHT_BLOCKS * Levels::BLOCK_SIZE;

  size_t r = (size_t)(pos.y / ROW_HEIGHT);
  if (r >= sim.size()) {
    return std::nullopt;
  }

  int block_x = (int)(pos.x / Levels::BLOCK_SIZE);

  const std::vector<Node> &row = sim[r];
  for (size_t c = 0; c < row.size(); c++) {
    const Node &node = row[c];
    int width = library.GetInfo(node.cell).block_width;
    if (block_x >= node.xpos && block_x < node.xpos + width) {
      return NodeLocation{r, c, &node};
    }
  }
  return std::nullopt;
}

Layout CircuitSim::ExtractOverlapping(vec2f aabb_min, vec2f aabb_max) const {
  Layout result;
  if (sim.empty()) return result;

  static constexpr int ROW_HEIGHT_BLOCKS = Levels::OUT_Y + 1;
  static constexpr float ROW_HEIGHT = ROW_HEIGHT_BLOCKS * Levels::BLOCK_SIZE;

  int min_r = aabb_min.y < 0.0f ? 0 : (int)(aabb_min.y / ROW_HEIGHT);
  int max_r = aabb_max.y < 0.0f ? -1 : (int)(aabb_max.y / ROW_HEIGHT);
  if (max_r >= (int)sim.size()) max_r = (int)sim.size() - 1;
  if (min_r > max_r || min_r >= (int)sim.size()) return Layout();

  std::vector<std::vector<bool>> kept(sim.size());
  for (size_t r = 0; r < sim.size(); r++) {
    kept[r].resize(sim[r].size(), false);
  }

  // Identify cells that are initially inside the bounding box.
  for (int r = min_r; r <= max_r; r++) {
    for (size_t c = 0; c < sim[r].size(); c++) {
      const Node &node = sim[r][c];
      int width = library.GetInfo(node.cell).block_width;
      float node_min_x = node.xpos * Levels::BLOCK_SIZE;
      float node_max_x = (node.xpos + width) * Levels::BLOCK_SIZE;
      if (node_min_x >= aabb_max.x) {
        break;
      }
      if (node_max_x > aabb_min.x) {
        kept[r][c] = true;
      }
    }
  }

  struct ExtraWire {
    int x;
    Cell cell;
  };
  std::vector<std::vector<ExtraWire>> extra_wires(sim.size());

  // Trace broken connections to the top/bottom of the bounding box.
  int start_r = std::max(0, min_r - 1);
  for (int r = start_r; r <= max_r; r++) {
    if (r + 1 >= (int)sim.size()) continue;

    for (size_t c = 0; c < sim[r].size(); c++) {
      const Node &src = sim[r][c];
      bool src_kept = (r >= min_r && r <= max_r && kept[r][c]);

      for (int out_idx = 0; out_idx < (int)src.matching_inputs.size(); out_idx++) {
        auto match = src.matching_inputs[out_idx];
        if (match.first == (size_t)-1) continue;

        bool dst_kept = (r + 1 >= min_r && r + 1 <= max_r && kept[r + 1][match.first]);

        if (src_kept && !dst_kept) {
          // Connection goes out of bounds downwards.
          const auto &info = library.GetInfo(src.cell);
          CType type = info.outputs[out_idx].type;
          Gate wire_gate = Gate::WIREA;
          if (type == CType::ZERO) wire_gate = Gate::WIRE0A;
          else if (type == CType::ONE) wire_gate = Gate::WIRE1A;
          Cell wire_cell(wire_gate, 0);
          const auto &wire_info = library.GetInfo(wire_cell);

          int current_x = src.xpos + info.outputs[out_idx].xblock;
          for (int k = r + 1; k <= max_r; k++) {
            int cell_x = current_x - wire_info.inputs[0].xblock;
            extra_wires[k].push_back({cell_x, wire_cell});
            current_x = cell_x + wire_info.outputs[0].xblock;
          }
        } else if (!src_kept && dst_kept) {
          // Connection comes from out of bounds upwards.
          const Node &dst = sim[r + 1][match.first];
          const auto &info = library.GetInfo(dst.cell);
          CType type = info.inputs[match.second].type;
          Gate wire_gate = Gate::WIREA;
          if (type == CType::ZERO) wire_gate = Gate::WIRE0A;
          else if (type == CType::ONE) wire_gate = Gate::WIRE1A;
          Cell wire_cell(wire_gate, 0);
          const auto &wire_info = library.GetInfo(wire_cell);

          int current_x = dst.xpos + info.inputs[match.second].xblock;
          for (int k = r; k >= min_r; k--) {
            int cell_x = current_x - wire_info.outputs[0].xblock;
            extra_wires[k].push_back({cell_x, wire_cell});
            current_x = cell_x + wire_info.inputs[0].xblock;
          }
        }
      }
    }
  }

  struct Item {
    int xpos;
    Cell cell;
    bool operator<(const Item &other) const {
      return xpos < other.xpos;
    }
  };

  // Filter out empty rows at the extremes.
  int actual_min_r = -1;
  int actual_max_r = -1;
  for (int r = min_r; r <= max_r; r++) {
    bool has_any = !extra_wires[r].empty();
    for (size_t c = 0; c < sim[r].size(); c++) {
      if (kept[r][c]) has_any = true;
    }
    if (has_any) {
      if (actual_min_r == -1) actual_min_r = r;
      actual_max_r = r;
    }
  }

  if (actual_min_r == -1) {
    return Layout();
  }

  for (int r = actual_min_r; r <= actual_max_r; r++) {
    std::vector<Item> items;
    for (size_t c = 0; c < sim[r].size(); c++) {
      if (kept[r][c]) {
        items.push_back({sim[r][c].xpos, sim[r][c].cell});
      }
    }
    for (const auto &ew : extra_wires[r]) {
      items.push_back({ew.x, ew.cell});
    }
    std::sort(items.begin(), items.end());

    Layer layer;
    int current_x = 0;
    for (const Item &item : items) {
      int x = std::max(item.xpos, current_x);
      if (x > current_x) {
        layer.push_back(CellLibrary::Spacer(x - current_x));
      }
      layer.push_back(item.cell);
      current_x = x + library.GetInfo(item.cell).block_width;
    }
    result.circuit.layers.push_back(std::move(layer));
  }

  int next_var_id = 0;
  for (const Cell &cell : result.circuit.layers.front()) {
    std::vector<CType> types = CellInputTypes(cell);
    for (CType t : types) {
      result.input_vars.push_back({next_var_id++, t});
    }
  }

  return LayoutEngine::Normalize(std::move(result));
}
