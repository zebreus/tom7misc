#include "albrecht.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "bit-string.h"
#include "color-util.h"
#include "db.h"
#include "examples.h"
#include "geom/polyhedra.h"
#include "image.h"
#include "mov-recorder.h"
#include "mov.h"
#include "opt/opt.h"
#include "periodically.h"
#include "randutil.h"
#include "status-bar.h"
#include "util.h"
#include "yocto-math.h"

/*
Can you help me implement an animation of a convex polyhedron
unfolding according to a net? See project.txt for background.

Here we have a specific polyhedron and an unfolding, which
is specified as a set of edges that are connected (in the
face-spanning tree). albrecht.h can convert these to a 2D
map of the net (or invalid, overlapping net), although to
animate we probably need to repeat some of those calculations.

I'd like the animation to position polygonal faces in 3D,
which we can then project onto 2D images (image.h) and
encode as a movie (mov-recorder.h). We should separate the
task of generating the scene geometry from the rasterization.
For rasterization, we can just use line drawing and
polygon-filling routines from image.h; a "retro" pixelated
aesthetic is what I'm going for anyway.

For the geometry, the net will end up in 2D on the XY plane
when everything is done. As we animate, the 3D faces of
the polyhedron will have positive Z coordinates, with the
camera looking down towards the origin. I usually think of
X increasing to the right and Y increasing downward (computer
graphics style), but it doesn't really matter for this
problem.

We'll pick some starting face for the animation. We begin with the
polyhedron sitting with the initial face on the XY plane. Each edge
connected to that face will get a piece of the polyhedron (the faces
on the subtree). They will fold out simultaneously, hinged along the
edge, until the next face is flat. This process repeats, with (in
general) several polyhedron parts "rolling" outward in lock-step.
Once all of them are deposited on the XY plane, the animation is
complete.

We can assume that the unfolding is acyclic, and connected. But
it may have overlaps between faces in the plane. We should detect
these so that we can color them differently when they land on
the XY plane.

 */

using Aug = Albrecht::AugmentedPoly;

// RGBA
static constexpr uint32_t connected_edge_color = 0xFFFFFFAA;
static constexpr uint32_t cut_edge_color = 0x0000FFAA;

// The minimum camera starting distance as a factor of the polyhedron's diameter.
static constexpr double MIN_STARTING_DISTANCE_FACTOR = 1.5;

struct AnimNode {
  // The index of the AugmentedPoly's face that's
  // described by this tree node.
  int face_idx = -1;
  int parent_node_idx = -1;
  int depth = 0;
  std::vector<int> children;

  // RGBA color; in the future, different faces
  // may have different colors.
  uint32_t face_color = 0x77889944;

  // Hinge properties in the original 3D space
  vec3 hinge_axis;
  int pivot_v_idx = -1;
  double max_angle = 0.0;
};

// Computes the inward-pointing unit normal of a face.
static vec3 GetInwardNormal(const Polyhedron &poly, int face_idx) {
  const std::vector<int> &face = poly.faces->v[face_idx];
  CHECK(face.size() >= 3);
  const vec3 &v0 = poly.vertices[face[0]];
  const vec3 &v1 = poly.vertices[face[1]];
  const vec3 &v2 = poly.vertices[face[2]];
  return yocto::normalize(yocto::cross(v1 - v0, v2 - v0));
}

// Creates a frame representing rotation around an axis through point p.
static frame3 RotateAroundAxis(const vec3 &p, const vec3 &axis,
                               double angle) {
  frame3 r = yocto::rotation_frame(axis, angle);
  r.o = p - yocto::transform_point(r, p);
  return r;
}

// Constructs the inverse frame that maps the root face to the XY plane
// such that the rest of the polyhedron lies in the Z >= 0 half-space.
static frame3 GetRootToWorldFrame(const Polyhedron &poly, int root_face) {
  const std::vector<int> &face = poly.faces->v[root_face];
  CHECK(face.size() >= 3);
  const vec3 &v0 = poly.vertices[face[0]];
  const vec3 &v1 = poly.vertices[face[1]];
  const vec3 &v2 = poly.vertices[face[2]];

  vec3 ey = yocto::normalize(v1 - v0);
  vec3 n = yocto::normalize(yocto::cross(ey, v2 - v0));
  vec3 ex = yocto::cross(n, ey);

  frame3 f;
  f.x = ex;
  f.y = ey;
  f.z = n;
  f.o = v0;

  return yocto::inverse(f);
}

