

#include <cmath>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "hashing.h"
#include "geom/polygonization.h"
#include "letters.h"
#include "yocto-math.h"
#include "ansi.h"

// Physical properties for the simulation.
static constexpr double DENSITY = 1.0;
static constexpr double TENSION = 1000.0;
static constexpr double DAMPING = 0.01;
static constexpr int SAMPLE_RATE = 44100;
static constexpr double DT = 1.0 / SAMPLE_RATE;

struct SimTri {
  // Vertex indices. Screen clockwise (Cartesian CCW) winding order.
  int a = 0, b = 0, c = 0;
  // Index of the neighboring triangle across each edge.
  // Unpaired edges have -1.
  int ab = 0, bc = 0, ca =0;

  // The length of these edges.
  float len_ab = 0.0f, len_bc = 0.0f, len_ca = 0.0f;

  // Physical quantities that we will need for the simulation.
  float area = 0.0f;
};

struct SimLetter {
  std::vector<vec2> vertices;
  std::vector<SimTri> triangles;
};

// Triangles only move along the z-axis. This is the simulation state,
// with vectors parallel to the triangles in SimLetter.
struct SimState {
  std::vector<double> pos;
  std::vector<double> vel;
};

static SimState RestState(const SimLetter &letter) {
  return SimState{
    .pos = std::vector<double>(letter.triangles.size(), 0.0),
    .vel = std::vector<double>(letter.triangles.size(), 0.0),
  };
}

static void ApplyImpulse(SimState *state, int idx, double accel) {
  state->vel[idx] += accel;
}

static void StepState(SimState *state) {
  // TODO
}

SimLetter MakeSimLetter(const Letter &letter) {
  SimLetter result;

  // Copy the vertex data directly.
  result.vertices.reserve(letter.mesh.vertices.size());
  for (int i = 0; i < (int)letter.mesh.vertices.size(); i++) {
    result.vertices.push_back(letter.mesh.vertices[i]);
  }

  // Convert polygons into triangles, assuming the mesh is fully triangulated.
  result.triangles.reserve(letter.mesh.polygons.size());
  for (int i = 0; i < (int)letter.mesh.polygons.size(); i++) {
    const std::vector<int> &poly = letter.mesh.polygons[i];
    CHECK(poly.size() == 3) << "Mesh must be triangular.";

    int a = poly[0];
    int b = poly[1];
    int c = poly[2];

    const vec2 &va = result.vertices[a];
    const vec2 &vb = result.vertices[b];
    const vec2 &vc = result.vertices[c];

    // Compute signed area to ensure Screen CW (Cartesian CCW) winding.
    // In a y-down coordinate system, a positive cross product corresponds
    // to a Screen CW winding order.
    double cross = yocto::cross(vb - va, vc - va);
    if (cross < 0.0) {
      std::swap(b, c);
      cross = -cross;
    }

    const vec2 &vb_fixed = result.vertices[b];
    const vec2 &vc_fixed = result.vertices[c];

    SimTri tri;
    tri.a = a;
    tri.b = b;
    tri.c = c;
    tri.ab = -1;
    tri.bc = -1;
    tri.ca = -1;

    tri.len_ab = (float)yocto::distance(vb_fixed, va);
    tri.len_bc = (float)yocto::distance(vc_fixed, vb_fixed);
    tri.len_ca = (float)yocto::distance(va, vc_fixed);

    tri.area = (float)(0.5 * cross);
    CHECK(tri.area >= 0.0);

    result.triangles.push_back(tri);
  }

  // Compute adjacency using a map of directed edges.
  std::unordered_map<std::pair<int, int>, int, Hashing<std::pair<int, int>>> edge_to_tri;
  for (int i = 0; i < (int)result.triangles.size(); i++) {
    const SimTri &tri = result.triangles[i];
    edge_to_tri[{tri.a, tri.b}] = i;
    edge_to_tri[{tri.b, tri.c}] = i;
    edge_to_tri[{tri.c, tri.a}] = i;
  }

  for (int i = 0; i < (int)result.triangles.size(); i++) {
    SimTri &tri = result.triangles[i];

    auto it_ab = edge_to_tri.find({tri.b, tri.a});
    if (it_ab != edge_to_tri.end()) {
      tri.ab = it_ab->second;
    }

    auto it_bc = edge_to_tri.find({tri.c, tri.b});
    if (it_bc != edge_to_tri.end()) {
      tri.bc = it_bc->second;
    }

    auto it_ca = edge_to_tri.find({tri.a, tri.c});
    if (it_ca != edge_to_tri.end()) {
      tri.ca = it_ca->second;
    }
  }

  return result;
}

void TestOne(const Letters &letters, char ch) {
  auto it = letters.letter.find(ch);
  CHECK(it != letters.letter.end());
  SimLetter sletter = MakeSimLetter(it->second);

  SimState state = RestState(sletter);

  ApplyImpulse(&state, 0, 1.0);

  static constexpr int SECONDS = 5;
  for (int i = 0; i < SAMPLE_RATE * SECONDS; i++) {
    StepState(&state);

    // TODO: Sample and record wave
  }
}

void Test() {
  std::unique_ptr<Letters> font = Letters::LoadFont("helveticab.ttf", true);
  TestOne(*font, 'r');
}

int main(int argc, char **argv) {
  ANSI::Init();

  Test();

  return 0;
}
