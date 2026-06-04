
#include "base/print.h"
#include "folding.h"

#include <optional>

#include "ansi.h"
#include "geom/polyhedra.h"
#include "yocto-math.h"

void UnfoldNamiki() {
  //
  //                                                        ,E
  //                                                 D    :~:
  //                                                 :~,:~:
  //                                                 :^>-.
  //                                              .:,./.-
  //                                            ,,. ,.-  :.
  //                                         .::  ,.   ,  :.
  //                                       ::   ,:.    ^   :.
  //                                    ,:.   .:.      ^    :.
  //                                  ,,.    ,.        .,    :.
  //                               .,:     ,.           ~     :
  //                             :-      ,:             ^      :
  //                          ,:.      .:.              ,.      -
  //                        :,.       :.                 :       -
  //                     ,,,        ,,                   ^        -
  //                   ,-         -:                     ,.        -
  //                ,:.         :-              .,:--:-^-B:.        :
  //              :,          -F:,,..,,:-:::,,--,,::,.     .:,      .:
  //           ,,,       :,,:.       .::,,::,.                ,:.    .:
  //        .,:     ,,::.    .:,.,::.                           .::   .:
  //      ,:.  :,,:..,:,,,:,.                                      ,:, .:
  //   .:::,-~-,,::,.                                                 ,:::
  // A\/<^~-:,..........................................................:^C


  Folding::UnfoldedMesh umesh;
  auto Add = [&umesh](double x, double y) {
      int idx = (int)umesh.vertices.size();
      umesh.vertices.push_back(vec2{x, y});
      return idx;
    };

  int a = Add(0, 0);
  int b = Add(4.5016358, 0.9936677);
  int c = Add(5.93, 0.0);
  int d = Add(4.1373236, 3.3051341);
  int e = Add(4.8094541, 3.469013);
  int f = Add(2.1704535, 0.7907158);

  // Four triangles.
  umesh.polygons = {
    {a, c, b},
    {a, b, f},
    {a, f, e},
    {b, c, d},
  };

  auto len = [&](int v0, int v1) {
    return (double)length(umesh.vertices[v0] - umesh.vertices[v1]);
  };
  auto diff = [](double l1, double l2) {
    return l1 > l2 ? l1 - l2 : l2 - l1;
  };

  Print("Diagnostics for edge lengths (tolerance is 1e-5):\n");
  Print("  |ea| = {:.6f}, |ac| = {:.6f}, diff = {:.6f}\n", len(e,a), len(a,c), diff(len(e,a), len(a,c)));
  Print("  |db| = {:.6f}, |bf| = {:.6f}, diff = {:.6f}\n", len(d,b), len(b,f), diff(len(d,b), len(b,f)));
  Print("  |cd| = {:.6f}, |fe| = {:.6f}, diff = {:.6f}\n\n", len(c,d), len(f,e), diff(len(c,d), len(f,e)));

  std::optional<Polyhedron> opoly =
    Folding::Fold(umesh);

  CHECK(opoly.has_value());

  for (const vec3 &v : opoly.value().vertices) {
    Print("  {:.11g}, {:.11g}, {:.11g}\n", v.x, v.y, v.z);
  }

}


int main(int argc, char **argv) {
  ANSI::Init();

  UnfoldNamiki();

  return 0;
}