// Traverses the unfolding tree and computes the hinge properties in 3D.
static std::vector<AnimNode> BuildUnfoldingTree(
    const Aug &aug,
    BitStringConstView unfolding,
    int root_face) {

  const Polyhedron &poly = aug.poly;
  const Faces &faces = *poly.faces;
  const int num_faces = faces.NumFaces();

  std::vector<AnimNode> tree;
  std::vector<int> face_to_node(num_faces, -1);

  std::vector<int> q;
  q.push_back(root_face);

  AnimNode root_node;
  root_node.face_idx = root_face;
  root_node.parent_node_idx = -1;
  root_node.depth = 0;
  tree.push_back(root_node);
  face_to_node[root_face] = 0;

  size_t head = 0;
  while (head < q.size()) {
    int curr_face = q[head++];
    int curr_node_idx = face_to_node[curr_face];

    for (int edge_idx : aug.face_edges[curr_face]) {
      if (unfolding.Get(edge_idx)) {
        const Faces::Edge &edge = faces.edges[edge_idx];
        int next_face = (edge.f0 == curr_face) ? edge.f1 : edge.f0;

        if (face_to_node[next_face] == -1) {
          AnimNode child_node;
          child_node.face_idx = next_face;
          child_node.parent_node_idx = curr_node_idx;
          child_node.depth = tree[curr_node_idx].depth + 1;

          // Hinge axis is perpendicular to both inward normals
          vec3 n1 = GetInwardNormal(poly, curr_face);
          vec3 n2 = GetInwardNormal(poly, next_face);
          vec3 axis = yocto::normalize(yocto::cross(n2, n1));
          double angle = std::acos(
              std::clamp(yocto::dot(n2, n1), -1.0, 1.0));

          child_node.hinge_axis = axis;
          child_node.pivot_v_idx = edge.v0;
          child_node.max_angle = angle;

          int child_node_idx = tree.size();
          tree.push_back(child_node);
          tree[curr_node_idx].children.push_back(child_node_idx);
          face_to_node[next_face] = child_node_idx;

          q.push_back(next_face);
        }
      }
    }
  }

  return tree;
}

// Computes the accumulated local-to-world frames for all faces at time T.
static std::vector<frame3> ComputeNodeFrames(
    const Polyhedron &poly,
    const std::vector<AnimNode> &tree,
    double T,
    const frame3 &root_to_world) {

  std::vector<frame3> node_frames(tree.size());
  node_frames[0] = root_to_world;

  for (size_t i = 1; i < tree.size(); ++i) {
    const auto &node = tree[i];
    int parent = node.parent_node_idx;

    // Linear interpolation of the rotation angle per level
    double d = node.depth;
    double s = 0.0;
    if (T >= d) {
      s = 1.0;
    } else if (T <= d - 1) {
      s = 0.0;
    } else {
      s = T - (d - 1);
    }

    double angle = s * node.max_angle;
    frame3 rot = RotateAroundAxis(poly.vertices[node.pivot_v_idx],
                                  node.hinge_axis, angle);

    node_frames[i] = node_frames[parent] * rot;
  }

  return node_frames;
}

struct Camera {
  vec3 camera_pos = {0, 0, 1};
  vec3 looking_at = {0, 0, 0};
  vec3 y_direction = {0, -1, 0};
};

static void BlendFilledPoly(ImageRGBA &img,
                            const std::vector<vec2> &poly,
                            uint32_t color) {
  if (poly.size() < 3) return;

  double min_x = poly[0].x;
  double max_x = poly[0].x;
  double min_y = poly[0].y;
  double max_y = poly[0].y;
  for (int i = 1; i < poly.size(); i++) {
    const vec2 &p = poly[i];
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }

  int x0 = std::max(0, (int)std::floor(min_x));
  int x1 = std::min(img.Width() - 1, (int)std::ceil(max_x));
  int y0 = std::max(0, (int)std::floor(min_y));
  int y1 = std::min(img.Height() - 1, (int)std::ceil(max_y));

  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (PointInPolygon(vec2{(double)x, (double)y}, poly)) {
        img.BlendPixel32(x, y, color);
      }
    }
  }
}

