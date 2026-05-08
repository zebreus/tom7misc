
#ifndef _BRECHTFAST_NASTY_H
#define _BRECHTFAST_NASTY_H

#include "geom/polyhedra.h"

struct Nasty {
  // A very tall pyramid with a decagon for its base, and the
  // tip not centered.
  static Polyhedron TiltedDecagonPyramid();

  // A regular icosahedron flattened 100:1 on the z axis.
  static Polyhedron FlattenedIcosahedron();

  static Polyhedron LongTaperedPrism();

  static Polyhedron LongTaperedAntiprism();

  // Two shallow geodesic caps joined at a sharp boundary.
  // (Too many faces!)
  static Polyhedron Lens();

  static Polyhedron LowPolyLens();

  // Very shallow 30-gon prism.
  static Polyhedron Coin();

  // Like coin, but an antiprism.
  static Polyhedron Sawblade();

  // Like lens, but just a hemisphere with a big flat base.
  static Polyhedron Dome();

  static Polyhedron Chisel();
};

#endif

