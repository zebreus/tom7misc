
#include <tuple>
#include <cmath>
#include <string>
#include <cstdint>
#include <unordered_map>

#include "threadutil.h"
#include "image.h"
#include "color-util.h"
#include "base/stringprintf.h"
#include "geom/marching.h"
#include "util.h"
#include "ansi.h"

#include "meshoptimizer.h"

using namespace std;
using uint32 = uint32_t;

// To cc-lib mesh-utils or whatever
static void SimplifyMesh(MarchingCubes::Mesh *mesh,
                         size_t target_indices,
                         float target_error) {

  std::vector<unsigned int> flat_triangles, flat_triangles_out;
  flat_triangles.reserve(mesh->triangles.size() * 3);
  for (const auto &[a, b, c] : mesh->triangles) {
    flat_triangles.push_back(a);
    flat_triangles.push_back(b);
    flat_triangles.push_back(c);
  }
  // In case we are processing a really large mesh, reclaim the
  // memory right now.
  mesh->triangles.clear();

  // Worst case output is same size as input.
  flat_triangles_out.resize(flat_triangles.size());

  // We can use vertices in place, but this code requires that
  // the three floats be first in the struct.
#if 0
  size_t num_indices = meshopt_simplifySloppy(
      (unsigned int *)flat_triangles_out.data(),
      (const unsigned int*)flat_triangles.data(),
      (size_t)flat_triangles.size(),
      (const float *)mesh->vertices.data(),
      mesh->vertices.size(),
      (size_t)sizeof (MarchingCubes::Vertex),
      target_indices,
      target_error,
      nullptr);
#endif

  size_t num_indices = meshopt_simplify(
      (unsigned int *)flat_triangles_out.data(),
      (const unsigned int*)flat_triangles.data(),
      (size_t)flat_triangles.size(),
      (const float *)mesh->vertices.data(),
      mesh->vertices.size(),
      (size_t)sizeof (MarchingCubes::Vertex),
      target_indices,
      target_error,
      // options
      0,
      nullptr);


  CHECK(num_indices % 3 == 0);
  const int num_triangles = num_indices / 3;

  // Now unnecessary.
  flat_triangles.clear();

  // Next, optimize away unused vertices and repack.
  std::unordered_map<int, int> remap;
  std::vector<MarchingCubes::Vertex> vertices_out;

  // Remap all indices in place.
  for (unsigned int &idx : flat_triangles_out) {
    // We have an index into the original vertex array,
    // but we want to use a new, densely packed index.
    int new_idx = 0;
    auto it = remap.find(idx);
    if (it == remap.end()) {
      // First use, so assign it the next index and
      // put that vertex in the output array.
      new_idx = vertices_out.size();
      vertices_out.push_back(mesh->vertices[idx]);
      remap[idx] = new_idx;
    } else {
      new_idx = it->second;
    }

    idx = new_idx;
  }

  mesh->vertices = std::move(vertices_out);
  mesh->triangles.resize(num_triangles);
  for (int i = 0; i < num_triangles; i++) {
    mesh->triangles.emplace_back(
        flat_triangles_out[i * 3 + 0],
        flat_triangles_out[i * 3 + 1],
        flat_triangles_out[i * 3 + 2]);
  }
}

/**
 * Experimental: Mesh simplifier (sloppy)
 * Reduces the number of triangles in the mesh, sacrificing mesh appearance for simplification performance
 * The algorithm doesn't preserve mesh topology but can stop short of the target goal based on target error.
 * Returns the number of indices after simplification, with destination containing new index data
 * The resulting index buffer references vertices from the original vertex buffer.
 * If the original vertex data isn't required, creating a compact vertex buffer using meshopt_optimizeVertexFetch is recommended.
 *
 * destination must contain enough space for the target index buffer, worst case is index_count elements (*not* target_index_count)!
 * vertex_positions should have float3 position in the first 12 bytes of each vertex
 * target_error represents the error relative to mesh extents that can be tolerated, e.g. 0.01 = 1% deformation
 * result_error can be NULL; when it's not NULL, it will contain the resulting (relative) error after simplification
MESHOPTIMIZER_EXPERIMENTAL size_t meshopt_simplifySloppy(unsigned int* destination, const unsigned int* indices, size_t index_count, const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride, size_t target_index_count, float target_error, float* result_error);
*/


// Baffling numbers are like complex numbers, but with three terms: a
// + bi + cj. Yes, this is known to "not work" (in the sense that
// there exists no such algebra that is associative), but we don't
// need associativity. Plus, who's gonna stop me, FROBENIUS??

//   *  1  i  j
//   1  1  i  j
//   i  i -1  A
//   j  j  B  C


