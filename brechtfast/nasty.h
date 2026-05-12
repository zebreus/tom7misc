
#ifndef _BRECHTFAST_NASTY_H
#define _BRECHTFAST_NASTY_H

#include <optional>
#include <string_view>

#include "geom/polyhedra.h"

struct Nasty {
  // A very tall pyramid with a decagon for its base, and the
  // tip not centered.
  static Polyhedron TiltedDecagonPyramid();

  // Like the previous, but much more squat. This makes it
  // possible for fans of the triangles to self-intersect.
  static Polyhedron SquatSnail();

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

  // Long topological diameter between the top and bottom faces.
  static Polyhedron Cigar();

  // A 3x3x3 "Rubik's cube", slighly distorted to make it
  // strictly convex.
  static Polyhedron RubiksCube();

  static std::optional<Polyhedron> ByName(std::string_view name);
};

#endif

