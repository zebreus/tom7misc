
#include "layout.h"

#include <deque>
#include <span>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "cell-library.h"
#include "circuit.h"
#include "prop.h"

// Layout cell is a working representation, where we have
// a Cell (or perhaps an abstract cell) and the vector of
// input propositions for it.
struct LC {
  std::vector<Prop> inprops;
  Cell cell;
};

// We work bottom-up. The goal is to add layers so that we simplify
// the inputs, until they're all variables.
Layout DoLayout(const World &world, std::span<const Prop> props) {
  // All the layers, annotated with props. We'll add to the front
  // of this.
  std::deque<std::vector<LC>> layers;

  // First create a layer with just wires to get us started. This
  // is probably not necessary, but it makes it easier to reason
  // about and makes sure that we don't get an empty circuit.

  {
    std::vector<LC> last_layer;
    for (int i = 0; i < props.size(); i++) {
      LC lc{
        .inprops = {props[i]},
        .cell = CellLibrary::WireB(0),
      };
      last_layer.push_back(lc);
    }
    layers.push_front(std::move(last_layer));
  }

  // HERE.
  // Repeatedly take the front of the layers, and simplify.
  LOG(FATAL) << "Unimplemented";

  Layout ret;
  LOG(FATAL) << "Unimplemented: input_vars";
  ret.circuit.layers.reserve(layers.size());
  for (std::vector<LC> &layout_layer : layers) {
    std::vector<Cell> layer;
    layer.reserve(layout_layer.size());
    for (LC &lc : layout_layer) {
      layer.push_back(std::move(lc.cell));
    }
    ret.circuit.layers.push_back(std::move(layer));
  }
  return ret;
}

