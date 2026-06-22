
// Lay out components to form a circuit.

// The circuit is a series of layers; each one computes a
// cval (chute value) for some propositions at some x
// locations. A chute value can be MIXED, which means that
// it contains a '0' glyph or a '1' glyph indicating the
// value of the proposition. It can also be ZERO, meaning
// that it contains a '0' glyph only if the proposition
// is false. Or it can be ONE, meaning that it contains
// a '1' glyph only if the proposition is true. These last
// two cases are called "separated" values; usually we
// have a pair of outputs (ZERO, ONE) for a proposition
// when working with separated values.

enum cval {
  ZERO,
  ONE,
  MIXED,
};

