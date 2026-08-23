
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ansi.h"
#include "geom/polygonization.h"
#include "geom/polygons.h"
#include "hashing.h"
#include "letters.h"
#include "yocto-math.h"

// Physical properties for the simulation.
// kg/m^2
static constexpr double DENSITY = 1.0;
// N/m
static constexpr double TENSION = 1000.0;
// kg/(m^2 s)
static constexpr double DAMPING = 0.01;
// Samples per second.
static constexpr int SAMPLE_RATE = 44100;
static constexpr double DT = 1.0 / SAMPLE_RATE;
// kg/(m*s). 0 means free-floating; infinity means fixed (like a drum head).
static constexpr double BOUNDARY_DAMPING =
  std::numeric_limits<double>::infinity();

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
  vec2 center = {0.0f, 0.0f};
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

// Advance the simulation state by DT.
static void StepState(const SimLetter &letter, SimState *state) {
  int n = letter.triangles.size();
  std::vector<double> force(n, 0.0);

  for (int i = 0; i < n; i++) {
    const SimTri &tri = letter.triangles[i];
    double z_i = state->pos[i];
    double v_i = state->vel[i];

    // Intrinsic damping force
    double f = -DAMPING * tri.area * v_i;

    // Forces from neighboring triangles or boundaries
    auto edge_force = [&](int neighbor_idx, float len) -> double {
      if (neighbor_idx >= 0) {
        double z_j = state->pos[neighbor_idx];
        const SimTri &ntri = letter.triangles[neighbor_idx];
        double dist = yocto::distance(tri.center, ntri.center);
        dist = dist < 1e-9 ? 1e-9 : dist;
        return TENSION * len * (z_j - z_i) / dist;
      } else {
        if (std::isinf(BOUNDARY_DAMPING)) {
          // Fixed boundary at z = 0
          double dist = (2.0 * tri.area) / (3.0 * len);
          dist = dist < 1e-9 ? 1e-9 : dist;
          return TENSION * len * (0.0 - z_i) / dist;
        } else {
          // Absorbing boundary
          return -BOUNDARY_DAMPING * len * v_i;
        }
      }
    };

    f += edge_force(tri.ab, tri.len_ab);
    f += edge_force(tri.bc, tri.len_bc);
    f += edge_force(tri.ca, tri.len_ca);

    force[i] = f;
  }

  // Update velocities and positions using semi-implicit Euler
  for (int i = 0; i < n; i++) {
    double mass = DENSITY * letter.triangles[i].area;
    mass = mass < 1e-9 ? 1e-9 : mass;
    double accel = force[i] / mass;
    state->vel[i] += accel * DT;
    state->pos[i] += state->vel[i] * DT;
  }
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

    tri.center.x = (va.x + vb.x + vc.x) / 3.0f;
    tri.center.y = (va.y + vb.y + vc.y) / 3.0f;

    result.triangles.push_back(tri);
  }

  // Compute adjacency using a map of directed edges.
  std::unordered_map<std::pair<int, int>, int,
                     Hashing<std::pair<int, int>>> edge_to_tri;
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
    StepState(sletter, &state);

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
