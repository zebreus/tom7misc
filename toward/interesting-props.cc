
#include "interesting-props.h"
#include "prop.h"
#include <vector>

const std::vector<Prop> &SmallInterestingProps() {
  static std::vector<Prop> *p = []{
      Prop p0 = Prop{.p = Var{.id = 0}};
      Prop p1 = Prop{.p = Var{.id = 1}};
      Prop p2 = Prop{.p = Var{.id = 2}};
      Prop p3 = Prop{.p = Var{.id = 3}};

      return new std::vector<Prop>{
        True(),
        False(),
        p0,
        -p0,
        p0 | p1,
        p0 & p1,
        p0 ^ p1,
        (p0 | p1) ^ p2,
        -(p0 | (p1 & p2)),
        p0 & p1 & p2 & p3,
        p0 | p1 | p2 | p3,
        p0 ^ p1 ^ p2 ^ p3,
        (p0 & p1) | (p2 & p3),
        p0 | False(),
        p0 & True(),
        p0 ^ p0,
        -(-p0),
      };
    }();

  return *p;
}

const std::vector<Prop> &MediumInterestingProps() {
  static std::vector<Prop> *p = []{
      Prop a = Prop{.p = Var{.id = 0}};
      Prop b = Prop{.p = Var{.id = 1}};
      Prop c = Prop{.p = Var{.id = 2}};
      Prop d = Prop{.p = Var{.id = 3}};
      Prop e = Prop{.p = Var{.id = 4}};
      Prop f = Prop{.p = Var{.id = 5}};
      Prop g = Prop{.p = Var{.id = 6}};
      Prop h = Prop{.p = Var{.id = 7}};
      Prop i = Prop{.p = Var{.id = 8}};
      Prop j = Prop{.p = Var{.id = 9}};

      return new std::vector<Prop>{
        a & b & c & d & e & f,
        (a & b & c & d & e & f) |
        (-a & -b & (d | e) & -f),
        a | b | c | d | e | f,
        a ^ b ^ c ^ d ^ e ^ f,
        (a & b & c) | (d & e & f),
        (a | b | c) & (d | e | f),
        ((a & b) | (c & d)) ^ (e | f),
        -(a | b) & (c ^ d) | (e & -f),
        // At most one set.
        -(a & b & c & d & e) |
        -(a & b & c & d & f) |
        -(a & b & c & e & f) |
        -(a & b & d & e & f) |
        -(a & c & d & e & f) |
        -(b & c & d & e & f),
        // At most one set, but scrambled.
        -(a & c & b & e & d) |
        -(b & c & a & d & f) |
        -(f & c & a & b & e) |
        -(e & b & f & d & a) |
        -(d & f & c & a & e) |
        -(b & e & d & f & c),

        // kid chess props
        (a & (b & c)) |
        ((b & (d | e)) & ((f | g) | ((h | i) | (c | j)))),
      };
    }();

  return *p;
}
