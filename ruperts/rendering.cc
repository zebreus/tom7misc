
#include "rendering.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <utility>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "util.h"

#include "yocto_matht.h"
#include "polyhedra.h"

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;
using vec4 = yocto::vec<double, 4>;
using mat4 = yocto::mat<double, 4>;
using quat4 = yocto::quat<double, 4>;
using frame3 = yocto::frame<double, 3>;

constexpr double MESH_SCALE = 0.20;

static std::vector<uint32_t> COLORS = {
  0xFF0000FF,
  0xFFFF00FF,
  0x00FF00FF,
  0x00FFFFFF,
  0x0000FFFF,
  0xFF00FFFF,
  0x770000FF,
  0x777700FF,
  0x007700FF,
  0x007777FF,
  0x000077FF,
  0x770077FF,
  0xFF7777FF,
  0xFFFF77FF,
  0x77FF77FF,
  0x77FFFFFF,
  0x7777FFFF,
  0xFF77FFFF,
  0x330000FF,
  0x333300FF,
  0x003300FF,
  0x003333FF,
  0x000033FF,
  0x330033FF,
  0x77333377,
  0x77773377,
  0x33773377,
  0x33777777,
  0x33337777,
  0x77337777,
  0xFFAAAAFF,
  0xFFFFAAFF,
  0xAAFFAAFF,
  0xAAFFFFFF,
  0xAAAAFFFF,
  0xFFAAFFFF,
  0xFF3333FF,
  0xFFFF33FF,
  0x33FF33FF,
  0x33FFFFFF,
  0x3333FFFF,
  0xFF33FFFF,
};

Rendering::Rendering(int width_in, int height_in) :
  width(width_in), height(height_in), img(width, height) {
  scale = std::min(width, height) * 0.75;
}

void Rendering::Render(const Polyhedron &p, uint32_t color) {
  constexpr double aspect = 1.0;
  mat4 proj = yocto::perspective_mat(
      yocto::radians(60.0), aspect, 0.1, 100.0);

  frame3 camera_frame = yocto::lookat_frame<double>(
      {0, 0, 5}, {0, 0, 0}, {0, 1, 0});
  mat4 view_matrix = yocto::frame_to_mat(camera_frame);
  mat4 model_view_projection = proj * view_matrix;

  for (const std::vector<int> &face : p.faces->v) {
    for (int i = 0; i < (int)face.size(); i++) {
      vec3 v0 = p.vertices[face[i]];
      vec3 v1 = p.vertices[face[(i + 1) % face.size()]];

      vec2 p0 = Project(v0, model_view_projection);
      vec2 p1 = Project(v1, model_view_projection);

      float x0 = (p0.x * scale + width * 0.5);
      float y0 = (p0.y * scale + height * 0.5);
      float x1 = (p1.x * scale + width * 0.5);
      float y1 = (p1.y * scale + height * 0.5);
      img.BlendThickLine32(x0, y0, x1, y1, 4.0f, color & 0xFFFFFF88);
    }
  }
}

void Rendering::RenderMesh(const Mesh2D &mesh) {
  // XXX compute this from the polyhedron's diameter
  const double polyscale = std::min(width, height) * MESH_SCALE;

  CHECK(mesh.faces->v.size() < COLORS.size()) << mesh.faces->v.size()
                                              << " but have "
                                              << COLORS.size();

  auto ToWorld = [this, polyscale](int sx, int sy) -> vec2 {
      // Center of screen should be 0,0.
      double cy = sy - height / 2.0;
      double cx = sx - width / 2.0;
      return vec2{.x = cx / polyscale, .y = cy / polyscale};
    };
  auto ToScreen = [this, polyscale](const vec2 &pt) -> std::pair<int, int> {
    double cx = pt.x * polyscale;
    double cy = pt.y * polyscale;
    return std::make_pair(cx + width / 2.0, cy + height / 2.0);
  };

  // Draw filled polygons first.
  for (int sy = 0; sy < height; sy++) {
    for (int sx = 0; sx < width; sx++) {
      vec2 pt = ToWorld(sx, sy);
      for (int i = 0; i < mesh.faces->v.size(); i++) {
        if (PointInPolygon(pt, mesh.vertices, mesh.faces->v[i])) {
          img.BlendPixel32(sx, sy, COLORS[i] & 0xFFFFFF22);
        }
      }
    }
  }

  // Draw lines on top.
  for (const std::vector<int> &face : mesh.faces->v) {
    for (int i = 0; i < face.size(); i++) {
      const vec2 &a = mesh.vertices[face[i]];
      const vec2 &b = mesh.vertices[face[(i + 1) % face.size()]];

      const auto &[ax, ay] = ToScreen(a);
      const auto &[bx, by] = ToScreen(b);
      img.BlendThickLine32(ax, ay, bx, by, 3.0, 0xFFFFFF99);
    }
  }
}

void Rendering::RenderBadPoints(const Mesh2D &sinner, const Mesh2D &souter) {
  // XXX compute this from the polyhedron's diameter
  const double polyscale = std::min(width, height) * MESH_SCALE;

  auto ToScreen = [this, polyscale](const vec2 &pt) -> std::pair<int, int> {
    double cx = pt.x * polyscale;
    double cy = pt.y * polyscale;
    return std::make_pair(cx + width / 2.0, cy + height / 2.0);
  };

  for (const vec2 &v : sinner.vertices) {
    if (!InMesh(souter, v)) {
      const auto &[sx, sy] = ToScreen(v);
      img.BlendThickCircle32(sx, sy, 20, 4, 0xFF0000AA);
    }
  }
}

void Rendering::RenderHull(const Mesh2D &mesh,
                           const std::vector<int> &hull) {
  // XXX compute this from the polyhedron's diameter
  const double polyscale = std::min(width, height) * MESH_SCALE;

  auto ToScreen = [this, polyscale](const vec2 &pt) -> std::pair<int, int> {
    double cx = pt.x * polyscale;
    double cy = pt.y * polyscale;
    return std::make_pair(cx + width / 2.0, cy + height / 2.0);
  };

  for (int i = 0; i < hull.size(); i++) {
    const vec2 &v0 = mesh.vertices[hull[i]];
    const vec2 &v1 = mesh.vertices[hull[(i + 1) % hull.size()]];

    const auto &[x0, y0] = ToScreen(v0);
    const auto &[x1, y1] = ToScreen(v1);

    img.BlendThickLine32(x0, y0, x1, y1, 2.0, 0x00FF00AA);
  }

  for (int i = 0; i < hull.size(); i++) {
    const vec2 &v0 = mesh.vertices[hull[i]];
    const auto &[x, y] = ToScreen(v0);
    img.BlendText32(x - 12, y - 12, 0xFFFF00FF,
                     StringPrintf("%d", i));
  }
}

void Rendering::DarkenBG() {
  for (int y = 0; y < img.Height(); y++) {
    for (int x = 0; x < img.Width(); x++) {
      img.BlendPixel32(x, y, 0x550000AA);
    }
  }
}

uint32_t Rendering::Color(int idx) {
  return COLORS[idx % COLORS.size()];
}
