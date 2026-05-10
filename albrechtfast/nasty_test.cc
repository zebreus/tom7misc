
#include "nasty.h"

#include "ansi.h"
#include "poly-util.h"


static void WriteSTLs() {
  SaveAsSTL(Nasty::TiltedDecagonPyramid(), "tilteddecagonpyramid.stl");
  SaveAsSTL(Nasty::FlattenedIcosahedron(), "flattenedicosahedron.stl");
  SaveAsSTL(Nasty::LongTaperedPrism(), "longtaperedprism.stl");
  SaveAsSTL(Nasty::LongTaperedAntiprism(), "longtaperedantiprism.stl");
  SaveAsSTL(Nasty::Lens(), "lens.stl");
  SaveAsSTL(Nasty::LowPolyLens(), "lowpolylens.stl");
  SaveAsSTL(Nasty::Coin(), "coin.stl");
  SaveAsSTL(Nasty::Sawblade(), "sawblade.stl");
  SaveAsSTL(Nasty::Dome(), "dome.stl");
  SaveAsSTL(Nasty::Chisel(), "chisel.stl");
  SaveAsSTL(Nasty::Cigar(), "cigar.stl");
}


int main(int argc, char **argv) {
  ANSI::Init();

  WriteSTLs();

  return 0;
}


