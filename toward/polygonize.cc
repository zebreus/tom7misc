
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "toward-util.h"
#include "yocto-math.h"
#include "base/print.h"
#include "ansi.h"

static void PolygonizePointer() {
  Polygonization::Shape shape;
  shape.polys.push_back(Polygon{
    vec2(108, 36),
    vec2(108, 306.14),
    vec2(180, 234),
    vec2(252, 369),
    vec2(288, 351),
    vec2(216, 216),
    vec2(315, 216),
    });

  Polygonization::Mesh mesh = Polygonization::PolygonizeOrDie(
      shape, MAX_POLYGON_VERTICES);

  Print("Polygonization::Mesh{{\n"
        "  .vertices = {{\n");
  for (vec2 v : mesh.vertices) {
    Print("    vec2{{" "{:.8g}, {:.8g}" "}},\n",
          v.x, v.y);
  }
  Print("  }},\n"
        "  .polygons = {{\n");
  for (const std::vector<int> &p : mesh.polygons) {
    Print("    {{");
    for (int i : p) {
      Print("{}, ", i);
    }
    Print("}},\n");
  }
  Print("  }}\n"
        "}};\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  PolygonizePointer();

  return 0;
}