// Draws a coarse grid on the XY plane with diminishing alpha as segments get
// more distant from the origin.
static void DrawXYGrid(ImageRGBA &img, const frame3 &camera_frame,
                       double dist, double zoom, double d,
                       int width, int height) {
  static constexpr double GRID_SPACING = 1.0;
  static constexpr double GRID_FADE_START = 16.0 * GRID_SPACING;
  static constexpr double GRID_FADE_END = 32.0 * GRID_SPACING;
  static constexpr uint32_t GRID_COLOR = 0x00770099;

  uint8_t r = (GRID_COLOR >> 24) & 0xFF;
  uint8_t g = (GRID_COLOR >> 16) & 0xFF;
  uint8_t b = (GRID_COLOR >> 8) & 0xFF;
  uint8_t a = GRID_COLOR & 0xFF;

  frame3 inv_camera_frame = yocto::inverse(camera_frame);

  auto ProjectAndClip = [&](const vec3 &pt, vec2 &proj, bool &ok) {
    vec3 pt_cam = yocto::transform_point(inv_camera_frame, pt);
    if (pt_cam.z >= 0.0) {
      ok = false;
      return;
    }
    double z = -pt_cam.z;
    double factor = (std::min(width, height) * 0.6 * zoom) * (dist / z) / d;
    proj = vec2{
      (float)(width / 2.0 + pt_cam.x * factor),
      (float)(height / 2.0 - pt_cam.y * factor)
    };
    ok = true;
  };

  int max_lines = (int)std::ceil(GRID_FADE_END / GRID_SPACING);

  auto DrawSegment = [&](const vec3 &p0, const vec3 &p1) {
    vec3 mid = (p0 + p1) * 0.5f;
    double r_dist = yocto::length(mid);
    if (r_dist >= GRID_FADE_END) return;

    vec2 proj0, proj1;
    bool ok0 = false, ok1 = false;
    ProjectAndClip(p0, proj0, ok0);
    ProjectAndClip(p1, proj1, ok1);
    if (!ok0 || !ok1) return;

    double t = 1.0;
    if (r_dist > GRID_FADE_START) {
      t = (GRID_FADE_END - r_dist) / (GRID_FADE_END - GRID_FADE_START);
    }
    uint8_t final_a = (uint8_t)(a * t);
    uint32_t color = ColorUtil::Pack32(r, g, b, final_a);

    img.BlendThickLine32(proj0.x, proj0.y,
                         proj1.x, proj1.y,
                         2.0,
                         color);
  };

  for (int i = -max_lines; i <= max_lines; ++i) {
    double coord = i * GRID_SPACING;
    for (int j = -max_lines; j < max_lines; ++j) {
      double start = j * GRID_SPACING;
      double end = (j + 1) * GRID_SPACING;
      // Line parallel to Y (varying Y, constant X = coord)
      DrawSegment(vec3{(float)coord, (float)start, 0.0f},
                  vec3{(float)coord, (float)end, 0.0f});
      // Line parallel to X (varying X, constant Y = coord)
      DrawSegment(vec3{(float)start, (float)coord, 0.0f},
                  vec3{(float)end, (float)coord, 0.0f});
    }
  }
}

struct Scene {
  const Aug &aug;
  std::vector<AnimNode> nodes;
  int root_face = 0;
  int max_depth = 0;
  std::vector<int> face_overlap;
  std::vector<bool> unfolding_edges;

  // Spherical bounding volume for the scene at the beginning
  // and end of the animation.
  vec3 start_center = {0.0, 0.0, 0.0};
  double start_radius = 1.0;
  vec3 end_center = {0.0, 0.0, 0.0};
  double end_radius = 1.0;

  // Camera simplified parameters
  double opt_theta_0 = 1.5;
  double opt_phi_0 = 0.78;
  double opt_r0 = 1.0;
  double opt_r1 = 1.0;
  double opt_r2 = 1.0;
  double opt_r3 = 1.0;

