
#ifndef _CC_LIB_BOXES_AND_GLUE
#define _CC_LIB_BOXES_AND_GLUE

#include <vector>

// Abstract version of the boxes-and-glue algorithm.
//
// This is presented as the classic problem of
// placing words into lines, but it can be used for
// other stuff, like placing paragraphs into columns.
// Note that even when placing words on a line, we
// often have more than one box per word, to represent
// potential hyphenation points or kerning pairs.

struct BoxesAndGlue {

  struct BoxIn {
    // Native width of the box, not counting any glue.
    double width;
    // Additional penalty if we break here.
    double glue_break_penalty;
    // If we break here, then this much extra width is used
    // (for insertion of a hyphen or something like that).
    double glue_break_extra_width;
    // Ideal amount of glue after the box, e.g. the width
    // of a space glyph in the font (or a kerning pair).
    double glue_ideal;

    double glue_expand;
    double glue_contract;

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

  // Return the vector of "lines," each with the vector of "words"
  // on that line.
  static std::vector<std::vector<BoxOut>> PackBoxes(
      double line_width,
      const std::vector<BoxIn> &boxes_in);

};

#endif
