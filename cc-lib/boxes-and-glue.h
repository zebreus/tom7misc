
#ifndef _CC_LIB_BOXES_AND_GLUE
#define _CC_LIB_BOXES_AND_GLUE

#include <vector>

// Abstract version of a boxes-and-glue algorithm.
//
// This is presented as the classic problem of placing words into
// lines with optinal line breaks. But it can be used for other stuff,
// like placing paragraphs into columns. Note that even when placing
// words on a line, we often have more than one box per word, to
// represent potential hyphenation points or kerning pairs.
//
// Note that this is probably not the same algorithm as Knuth's.
struct BoxesAndGlue {

  struct BoxIn {
    // Native width of the box, not counting any glue.
    double width = 0.0;
    // Additional penalty if we break here.
    double glue_break_penalty = 0.0;
    // If we break here, then this much extra width is used
    // (for insertion of a hyphen or something like that).
    double glue_break_extra_width = 0.0;
    // Ideal amount of glue after the box, e.g. the width
    // of a space glyph in the font (or a kerning pair).
    double glue_ideal = 0.0;

    // Coefficients used when applying glue in a line.
    // When a coefficient is higher, larger magnitude of space
    // (positive or negative) is apportioned here.
    double glue_expand = 1.0;
    double glue_contract = 1.0;

    // User data.
    void *data = nullptr;
  };

  struct BoxOut {
    // Points to one of the boxes in the input vector.
    const BoxIn *box = nullptr;

    // Populated by PackBoxes.
    // Did we break after this box?
    bool did_break = false;
    // Amount of glue assigned (on the right side). This does not
    // include width or glue_break_extra_width.
    double actual_glue = 0.0;
  };

  // For advanced uses, the boxes can actually be DAG-structured.
  // Any node with no parents is treated as a starting node.
  // Any node with no children is treated as an ending node.
  struct Edge {
    // Index (in the boxes_in vector) of the parent node.
    int parent_node = 0;
    // Index (in the boxes_out vector) of the child node; the one
    // coming after the parent in the sequence. In order to prevent
    // cycles, we require that parent_node < child_node.
    int child_node = 0;
    // Additional penalty when following this edge.
    double edge_penalty = 0.0;
  };

  // Return the vector of "lines," each with the vector of "words"
  // on that line.
  //
  // This is the same as PackBoxes with edges from each n to n+1,
  // and 0.0 penalty.
  static std::vector<std::vector<BoxOut>> PackBoxesLinear(
      double line_width,
      const std::vector<BoxIn> &boxes_in);

  static std::vector<std::vector<BoxOut>> PackBoxes(
      double line_width,
      const std::vector<BoxIn> &boxes_in,
      const std::vector<Edge> &edges);

  // Greedy algorithm, mostly useful for comparison purposes or
  // as a fallback (it's linear time).
  // Doesn't support DAG input.
  static std::vector<std::vector<BoxOut>> PackBoxesFirst(
      double line_width,
      const std::vector<BoxIn> &boxes_in,
      double max_break_penalty);

};

#endif
