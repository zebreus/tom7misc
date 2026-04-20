
#include <cstdio>
#include <format>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/stringprintf.h"
#include "big-polyhedra.h"
#include "geom/polyhedra.h"
#include "patches.h"
#include "util.h"
#include "yocto-math.h"

static constexpr int DIGITS = 24;

static std::vector<vec2> TransformHull(const Polyhedron &poly,
                                       const std::vector<int> &hull,
                                       const frame3 &f) {
  std::vector<vec2> shadow;
  for (int idx : hull) {
    const vec3 &pt = poly.vertices[idx];
    vec3 v = transform_point(f, pt);
    shadow.push_back(vec2(v.x, v.y));
  }
  return shadow;
}

int main(int argc, char **argv) {
  ANSI::Init();

  BigPoly scube = BigScube(DIGITS);
  Polyhedron small_scube = SmallPoly(scube);

  Boundaries boundaries(scube);
  PatchInfo patch_info =
    LoadPatchInfo("scube-patchinfo.txt");

  std::string json = "[";
  for (const auto &[code, cp] : patch_info.canonical) {
    const vec3 view = SmallVec(cp.example);
    const auto &[az, an] = SphericalFromView(view);

    AppendFormat(
        &json,
        "{{ 'code': '{:b}', 'mask': '{:b}',\n"
        "  'ex_spherical': {{'az': {:.17g}, 'an': {:.17g}}},\n"
        "  'ex_cartesian': {{'x': {:.17g}, 'y': {:.17g}, 'z': {:.17g}}},\n"
        "  'ex_hull': ",
        cp.code, cp.mask,
        az, an,
        view.x, view.y, view.z);

    frame3 view_frame = FrameFromViewPos(view);
    std::vector<vec2> outer_shadow =
      TransformHull(small_scube, cp.hull, view_frame);

    std::vector<std::string> pts;
    for (const vec2 &v : outer_shadow) {
      pts.push_back(std::format("{{'x': {:.17g}, 'y': {:.17g}}}",
                                v.x, v.y));
    }

    AppendFormat(&json, "[{}]\n",
                 Util::Join(pts, ", "));

    AppendFormat(&json, "}},\n");
  }

  AppendFormat(&json, "]\n");

  printf("%s\n", json.c_str());

  return 0;
}