  Scene(const Aug &aug,
        BitStringConstView unfolding,
        int root_face) : aug(aug), root_face(root_face) {
    nodes = BuildUnfoldingTree(aug, unfolding, root_face);

    max_depth = 0;
    for (const auto &node : nodes) {
      max_depth = std::max(max_depth, node.depth);
    }

    Albrecht::DebugResult debug = Albrecht::DebugUnfolding(aug, unfolding);
    face_overlap = debug.face_overlap;

    unfolding_edges.resize(aug.poly.faces->NumEdges());
    for (int i = 0; i < aug.poly.faces->NumEdges(); ++i) {
      unfolding_edges[i] = unfolding.Get(i);
    }

    frame3 initial_tf = GetRootToWorldFrame(aug.poly, root_face);

    std::vector<frame3> start_tfs = ComputeNodeFrames(
        aug.poly, nodes, 0.0, initial_tf);
    std::vector<vec3> start_pts;
    start_pts.reserve(nodes.size() * 4);
    for (size_t i = 0; i < nodes.size(); ++i) {
      const auto &face = aug.poly.faces->v[nodes[i].face_idx];
      for (int v_idx : face) {
        start_pts.push_back(
            yocto::transform_point(start_tfs[i],
                                   aug.poly.vertices[v_idx]));
      }
    }

    vec3 start_min = start_pts[0];
    vec3 start_max = start_pts[0];
    for (size_t i = 1; i < start_pts.size(); ++i) {
      const auto &pt = start_pts[i];
      start_min.x = std::min(start_min.x, pt.x);
      start_min.y = std::min(start_min.y, pt.y);
      start_min.z = std::min(start_min.z, pt.z);
      start_max.x = std::max(start_max.x, pt.x);
      start_max.y = std::max(start_max.y, pt.y);
      start_max.z = std::max(start_max.z, pt.z);
    }
    start_center = (start_min + start_max) * 0.5;

    start_radius = 0.0;
    for (const auto &pt : start_pts) {
      start_radius = std::max(
          start_radius, yocto::length(pt - start_center));
    }

    std::vector<frame3> end_tfs = ComputeNodeFrames(
        aug.poly, nodes, max_depth, initial_tf);
    std::vector<vec3> end_pts;
    end_pts.reserve(nodes.size() * 4);
    for (size_t i = 0; i < nodes.size(); ++i) {
      const auto &face = aug.poly.faces->v[nodes[i].face_idx];
      for (int v_idx : face) {
        end_pts.push_back(
            yocto::transform_point(end_tfs[i],
                                   aug.poly.vertices[v_idx]));
      }
    }

    vec3 end_min = end_pts[0];
    vec3 end_max = end_pts[0];
    for (size_t i = 1; i < end_pts.size(); ++i) {
      const auto &pt = end_pts[i];
      end_min.x = std::min(end_min.x, pt.x);
      end_min.y = std::min(end_min.y, pt.y);
      end_min.z = std::min(end_min.z, pt.z);
      end_max.x = std::max(end_max.x, pt.x);
      end_max.y = std::max(end_max.y, pt.y);
      end_max.z = std::max(end_max.z, pt.z);
    }
    end_center = (end_min + end_max) * 0.5;

    end_radius = 0.0;
    for (const auto &pt : end_pts) {
      end_radius = std::max(
          end_radius, yocto::length(pt - end_center));
    }
    OptimizeCamera();
  }