template<int AR, int AI, int AJ,
         int BR, int BI, int BJ,
         int CR, int CI, int CJ>
struct baffling_tmpl {
  using baffling = baffling_tmpl<AR, AI, AJ,
                                 BR, BI, BJ,
                                 CR, CI, CJ>;

  float rpart = 0.0f, ipart = 0.0f, jpart = 0.0f;

  baffling_tmpl() {}
  baffling_tmpl(float r, float i, float j) : rpart(r), ipart(i), jpart(j) {}

  float Abs() const { return sqrtf(rpart * rpart +
                                   ipart * ipart +
                                   jpart * jpart); }

  float Re() const { return rpart; }
  float Im() const { return ipart; }
  float Jm() const { return jpart; }
  std::tuple<float, float, float> Parts() const {
    return std::make_tuple(rpart, ipart, jpart);
  }

  inline baffling &operator +=(const baffling &rhs) {
    rpart += rhs.rpart;
    ipart += rhs.ipart;
    jpart += rhs.jpart;
    return *this;
  }

  inline baffling &operator -=(const baffling &rhs) {
    rpart -= rhs.rpart;
    ipart -= rhs.ipart;
    jpart -= rhs.jpart;
    return *this;
  }

  inline baffling &operator +=(const float &rhs) {
    rpart += rhs;
    return *this;
  }

  inline baffling &operator -=(const float &rhs) {
    rpart -= rhs;
    return *this;
  }

  inline baffling &operator *=(const float &rhs) {
    rpart *= rhs;
    ipart *= rhs;
    jpart *= rhs;
    return *this;
  }

  inline baffling operator +(const baffling &b) {
    return baffling(this->rpart + b.rpart, this->ipart + b.ipart, this->jpart + b.jpart);
  }

  inline baffling operator -(const baffling &b) {
    return baffling(this->rpart - b.rpart, this->ipart - b.ipart, this->jpart - b.jpart);
  }

  inline baffling operator *(const baffling &b) {
    const baffling &a = *this;
    // (ar + ai * i + aj * j)(br + bi * i + bj * j) =
    // 9 terms:
    // (1)  ar * br     + ar * bi * i   + ar * bj * j   +
    // (2)  ai * br * i + ai * bi * i^2 + ai * bj * ij  +
    // (3)  aj * br * j + aj * bi * ji  + aj * bj * j^2
    //
    // Which collects to (and this is of course fishy, because
    // I'm assuming associativity):

    // (1)  ar * br  +
    // (2)  (ar * bi + ai * br) * i  +
    // (3)  (ar * bj + aj * br) * j  +
    // (4)  (ai * bj) * ij  +
    // (5)  (aj * bi) * ji  +
    // (6)  (ai * bi) * i^2  +
    // (7)  (aj * bj) * j^2

    float term1 = a.rpart * b.rpart;
    float term2 = a.rpart * b.ipart + a.ipart * b.rpart;
    float term3 = a.rpart * b.jpart + a.jpart * b.rpart;
    float term4 = a.ipart * b.jpart;
    float term5 = a.jpart * b.ipart;
    float term6 = a.ipart * b.ipart;
    float term7 = a.jpart * b.jpart;

    // (1) and (6)
    float r = term1 - term6;

    //   *  1  i  j
    //   1  1  i  j
    //   i  i -1  A
    //   j  j  B  C
    if constexpr (AR != 0) r += AR * term4;
    if constexpr (BR != 0) r += BR * term5;
    if constexpr (CR != 0) r += CR * term7;

    float i = term2;
    if constexpr (AI != 0) i += AI * term4;
    if constexpr (BI != 0) i += BI * term5;
    if constexpr (CI != 0) i += CI * term7;

    float j = term3;
    if constexpr (AJ != 0) j += AJ * term4;
    if constexpr (BJ != 0) j += BJ * term5;
    if constexpr (CJ != 0) j += CJ * term7;

    return baffling(r, i, j);
  }

// Operations with scalars.

  inline baffling operator +(const float &s) {
    return baffling(this->rpart + s, this->ipart, this->jpart);
  }

  inline baffling operator -(const float &s) {
    return baffling(this->rpart - s, this->ipart, this->jpart);
  }

  inline baffling operator *(const float &s) {
    // This is the same thing we get with s + 0i + 0j, but saving several
    // terms that don't do anything.
    return baffling(this->rpart * s, this->ipart * s, this->jpart * s);
  }

  // unary negation; same as multiplication by scalar -1.
  inline baffling operator -() {
    return baffling(-this->rpart, -this->ipart, -this->jpart);
  }


  // TODO: Division, etc.

