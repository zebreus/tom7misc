
#ifndef _TOWARD_CIRCUIT_SIM_H
#define _TOWARD_CIRCUIT_SIM_H

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
    // haven't loaded the level yet).
    std::unique_ptr<Scene> scene;

    // The indices of objects in the level that are items (zero or
    // one bits). Their user_data fields should indicate level
    // items
    std::vector<int> items;

    // TODO: Stuff for telling whether the node is waiting, running,
    // or complete.
  };


  vec2f ViewPosMax() const;
  vec2f ScreenToWorld(int x, int y) const;
  // Simulation steps executed since reset.
  int64_t Ticks() const;

  // Ensure that the node is active, lazily loading if needed.
  void ActivateNode(Node &node);

  CircuitSim(const CellLibrary &library,
             Inputs *inputs,
             Rendering *rendering,
             std::string_view layout_file);

  void Reset();

  // Insert a bit body into one of the node's inputs.
  void AddInput(Node *node,
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

  std::optional<std::pair<size_t, int>> FindMatchingInput(
      size_t r, size_t c, int local_out_idx) const;

  // Returns the index of the output that the item's center is inside,
  // if any.
  std::optional<int> ItemInsideOutput(Node *node, int item_idx);

  void StepSimulation();

  void FillVisibleTriangles(std::vector<Rendering::Triangle> *tri);

  void InjectRandomAssignment();

 private:
    const CellLibrary &library;

  // Not owned.
  // Maybe should leave this up to the client...
  Inputs *inputs = nullptr;
  Rendering *rendering = nullptr;

  // World coordinates of the top left of the screen.
  // This code uses computer graphics coordinates (y down) except for
  // the rendered triangles.
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

  ArcFour rc;
  int64_t ticks = 0;
};

#endif