  void OptimizeCamera() {
    const double PI = 3.14159265358979323846;

    // Sample some frames through the movie.
    static constexpr int NUM_SAMPLES = 12;
    struct Sample {
      double T;
      std::vector<vec3> pts;
    };
    std::vector<Sample> samples(NUM_SAMPLES);
    for (int k = 0; k < NUM_SAMPLES; k++) {
      double T = (max_depth > 0 ? max_depth : 1.0) *
        (k / (NUM_SAMPLES - 1.0));
      samples[k].T = T;
      frame3 initial_tf = GetRootToWorldFrame(aug.poly, root_face);
      std::vector<frame3> face_tfs = ComputeNodeFrames(
          aug.poly, nodes, T, initial_tf);
      std::vector<vec3> pts;
      pts.reserve(nodes.size() * 4);
      for (size_t i = 0; i < nodes.size(); ++i) {
        const auto &face = aug.poly.faces->v[nodes[i].face_idx];
        for (int v_idx : face) {
          pts.push_back(
              yocto::transform_point(face_tfs[i],
                                     aug.poly.vertices[v_idx]));
        }
      }
      samples[k].pts = std::move(pts);
    }

    // Set up initial parameters
    double d = Diameter(aug.poly);
    double min_start_dist = MIN_STARTING_DISTANCE_FACTOR * d;

    double init_theta = PI / 2.0 - 0.2;
    double init_phi = PI / 4.0;
    double init_r0 = std::max(3.0 * start_radius, min_start_dist);
    double init_r1 = std::max(3.0 * start_radius, min_start_dist);
    double init_r2 = 3.0 * end_radius;
    double init_r3 = 3.0 * end_radius;

    std::vector<double> start_params = {
      init_theta,
      init_phi,
      init_r0,
      init_r1,
      init_r2,
      init_r3
    };

    auto Loss = [&](std::span<const double> params) -> double {
      double theta_0 = params[0];
      double phi_0 = params[1];
      double r0 = params[2];
      double r1 = params[3];
      double r2 = params[4];
      double r3 = params[5];

      double d = Diameter(aug.poly);
      double total_loss = 0.0;

      for (const auto& sample : samples) {
        double T = sample.T;
        double t_param = max_depth > 0 ?
          std::clamp(T / max_depth, 0.0, 1.0) : 0.0;
        double mt = 1.0 - t_param;

        double theta = (1.0 - t_param) * theta_0;
        double phi = phi_0;
        double dist = r0 * (mt * mt * mt) +
                      r1 * (3.0 * mt * mt * t_param) +
                      r2 * (3.0 * mt * t_param * t_param) +
                      r3 * (t_param * t_param * t_param);

        vec3 look_at = (1.0 - t_param) * start_center + t_param * end_center;

        vec3 z_dir = vec3{
          (float)(std::sin(theta) * std::sin(phi)),
          (float)(-std::sin(theta) * std::cos(phi)),
          (float)std::cos(theta)
        };
        vec3 cam_pos = look_at + z_dir * (float)dist;

        vec3 x_dir = vec3{(float)std::cos(phi), (float)std::sin(phi), 0.0f};
        vec3 y_dir = vec3{
          (float)(-std::cos(theta) * std::sin(phi)),
          (float)(std::cos(theta) * std::cos(phi)),
          (float)std::sin(theta)
        };

        frame3 camera_frame;
        camera_frame.x = x_dir;
        camera_frame.y = y_dir;
        camera_frame.z = z_dir;
        camera_frame.o = cam_pos;

        frame3 inv_camera_frame = yocto::inverse(camera_frame);

        double progress =
          std::clamp(T / (max_depth > 0 ? max_depth : 1.0), 0.0, 1.0);
        double zoom = 1.0 - 0.5 * progress;

        double max_val = 0.0;
        for (const auto& pt : sample.pts) {
          vec3 pt_cam = yocto::transform_point(inv_camera_frame, pt);
          double z = -pt_cam.z;
          if (z < 0.01) z = 0.01;
          double factor = (1080.0 * 0.6 * zoom) * (dist / z) / d;
          double u = std::abs(pt_cam.x * factor / 960.0);
          double v = std::abs(pt_cam.y * factor / 540.0);
          max_val = std::max(max_val, std::max(u, v));
        }

        if (max_val > 0.95) {
          total_loss += (max_val - 0.95) * 10000.0;
        } else {
          total_loss += (0.95 - max_val) * (0.95 - max_val);
        }
      }
      return total_loss;
    };

    std::vector<double> lower_bounds = {
      1.0,
      0.0,
      min_start_dist,
      min_start_dist,
      1.0 * end_radius,
      1.0 * end_radius
    };
    std::vector<double> upper_bounds = {
      PI / 2.0,
      2.0 * PI,
      10.0 * start_radius,
      10.0 * start_radius,
      10.0 * end_radius,
      10.0 * end_radius
    };

    auto [best_params, best_loss] = Opt::Minimize(
        6,
        Loss,
        lower_bounds,
        upper_bounds,
        1000);

    // Apply best parameters
    opt_theta_0 = best_params[0];
    opt_phi_0 = best_params[1];
    opt_r0 = best_params[2];
    opt_r1 = best_params[3];
    opt_r2 = best_params[4];
    opt_r3 = best_params[5];
  }

