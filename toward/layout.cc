
#include "layout.h"

#include <deque>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "cell-library.h"
#include "circuit.h"
#include "image.h"
#include "prop.h"

// Layout cell is a working representation, where we have
// a Cell (or perhaps an abstract cell) and the vector of
// input propositions for it.
struct LC {
  std::vector<Prop> inprops;
  Cell cell;
};

// Returns the flattened vector of variable ids if all
// of the inputs are variables; nullopt otherwise.
static std::optional<std::vector<int>>
AllVars(std::span<const LC> lcs) {
  std::vector<int> vars;
  for (const LC &lc : lcs) {
    for (const Prop &p : lc.inprops) {
      if (const Var *v = std::get_if<Var>(&p.p)) {
        vars.push_back(v->id);
      } else {
        return std::nullopt;
      }
    }
  }
  return {vars};
}

struct LayoutEngine {
  const World &world;
  const CellLibrary &library;

  int ItsOutputPos(const Cell &cell) const {
    const CellLibrary::Info info = library.GetInfo(cell);
    CHECK(info.outputs.size() == 1);
    return info.outputs[0].xblock;
  }

  // Given a top layer (annotated with the propositions it takes as
  // inputs), create a new layer that produces those layers and is
  // simpler. (Simpler as in some unspecified well-founded ordering
  // so that this process terminates.)
  std::vector<LC> AddLayer(std::span<const LC> last) {
    // For each proposition, there is a natural next gate that we want
    // to add (e.g. if the prop is A & B, we want to add an AND gate).
    // The best thing we can do is add a gate, since this shrinks our
    // propositions. However, we have to account for the physical
    // space taken up. If there is not enough space, we need to add
    // wires to slope adjacent inputs away from the gate.
    // For example, on the very first call, the last layer will
    // be a bunch of wires. Each is 12 blocks wide. In order to
    // put an AND gate, we need ~20 blocks to the right of its
    // output, and ~48 to its left. But that might encroach upon
    // space we need to insert gates next to that AND gate.
    //  - If we have space to do it without encroaching, we can just
    //    do it (the gates are all rectangular so this doesn't
    //    really make anything worse, although now we might have
    //    multiple inputs to deal with).
    //  - But if it would block off a nearby input, we need to
    //    do something like add a spring in this position. We know
    //    that the leftmost and rightmost gates always have space
    //    to slope outwards, so the tension can always be relieved.

    // The desired cells. These are in the correct order to match up
    // with the next layer, and have the global position that would
    // match their outputs to the next layer's inputs. But they
    // may be overlapping. No spacers here.
    std::vector<std::pair<int, LC>> desired;
    // Current position (left edge of the next cell in the last layer,
    // in blocks).
    int pos = 0;
    for (const LC &lc : last) {

      CellLibrary::Info info = library.GetInfo(lc.cell);
      CHECK(info.inputs.size() == lc.inprops.size());

      for (int i = 0; i < lc.inprops.size(); i++) {
        const Prop &prop = lc.inprops[i];
        const CellLibrary::IO &io = info.inputs[i];

        int input_pos = pos + io.xblock;

        if (const Var *v = std::get_if<Var>(&prop.p)) {
          if (io.type == CType::MIXED) {
            // This is what we ultimately want on the input
            // layer. So we just want to continue wiring it
            // upward.

            // (But I think we should not do this for internal
            // wires, since we can only do crossovers for
            // separated wires. If they interior, we should
            // split them?)

            Cell wire = CellLibrary::WireB(0, CType::MIXED);
            int xout = ItsOutputPos(wire);

            desired.emplace_back(
                input_pos - xout,
                LC{
                  .inprops = {prop},
                  .cell = wire,
                });

          } else {

            // TODO: Tricky case. We need the separated 0 and
            // 1 to be next to each other.

          }

        } else if (const Value *v = std::get_if<Value>(&prop.p)) {
          Cell cell(v->value ? CONST1 : CONST0);
          int xout = ItsOutputPos(cell);

          desired.emplace_back(
              input_pos - xout,
              LC{
                .inprops = {},
                .cell = cell,
              });
        }

      }

      pos += info.block_width;
    }


    // TODO: Take the desired gates and resolve them: Place the ones
    // that we can, or use wires to spread out if not possible.
    LOG(FATAL) << "Unimplemented";
  }

  #if 0
  // TODO: Make some kind of
  ImageRGBA DebugRender() {

  }
  #endif

  // We work bottom-up. The goal is to add layers so that we simplify
  // the inputs, until they're all variables.
  Layout Run(std::span<const Prop> props) {
    // All the layers, annotated with props. We'll add to the front
    // of this.
    std::deque<std::vector<LC>> layers;

    // First create a layer with just wires to get us started. This is
    // probably not necessary, but it makes it easier to reason about
    // the steady state and makes sure that we don't get an empty
    // circuit.

    {
      std::vector<LC> final_layer;
      for (int i = 0; i < props.size(); i++) {
        LC lc{
          .inprops = {props[i]},
          .cell = CellLibrary::WireB(0),
        };
        final_layer.push_back(lc);
      }
      layers.push_front(std::move(final_layer));
    }

    // HERE.
    // Repeatedly take the front of the layers, and simplify.
    for (;;) {
      CHECK(!layers.empty());
      const std::vector<LC> &last = layers.front();

      // If it's all variables, then we are done!
      if (std::optional<std::vector<int>> ovars = AllVars(last)) {
        Layout ret;
        ret.input_vars = std::move(ovars.value());
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

      // Otherwise...
      std::vector<LC> next = AddLayer(last);
      layers.push_front(std::move(next));
    }

  }

  // Args must outlast the engine.
  LayoutEngine(const World &world, const CellLibrary &library) :
    world(world), library(library) {}
};

Layout DoLayout(const World &world, std::span<const Prop> props) {
  CellLibrary library;
  LayoutEngine engine(world, library);
  return engine.Run(props);
}
