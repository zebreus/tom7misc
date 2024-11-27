
#ifndef _RUPERTS_RENDERING_H
#define _RUPERTS_RENDERING_H

#include <cstdint>
#include <vector>

#include "yocto_matht.h"
#include "polyhedra.h"

#include "image.h"

struct Rendering {
  Rendering(int width, int height);

  void Render(const Polyhedron &p, uint32_t color);
  void RenderMesh(const Mesh2D &mesh);
  void RenderBadPoints(const Mesh2D &sinner, const Mesh2D &souter);
  void RenderHull(const Mesh2D &mesh, const std::vector<int> &hull);

  void DarkenBG();

  static uint32_t Color(int idx);

  int width = 0, height = 0;
  double scale = 1.0;
  ImageRGBA img;
};


#endif