  Camera GetCamera(double T) {
    Camera camera;
    double t_param = max_depth > 0 ? std::clamp(T / max_depth, 0.0, 1.0) : 0.0;
    double mt = 1.0 - t_param;

    double theta = (1.0 - t_param) * opt_theta_0;
    double phi = opt_phi_0;
    double dist = opt_r0 * (mt * mt * mt) +
                  opt_r1 * (3.0 * mt * mt * t_param) +
                  opt_r2 * (3.0 * mt * t_param * t_param) +
                  opt_r3 * (t_param * t_param * t_param);

    camera.looking_at = (1.0 - t_param) * start_center + t_param * end_center;

    vec3 z_dir = vec3{
      (float)(std::sin(theta) * std::sin(phi)),
      (float)(-std::sin(theta) * std::cos(phi)),
      (float)std::cos(theta)
    };
    camera.camera_pos = camera.looking_at + z_dir * (float)dist;
    camera.y_direction = vec3{
      (float)(-std::cos(theta) * std::sin(phi)),
      (float)(std::cos(theta) * std::cos(phi)),
      (float)std::sin(theta)
    };
    return camera;
  }
ImageRGBA RenderImage(int width, int height,
                        double T) {
    Camera camera = GetCamera(T);

    frame3 initial_tf = GetRootToWorldFrame(aug.poly, root_face);

    std::vector<frame3> face_tfs = ComputeNodeFrames(
        aug.poly,
        nodes,
        T,
        initial_tf);

    ImageRGBA img(width, height);
    img.Clear32(0x000000FF);

    double d = Diameter(aug.poly);
    double dist = yocto::length(camera.camera_pos - camera.looking_at);

    // Compute camera frame
    double t_param = max_depth > 0 ? std::clamp(T / max_depth, 0.0, 1.0) : 0.0;
    double theta = (1.0 - t_param) * opt_theta_0;
    double phi = opt_phi_0;

    vec3 z_dir = vec3{
      (float)(std::sin(theta) * std::sin(phi)),
      (float)(-std::sin(theta) * std::cos(phi)),
      (float)std::cos(theta)
    };
    vec3 x_dir = vec3{(float)std::cos(phi), (float)std::sin(phi), 0.0f};
    vec3 y_dir = vec3{
      (float)(-std::cos(theta) * std::sin(phi)),
      (float)(std::cos(theta) * std::cos(phi)),
      (float)std::sin(theta)
    };

    frame3 camera_frame;
    camera_frame.x = x_dir;
    camera_frame.y = y_dir;
    camera_frame.z = z_dir;
    camera_frame.o = camera.camera_pos;

    double progress = std::clamp(T / max_depth, 0.0, 1.0);
    double zoom = 1.0 - 0.5 * progress;

    DrawXYGrid(img, camera_frame, dist, zoom, d, width, height);

    auto ProjectPoint = [&](const vec3 &pt) -> vec2 {
      vec3 pt_cam = yocto::transform_point(yocto::inverse(camera_frame), pt);
      double z = -pt_cam.z;
      if (z < 0.01) z = 0.01;
      double factor = (std::min(width, height) * 0.6 * zoom) * (dist / z) / d;
      return vec2{
        width / 2.0 + pt_cam.x * factor,
        height / 2.0 - pt_cam.y * factor
      };
    };

    // Get their least common ancestor.
    auto GetLCADepth = [&](int f1, int f2) -> int {
      int n1 = -1, n2 = -1;
      for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].face_idx == f1) n1 = i;
        if (nodes[i].face_idx == f2) n2 = i;
      }
      if (n1 == -1 || n2 == -1) return 0;
      std::vector<int> path1;
      while (n1 != -1) {
        path1.push_back(n1);
        n1 = nodes[n1].parent_node_idx;
      }
      std::vector<int> path2;
      while (n2 != -1) {
        path2.push_back(n2);
        n2 = nodes[n2].parent_node_idx;
      }
      int lca = -1;
      for (int x1 : path1) {
        for (int x2 : path2) {
          if (x1 == x2) {
            lca = x1;
            break;
          }
        }
        if (lca != -1) break;
      }
      if (lca != -1) return nodes[lca].depth;
      return 0;
    };

    struct RenderFace {
      int node_idx;
      int face_idx;
      std::vector<vec2> proj_verts;
      double avg_z;
      uint32_t color;
    };

    std::vector<RenderFace> render_faces;
    render_faces.reserve(nodes.size());

    for (size_t i = 0; i < nodes.size(); ++i) {
      const auto &node = nodes[i];
      int f = node.face_idx;
      const auto &face = aug.poly.faces->v[f];

      std::vector<vec2> proj_verts;
      proj_verts.reserve(face.size());

      double sum_z = 0.0;
      for (int v_idx : face) {
        vec3 pt_world =
          yocto::transform_point(face_tfs[i], aug.poly.vertices[v_idx]);
        vec3 pt_cam =
          yocto::transform_point(yocto::inverse(camera_frame), pt_world);
        sum_z += pt_cam.z;
        proj_verts.push_back(ProjectPoint(pt_world));
      }

      uint32_t color = 0x77889955;
      if (T >= node.depth && face_overlap[f] != -1) {
        color = 0xAA333355;
      }

      render_faces.push_back(RenderFace{
        .node_idx = (int)i,
        .face_idx = f,
        .proj_verts = std::move(proj_verts),
        .avg_z = sum_z / face.size(),
        .color = color
      });
    }

    std::sort(render_faces.begin(), render_faces.end(),
              [](const RenderFace &a, const RenderFace &b) {
                return a.avg_z < b.avg_z;
              });

    for (const auto &rf : render_faces) {
      BlendFilledPoly(img, rf.proj_verts, rf.color);

      int num_verts = rf.proj_verts.size();
      for (int j = 0; j < num_verts; ++j) {
        vec2 p0 = rf.proj_verts[j];
        vec2 p1 = rf.proj_verts[(j + 1) % num_verts];

        int e_idx = aug.face_edges[rf.face_idx][j];
        bool is_connected = unfolding_edges[e_idx];

        uint32_t edge_color = cut_edge_color;
        if (is_connected) {
          edge_color = connected_edge_color;
        }
        else {
          const Faces::Edge &edge = aug.poly.faces->edges[e_idx];
          int detach_time = GetLCADepth(edge.f0, edge.f1);
          if (T < detach_time) {
            edge_color = connected_edge_color;
          }
        }

        img.BlendThickLine32(p0.x, p0.y, p1.x, p1.y, 1.2f, edge_color);
      }
    }

    return img;
  }
};


