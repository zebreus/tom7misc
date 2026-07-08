#include "circuit-sim.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "circuit.h"
#include "drc.h"
#include "initialization.h"
#include "inputs.h"
#include "layout.h"
#include "level.h"
#include "periodically.h"
#include "randutil.h"
#include "rendering.h"
#include "scene.h"
#include "sdl-rendering.h"
#include "status-bar.h"
#include "timer.h"
#include "toward-util.h"
#include "utf8.h"
#include "util.h"

vec2f CircuitSim::ViewPosMax() const {
  return view_pos + vec2f{
    .x = Scene::WIDTH / view_zoom,
    .y = Scene::HEIGHT / view_zoom,
  };
}

vec2f CircuitSim::ScreenToWorld(int x, int y) const {
  vec2f bottom_right = ViewPosMax();
  return rendering->CartesianPixel(view_pos, bottom_right, x, y);
}

// Ensure that the node is active, lazily loading if needed.
void CircuitSim::ActivateNode(Node &node) {
  if (node.level.get() == nullptr) {
    node.level = library.GetLevel(node.cell);
    Levels::AddChutes(node.level.get(), 0x339933FF, 0x993333FF);
  }
  CHECK(node.level.get());

  if (node.scene.get() == nullptr) {
    node.scene = Levels::CreateScene(*node.level);

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
                       Inputs *inputs,
                       Rendering *rendering,
                       std::string_view layout_file) :
  library(library),
  inputs(inputs),
  rendering(rendering),
  rc("sim") {
  std::string content = Util::ReadFile(layout_file);
  CHECK(!content.empty()) << layout_file << " missing?";

  std::optional<Layout> olay = LayoutEngine::Parse(content);
  CHECK(olay.has_value()) << "Could not parse " << layout_file;
  layout = std::move(olay.value());

  // Maybe option to skip this in slideshow mode?
  DRC::CheckLayout(library, layout_file, layout);

  if (!sim.empty() && !sim[0].empty()) {
    view_pos.x = sim[0][0].xpos * Levels::BLOCK_SIZE;
    view_pos.y = 0.0f;
    view_zoom = 1.0f;
  }
}

void CircuitSim::Reset() {
  sim.clear();
  ticks = 0;

  int64_t nodes = 0;
  for (const Layer &layer : layout.circuit.layers) {
    std::vector<Node> row;
    int xpos = 0;
    for (const Cell &cell : layer) {
      int width = library.GetInfo(cell).block_width;
      if (cell.gate != Gate::SPACER) {
        Node node{
          .xpos = xpos,
          .cell = cell,
        };
        row.push_back(std::move(node));
        nodes++;
      }
      xpos += width;
    }
    sim.push_back(std::move(row));
  }
}


// Insert a bit body into one of the node's inputs.
void CircuitSim::AddInput(Node *node,
                          int input_idx,
                          bool one,
                          // Position of the body (relative to the output region's
                          // top-left corner).
                          vec2f output_pos,
                          float angle,
                          vec2f vel,
                          float avel) {
  CHECK(node != nullptr);
  ActivateNode(*node);

  CHECK(input_idx >= 0 && input_idx < node->level->inputs.size());

  LevelBody body = one ? Levels::One() : Levels::Zero();

  // Transform relative output_pos to the input region's coordinate space.
  body.pos.x = output_pos.x + node->level->inputs[input_idx] * Levels::BLOCK_SIZE;
  body.pos.y = output_pos.y + Levels::IN_Y * Levels::BLOCK_SIZE;
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
// PERF: We could do this once at load time.
std::optional<std::pair<size_t, int>> CircuitSim::FindMatchingInput(
    size_t r, size_t c, int local_out_idx) const {
  if (r + 1 >= sim.size()) return std::nullopt;

  int global_out = local_out_idx;
  for (size_t prev_c = 0; prev_c < c; prev_c++) {
    global_out += GateArity(sim[r][prev_c].cell.gate).second;
  }

  int input_sum = 0;
  for (size_t next_c = 0; next_c < sim[r + 1].size(); next_c++) {
    int node_inputs = GateArity(sim[r + 1][next_c].cell.gate).first;
    if (global_out < input_sum + node_inputs) {
      return std::pair{next_c, global_out - input_sum};
    }
    input_sum += node_inputs;
  }

  return std::nullopt;
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

  // TODO: Keep a queue of active nodes so that we don't have to
  // do a linear scan. Only activate them if they are on screen or
  // need to be simulated because a bit enters.
  //
  // TODO PERF: Can do a single row of the circuit in parallel. But
  // later rows depend on earlier ones (bits can pass downward).
  int running = 0;

  for (size_t r = 0; r < sim.size(); r++) {
    std::vector<Node> &row = sim[r];
    for (size_t c = 0; c < row.size(); c++) {
      Node &node = row[c];
      ActivateNode(node);

      if (!node.scene->AllAsleep()) {
        running++;
        node.scene->Update();

        // Look to see if any item has entered an output region.
        // If so, we should remove it from the node, and insert
        // a new bit (AddInput) into the connected node's input.
        for (size_t i = 0; i < node.items.size(); ) {
          std::optional<int> out_idx = ItemInsideOutput(&node, i);
          if (out_idx.has_value()) {
            int item_idx = i;
            int obj_idx = node.items[item_idx];
            const Scene::Obj &obj = node.scene->objects[obj_idx];

            bool is_one =
              node.level->bodies[obj.user_data.value()].item.value() == LevelItem::ONE;
            vec2f pos = node.scene->GetPosition(obj);
            vec2f vel = node.scene->GetVelocity(obj);
            float angle = node.scene->GetAngle(obj);
            float avel = node.scene->GetAngularVelocity(obj);

            float out_x = node.level->outputs[out_idx.value()] * Levels::BLOCK_SIZE;
            float out_y = Levels::OUT_Y * Levels::BLOCK_SIZE;
            vec2f rel_pos = {pos.x - out_x, pos.y - out_y};

            DeleteItem(&node, item_idx);

            std::optional<std::pair<size_t, int>> next_in =
                FindMatchingInput(r, c, out_idx.value());
            if (next_in.has_value()) {
              AddInput(&sim[r + 1][next_in->first], next_in->second, is_one,
                       rel_pos, angle, vel, avel);
            }
          } else {
            i++;
          }
        }
      }
    }
  }
}

void CircuitSim::FillVisibleTriangles(std::vector<Rendering::Triangle> *tri) {
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
    // Rendered triangles use cartesian coordinates. So as the
    // simulation row increases, render_y must decrese.
    float render_y = -(float)(mid) * ROW_HEIGHT;
    // Skip rows that are entirely above the top of the viewport (vmax.y)
    if (render_y - MAX_MARGIN > vmax.y) {
      low_r = mid + 1;
    } else {
      high_r = mid;
    }
  }

  for (size_t r = low_r; r < sim.size(); r++) {
    float render_y = -(float)(r) * ROW_HEIGHT;
    // If this row is entirely below the bottom of the viewport, stop.
    if (render_y + Scene::HEIGHT + MAX_MARGIN < vmin.y) break;

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

      ActivateNode(node);
      if (node.scene != nullptr) {
        vec2f offset = {
          .x = node.xpos * Levels::BLOCK_SIZE,
          .y = render_y,
        };

        for (Rendering::Triangle t : node.scene->GetTriangles()) {
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
          .y = Levels::IN_HEIGHT * 0.5f * Levels::BLOCK_SIZE,
        };
        vec2f vel = {(float)(RandDouble(&rc) * 2.0 - 1.0),
                     (float)(RandDouble(&rc) * 2.0 - 1.0)};
        float avel = (float)(RandDouble(&rc) * 4.0 - 2.0);
        AddInput(&node, i, is_one, pos, 0.0f, vel, avel);
      }

      global_in_idx++;
    }
  }
}

