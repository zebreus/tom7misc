
#include "layout.h"

#include <compare>
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
#include "vector-util.h"



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

  // What we want to do with a chute. This is thinking about the
  // bottom-up direction; "permute left" means a wire would slope
  // like a backslash.
  enum DesireType {
    INVALID,
    // Apply a gate to decompose the proposition.
    DECOMPOSE,
    // Apply a combiner so that we have separated inputs.
    UNCOMBINE,
    // Unduplicate adjacent identical propositions.
    UNDUP,
    // The chute is out of order and should swap to its left.
    EXCHANGE_LEFT,
    // ... or right.
    EXCHANGE_RIGHT,
    // The chute is in order, and should flow to a relative offset
    // of its current position (in desire_val).
    FLOW,
  };

  // Location and type of the transition between layers where
  // an input and output meet.
  struct Chute {
    int pos = 0;
    Prop prop = False();
    CType type = CType::MIXED;

    DesireType desire = DesireType::INVALID;
    int desire_val = 0;
  };

  // Flatten the inputs. Desires are not yet specified.
  std::vector<Chute> FlattenInputs(std::span<const LC> top) {
    std::vector<Chute> chutes;
    // Current position (left edge of the next cell in the top layer,
    // in blocks).
    int pos = 0;
    for (const LC &lc : top) {
      CellLibrary::Info info = library.GetInfo(lc.cell);
      CHECK(info.inputs.size() == lc.inprops.size());

      for (int i = 0; i < lc.inprops.size(); i++) {
        const Prop &prop = lc.inprops[i];
        const CellLibrary::IO &io = info.inputs[i];

        int input_pos = pos + io.xblock;
        chutes.emplace_back(Chute{
            .pos = input_pos,
            .prop = prop,
            .type = io.type,
            .desire = DesireType::INVALID,
            .desire_val = 0,
          });
      }
    }

    return chutes;
  }

  // Given a top layer (annotated with the propositions it takes as
  // inputs), create a new layer that produces those layers and is
  // simpler. (Simpler as in some unspecified well-founded ordering
  // so that this process terminates.) The input and output layers
  // should start at x=0.
  std::vector<LC> AddLayer(std::span<const LC> top) {
    CHECK(!top.empty()) << "Precondition.";

    // The hard part is the physical routing. Some gates like AND0110
    // need separated inputs in a specific form. Since we are working
    // bottom-up, the AND gate itself is not bad (we just place it
    // wherever we need a mixed A & B prop). But satisfying the
    // separated 0 A, 1 A, 1 B, and 0 B inputs of that then becomes
    // tricky.

    // |0A| |1A|  |1B| |0B|
    //   \           /
    //    \         /
    //     \       /
    //       |A&B|

    // It's possible to work directly with separated wires: We have
    // NOT0, NOT1 and these are simple and efficient. We could also
    // create a separated gate like AND0110_0, but we almost always
    // need a matching AND0110_1 (e.g. to satisfy the 0A and 1A inputs
    // of an AND gate itself), and so we would end up duplicating
    // input propositions.

    // So, a high priority is to put pairs of separated wires
    // next to each other so that we can join them with a separator.
    // (But we only need to do this when they represent binary
    // propositions; we can directly strip ¬,create constants,
    // and postpone variables until the end.) The tricky thing about
    // *this* is that we can only reorder inputs when they are separated.

    // First we flatten all of the inputs we need to satisfy on the
    // top layer, with their positions. These are just fixed and
    // independent since we aren't going to try to move them around.

    std::vector<Chute> chutes = FlattenInputs(top);

    // Now set the desires for each.
    // The first thing we need to do is deal with the order of the
    // chutes.

    // Exterior chutes that hold variables are done.
    std::vector<bool> done(chutes.size(), false);

    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (std::holds_alternative<Var>(chute.prop.p) &&
          chute.type == CType::MIXED) {
        // Still on the exterior.
        done[c] = true;
        chute.desire = DesireType::FLOW;
        // TODO: Adjust this later down once we know better how much
        // space we need.
        chute.desire_val = -8;
      } else {
        break;
      }
    }

    CHECK(!done.back()) << "Precondition: The top layer is already "
      "complete!";

    // Also the right side.
    for (int c = chutes.size() - 1; c >= 0; c--) {
      Chute &chute = chutes[c];
      if (std::holds_alternative<Var>(chute.prop.p) &&
          chute.type == CType::MIXED) {
        // Still on the exterior.
        done[c] = true;
        chute.desire = DesireType::FLOW;
        chute.desire_val = +8;
      } else {
        break;
      }
    }

    // TODO: exterior combined pairs of variables should get
    // unseparated!

    // Now any internal mixed variable is going to be problematic,
    // because we will need to cross over it to attain the order
    // we want. So unseparate those.

    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!done[c] &&
          chute.type == CType::MIXED &&
          std::holds_alternative<Var>(chute.prop.p)) {
        CHECK(chute.desire == DesireType::INVALID);
        chute.desire = UNCOMBINE;
      }
    }

    // chute indices (the first one) that we want to
    // undup.
    std::vector<int> undup_pairs;

    // TODO: We should UNDUP propositions that are equal and
    // already next to one another. We want to do this before
    // crossing over, because reducing the number of total
    // chutes is a big efficiency win.
    for (int c = 0; c < chutes.size() - 1; c++) {
      Chute &chute1 = chutes[c];
      Chute &chute2 = chutes[c + 1];
      // When we have two separated inputs for the same
      // proposition in a row, we should UNDUP them.
      if (!done[c] &&
          // Not if it already has a desire.
          chute1.desire == INVALID &&
          chute2.desire == INVALID &&
          // Must be same separated type.
          chute1.type != CType::MIXED &&
          chute1.type == chute2.type &&
          // And syntactically the same proposition.
          chute1.prop == chute2.prop) {
        chute1.desire = UNDUP;
        chute2.desire = UNDUP;
        undup_pairs.push_back(c);
      }
    }


    // Similarly, decomposing a mixed binary proposition gets us
    // separated inputs (which we want) as well as simplifying.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!done[c] &&
          chute.type == CType::MIXED &&
          std::holds_alternative<Binop>(chute.prop.p)) {
        CHECK(chute.desire == DesireType::INVALID);
        chute.desire = DECOMPOSE;
      }

      // TODO: We could use a mixed NOT here if we were
      // confident in it.
    }

    // Then we need to uncombine remaining mixed inputs, since we
    // don't have a way of dealing with them.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (!done[c] &&
          chute.type == CType::MIXED &&
          chute.desire == DesireType::INVALID) {
        chute.desire = UNCOMBINE;
      }
    }

    // Now if an input doesn't have a desire, put it in the
    // global order.

    for (int c = 0; c < chutes.size() - 1; c++) {
      Chute &chute1 = chutes[c];
      Chute &chute2 = chutes[c + 1];
      if (!done[c] &&
          chute1.desire == DesireType::INVALID &&
          chute2.desire == DesireType::INVALID) {
        // We should have dealt with this above!
        CHECK(chute1.type != CType::MIXED);
        CHECK(chute2.type != CType::MIXED);

        // Just do local swaps if out of order. We could be smarter
        // about this (e.g. by inspecting propositions that we know we
        // are decomposing and will need to deal with further up) but
        // it would probably be better to just make a better ordering.
        // (We don't even care what the ordering is, as long as it
        // puts equal props together so that we can dedup or uncombine
        // them).
        auto ord = chute1.prop <=> chute2.prop;

        if (// Props out of order?
            ord == std::strong_ordering::greater ||
            // Same prop but abnormal bit order?
            (ord == std::strong_ordering::equal &&
             chute1.type == CType::ONE &&
             chute2.type == CType::ZERO)) {
          chute1.desire = DesireType::EXCHANGE_RIGHT;
          chute2.desire = DesireType::EXCHANGE_LEFT;
        }
      }
    }

    // If no desire yet (e.g. already in order), just stay put.
    for (int c = 0; c < chutes.size(); c++) {
      Chute &chute = chutes[c];
      if (done[c]) {
        CHECK(chute.desire != DesireType::INVALID);
      } else if (chute.desire == DesireType::INVALID) {
        chute.desire = DesireType::FLOW;
      }
    }

    // TODO: Adjust the amount of flow for the exterior, with the
    // delta according to some estimate of how much space we need in
    // the middle (could be negative even).


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
    for (const LC &lc : top) {

      CellLibrary::Info info = library.GetInfo(lc.cell);
      CHECK(info.inputs.size() == lc.inprops.size());

      for (int i = 0; i < lc.inprops.size(); i++) {
        const Prop &prop = lc.inprops[i];
        const CellLibrary::IO &io = info.inputs[i];

        int input_pos = pos + io.xblock;

        if (const Var *v = std::get_if<Var>(&prop.p)) {
          (void)v;
          if (io.type == CType::MIXED) {
            // This is what we ultimately want on the input
            // layer. So we just want to continue wiring it
            // upward.

            // (But I think we should not do this for internal
            // wires, since we can only do crossovers for
            // separated wires. If they are interior, we should
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
            // 1 to be next to each other in order to use
            // the separator (or dupsep).


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

        } else if (const Unop *u = std::get_if<Unop>(&prop.p)) {
          CHECK(u->op == UnopOp::NOT);

          if (io.type == CType::MIXED) {
            // Use NOT01 for this, as it is more reliable.
            Cell cell(NOT01);
            int xout = ItsOutputPos(cell);

            desired.emplace_back(
                input_pos - xout,
                LC{
                  .inprops = {*u->a, *u->a},
                  .cell = cell,
                });

          } else {
            // Otherwise an arity-1 cell.
            Cell cell((io.type == CType::ONE) ? NOT1 : NOT0);
            int xout = ItsOutputPos(cell);
            desired.emplace_back(
                input_pos - xout,
                LC{
                  .inprops = {*u->a},
                  .cell = cell,
                });
          }

        } else if (const Binop *bop = std::get_if<Binop>(&prop.p)) {
          CHECK(bop->op == BinopOp::AND) << "Should have transformed "
            "this already to remove OR and XOR. We could have native "
            "support for those gates in the future, though.";

          if (io.type == CType::MIXED) {
            Cell cell(AND0110);
            int xout = ItsOutputPos(cell);
            desired.emplace_back(
                input_pos - xout,
                LC{
                  .inprops = {*bop->a, *bop->a, *bop->b, *bop->b},
                  .cell = cell,
                });
          } else {
            // Maybe we should be handling this as a general pass
            // earlier? Or we can have versions that only output
            // the particular bit.

            // We can do this by using a separator and sending the
            // other output to a sink, but in most situations we would
            // have the other separated half needed somewhere else in
            // the next layer. So we should avoid duplicating
            // propositions like that. Should we have an earlier pass
            // that puts equal props next to one another, and merges
            // them with separators/dups/etc.?


            // For now we defer; something else needs to separate
            // for us without wasting half of the proposition.
            Cell wire = CellLibrary::WireB(0, io.type);
            int xout = ItsOutputPos(wire);

            desired.emplace_back(
                input_pos - xout,
                LC{
                  .inprops = {prop},
                  .cell = wire,
                });
          }
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
  Layout Run(std::span<const Prop> props_in) {
    // I only support the AND binary gate today, so normalize to
    // a form that removes OR, XOR, etc.
    std::vector<Prop> props = VectorMap(props_in, NormalizeToAnd);

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