static void Animate(std::string_view poly_name,
                    std::optional<int> face_idx,
                    std::optional<int> edge_idx,
                    std::string_view filename) {
  auto [poly, example_net] = DB::GetPolyhedron(poly_name);

  CHECK(IsWellConditioned(poly.vertices));
  CHECK(IsManifold(poly));

  Aug aug = Aug(std::move(poly));

  std::string contents;

  ArcFour rc(std::format("inspect.{}", time(nullptr)));

  static constexpr int TARGET_NON_NETS = 3;

  Examples examples = GetSomeExamples(&rc, aug,
                                      face_idx, edge_idx,
                                      example_net,
                                      1, TARGET_NON_NETS, true);
  CHECK(!(examples.nets.empty() &&
          examples.non_nets.empty()));

  const Albrecht::DebugResult debug_result =
    examples.nets.empty() ? examples.non_nets[0] :
    examples.nets[0];

  StatusBar status(1);
  Periodically status_per(1.0);
  // TODO: choose this smartly (either from argument, or
  // near the "center" of the graph?)
  int root_face = 0;
  Scene scene(aug, debug_result.unfolding, root_face);

  static constexpr int WIDTH = 1920;
  static constexpr int HEIGHT = 1080;
  static constexpr int NUM_FRAMES = 8 * 60;
  double max_t = scene.max_depth + 1;

  {
    MovRecorder recorder(filename, WIDTH, HEIGHT);
    for (int i = 0; i < NUM_FRAMES; ++i) {
      status_per.RunIf([&]{
          status.Progress(i, NUM_FRAMES, "Rendering frames");
        });
      double T = max_t * i / (double)NUM_FRAMES;
      ImageRGBA img = scene.RenderImage(WIDTH, HEIGHT, T);
      recorder.AddFrame(std::move(img));
    }
    status.Progress(NUM_FRAMES, NUM_FRAMES, "Writing movie");
  }
  Print("Done.\n");
}

int main(int argc, char **argv) {
  ANSI::Init();

  std::string name;
  std::optional<int> face_idx, edge_idx;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-face" || arg == "-edge") {
      CHECK(i + 1 < argc) << "-face and -edge need arg.";
      i++;
      std::optional<int64_t> of = Util::ParseDoubleOpt(argv[i]);
      CHECK(of.has_value()) << "-face and -edge must be a number!";
      if (arg == "-face") face_idx = {of.value()};
      else if (arg == "-edge") edge_idx = {of.value()};
    } else {
      CHECK(name.empty()) << "Just one name.";
      name = arg;
    }
  }

  CHECK(!name.empty()) << "./inspect.exe [-face idx] [-edge idx] name";

  Animate(name, face_idx, edge_idx,
          std::format("animate-{}.mov", name));

  return 0;
}
