
#ifndef _TOWARD_CIRCUIT_SIM_H
#define _TOWARD_CIRCUIT_SIM_H

#include <cstdio>
#include <deque>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "arcfour.h"
#include "base/logging.h"
#include "inline-vector.h"
#include "cell-library.h"
#include "circuit.h"
#include "layout.h"
#include "level.h"
#include "rendering.h"
#include "scene.h"
#include "toward-util.h"

struct CircuitSim {

  enum NodeState {
    // No input has entered the cell yet
    WAITING,
    //
    FINISHED,
  };

  // Each node corresponds to a cell from the circuit, but we have
  // additional stuff for simulating and rendering it.
  struct Node {
    // True if this node is currently in the active_nodes queue.
    bool in_queue = false;

    // Position of the left edge, measured in blocks. All nodes
    // on this layer have the same y coordinate.
    int xpos = 0;
    // The abstract cell.
    Cell cell;

    // The level in its current state. This may be null
    // if we have not yet loaded it (e.g. for huge circuits we
    // might not load the levels until they're on-screen).
    std::unique_ptr<Level> level;

    // The corresponding scene. This may be null if we have
    // not yet started simulating this node (e.g. because we
    // haven't loaded the level yet). Might be hibernating.
    std::unique_ptr<Scene> scene;

    // The indices of objects in the level that are items (zero or
    // one bits). Their user_data fields should indicate level
    // items
    std::vector<int> items;

    // Precomputed table of connected inputs for each of the node's outputs.
    // matching_inputs[local_out_idx] contains the column and local input index
    // of the connected node in the next row.
    InlineVector<std::pair<size_t, int>> matching_inputs;

    // TODO: Stuff for telling whether the node is waiting, running,
    // or complete.
  };


  vec2f ViewPos() const { return view_pos; }
  vec2f ViewPosMax() const;
  // Using y-down coordinates.
  vec2f ScreenToWorld(int x, int y) const;
  // Simulation steps executed since reset.
  int64_t Ticks() const { return ticks; }

  void Pan(int x, int y, int dx, int dy);
  void Zoom(int x, int y, bool up);

  // Ensure that the node is active, lazily loading if needed.
  void ActivateNode(Node &node);

  CircuitSim(const CellLibrary &library,
             Rendering *rendering,
             std::string_view layout_file);

  // Or from an already-loaded Layout object.
  CircuitSim(const CellLibrary &library,
             Rendering *rendering,
             Layout layout);

  void Reset();
  void GoToTopLeftCell();

  // Insert a bit body into one of the node's inputs.
  void AddInput(size_t r, size_t c,
                int input_idx,
                bool one,
                // Position of the body (relative to the output region's
                // top-left corner).
                vec2f output_pos,
                float angle,
                vec2f vel,
                float avel);

  // Takes an index into the items vector and removes it. Marks as
  // deleted the corresponding Obj from the scene, and LevelBody from the level.
  void DeleteItem(Node *node, int item_idx);

  std::pair<size_t, int> FindMatchingInput(
      size_t r, size_t c, int local_out_idx) const;

  // Returns the index of the output that the item's center is inside,
  // if any.
  std::optional<int> ItemInsideOutput(Node *node, int item_idx);

  void StepSimulation();

  void FillVisibleTriangles(std::vector<Rendering::Triangle> *tri);

  void InjectRandomAssignment();

  struct NodeLocation {
    size_t layer;
    size_t col;
    const Node *node;
  };

  // Returns the node at the given world position, or std::nullopt if none.
  std::optional<NodeLocation> GetNodeAt(vec2f pos) const;

  // Extracts the cells that overlap an AABB provided in world coordinates,
  // returning them as a Layout object.
  Layout ExtractOverlapping(vec2f aabb_min, vec2f aabb_max) const;

  const std::vector<std::vector<Node>> &GetSim() const { return sim; }

 private:
  const CellLibrary &library;

  // Not owned.
  // Maybe should leave this up to the client...
  Rendering *rendering = nullptr;

  // World coordinates of the top left of the screen.
  // This code uses computer graphics coordinates (y down).
  vec2f view_pos = {0.0f, 0.0f};
  // When 1.0, this means the viewport is Scene::WIDTH x Scene::HEIGHT.
  // When 2.0, WIDTH/2 by HEIGHT/2.
  float view_zoom = 1.0f;

  // The original circuit we loaded.
  // We just keep this around so that we can rebuild the tree,
  // and deduce valid inputs.
  Layout layout;

  // The simulation. This has the same number of rows as the layout,
  // and a column for each of the layer's non-spacer cells.
  std::vector<std::vector<Node>> sim;

  // Nodes that are ready to execute or currently executing.
  std::deque<std::pair<size_t, size_t>> active_nodes;

  ArcFour rc;
  int64_t ticks = 0;
};

#endif
