
#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "cell-library.h"
#include "chess.h"
#include "chessprop.h"
#include "circuit-sim.h"
#include "layout.h"
#include "periodically.h"
#include "rendering.h"
#include "status-bar.h"
#include "timer.h"

static bool IsLayerActive(std::span<const CircuitSim::Node> layer) {
  for (const CircuitSim::Node &node : sim_nodes[r]) {
    if (node.level.get() == nullptr ||
        node.scene.get() == nullptr)

    }
  }
}


static void Benchmark(std::string_view layout_file) {
  CellLibrary library;
  std::unique_ptr<Rendering> rendering = CreateImageRendering("circuitbench");
  CircuitSim sim(library, rendering.get(), layout_file);

  Position start_pos;
  sim.InjectAssignment(ChessProp::AssignmentFromPosition(start_pos));

  const size_t total_nodes = [&]{
      size_t total_nodes = 0;
      for (const std::vector<CircuitSim::Node> &sim_nodes : sim.GetSim()) {
        total_nodes += sim_nodes.size();
      }
      return total_nodes;
    }();
  const size_t num_layers = sim.GetSim().size();

  Periodically status_per(1.0);
  StatusBar status(1);
  Timer run_timer;

  bool done = false;
  while (!done) {
    sim.StepSimulation();

    const std::vector<std::vector<CircuitSim::Node>> &sim_nodes = sim.GetSim();
    if (!sim.GetFinalOutputs().empty()) {
      done = true;
    }

    status_per.RunIf([&]() {
        size_t shallowest = 0;
        size_t deepest = 0;
        size_t active_count = 0;
        for (size_t r = 0; r < sim_nodes.size(); r++) {
          bool active = IsLayerActive(sim_nodes[r]);
          if (active) {
            deepest = std::max(deepest, r);
            active_count++;
          }
        }
        status.Status("{} ticks, {}/{} active nodes, layer {}-{}/{} [{}]",
                      sim.Ticks(), active_count, total_nodes,
                      shallowest, deepest, num_layers,
                      ANSI::Time(run_timer.Seconds()));
    });
  }

  status.Remove();
  Print("Simulation completed in {}, {} ticks.\n",
        ANSI::Time(run_timer.Seconds()), sim.Ticks());
}


int main(int argc, char* argv[]) {
  ANSI::Init();

  std::string layout_file = "one-legal-b7-h1.layout";
  if (argc >= 2) layout_file = argv[1];

  Benchmark(layout_file);

  return 0;
}