  //   *  1  i  j
  //   1  1  i  j
  //   i  i -1  1
  //   j  j  1 -1

 private:
};

using baffling =
  baffling_tmpl<0, 1, 0,
                0, 0, 1,
                1, 0, 0>;

static void Mandelbrot() {
  static constexpr int SIZE = 1024;
  static constexpr int NUM_THREADS = 12;
  static constexpr int MAX_ITERS = 32;

  // static constexpr float XMIN = -2.3f, XMAX = 1.3f;
  // static constexpr float YMIN = -1.8f, YMAX = 1.8f;
  static constexpr float XMIN = -2.0f, XMAX = 2.0f;
  static constexpr float YMIN = -2.0f, YMAX = 2.0f;

  static constexpr float WIDTH = XMAX - XMIN;
  static constexpr float HEIGHT = YMAX - YMIN;

  Asynchronously async(4);

  int idx = 0;
  for (float jplane = -2.0; jplane <= 2.0; jplane += 0.025f) {
    ImageRGBA img(SIZE, SIZE);

    ParallelComp2D(
        SIZE, SIZE,
        [jplane, &img](int yp, int xp) {

          float x = (xp / (float)SIZE) * WIDTH + XMIN;
          float y = ((SIZE - yp) / (float)SIZE) * HEIGHT + YMIN;

          baffling z(0, 0, 0);
          baffling c(x, y, jplane);

          for (int i = 0; i < MAX_ITERS; i++) {
            if (z.Abs() > 2.0f) {
              // Escaped. The number of iterations gives the pixel.
              float f = i / (float)MAX_ITERS;
              uint32 color = ColorUtil::LinearGradient32(
                  ColorUtil::HEATED_METAL, f);
              img.SetPixel32(xp, yp, color);
              return;
            }

            z = z * z + c;
          }

          // Possibly in the set.
          img.SetPixel32(xp, yp, 0xFFFFFFFF);
        },
        NUM_THREADS);

    async.Run([img = std::move(img), idx]() {
        img.Save(StringPrintf("baffling-mandelbrot-%d.png", idx));
      });
    idx++;
  }
}

static void March() {
  using Pos = MarchingCubes::Pos;

  // As above, but generate an 3D mesh from an "implicit surface."
  // This is probably not a valid implicit surface; we don't have
  // a proper "distance to the surface" and the surface itself
  // has infinite area (it's a fractal). But this excursion is not,
  // like, mathematically motivated.

  static constexpr int MAX_ITERS = 32;

  auto Distance = [](Pos pos) -> float {
      // slice in half
      if (pos.z > 0.0f) return pos.z;

      baffling z(0, 0, 0);
      baffling c(pos.x, pos.y, pos.z);


      for (int i = 0; i < MAX_ITERS; i++) {
        if (z.Abs() > 2.0f) {
          // Escaped.
          // float f = i / (float)MAX_ITERS;
          // return f * 0.01f;
          return +0.001f;
        }

        z = z * z + c;
      }

      // If we didn't escape, then the value is possibly in
      // the set.
      return -0.001f;
    };

  #if 0
  auto Distance = [](Pos pos) -> float {
      float x = pos.x;
      float y = pos.y;
      float z = pos.z;

      return sqrtf(x * x + y * y + z * z) - 1;
    };
  #endif

  printf(APURPLE("Generating...") "\n");

  MarchingCubes::Mesh mesh =
    MarchingCubes::Generate(Pos(-2.0f, -2.0f, -2.0f),
                            Pos(+2.0f, +2.0f, +2.0f),
                            0.0025f,
                            Distance);

  #if 1
  printf(ABLUE("Simplifying...") "\n");
  SimplifyMesh(&mesh,
               (mesh.triangles.size() * 3) / 10,
               0.0f);
#endif

  using Vertex = MarchingCubes::Vertex;
  string obj = "o isosurface\n";
  for (const Vertex &v : mesh.vertices) {
    StringAppendF(&obj, "v %f %f %f\n", v.pos.x, v.pos.y, v.pos.z);
  }
  for (const Vertex &v : mesh.vertices) {
    StringAppendF(&obj, "vn %f %f %f\n", v.normal.x, v.normal.y, v.normal.z);
  }
  for (const auto &[a, b, c] : mesh.triangles) {
    // +1 because obj files count from 1
    const int a1 = a + 1, b1 = b + 1, c1 = c + 1;
    StringAppendF(&obj,"f %d//%d %d//%d %d//%d\n",
                  a1, a1, b1, b1, c1, c1);
  }

  Util::WriteFile("baffling-simplified.obj", obj);
}

int main(int argc, char **argv) {
  AnsiInit();
  // Mandelbrot();
  March();

  return 0;
}
