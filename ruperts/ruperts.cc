
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "atomic-util.h"
#include "auto-histo.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "mov-recorder.h"
#include "opt/opt.h"
#include "periodically.h"
#include "status-bar.h"
#include "threadutil.h"
#include "timer.h"
#include "util.h"

#include "yocto_matht.h"
#include "polyhedra.h"

DECLARE_COUNTERS(iters, attempts, u1_, u2_, u3_, u4_, u5_, u6_);

using vec2 = yocto::vec<double, 2>;
using vec3 = yocto::vec<double, 3>;
using vec4 = yocto::vec<double, 4>;
using mat4 = yocto::mat<double, 4>;
using quat4 = yocto::quat<double, 4>;
using frame3 = yocto::frame<double, 3>;

static void Render(const Polyhedron &p, uint32_t color, ImageRGBA *img) {
  // Function to project a 3D point to 2D using a perspective projection

  const double scale = std::min(img->Width(), img->Height()) * 0.75;

  // Perspective projection matrix (example values)
  // double aspect = (double)1920.0 / 1080.0;
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

      float x0 = (p0.x * scale + img->Width() * 0.5);
      float y0 = (p0.y * scale + img->Height() * 0.5);
      float x1 = (p1.x * scale + img->Width() * 0.5);
      float y1 = (p1.y * scale + img->Height() * 0.5);
      img->BlendThickLine32(x0, y0, x1, y1, 4.0f, color & 0xFFFFFF88);
    }
  }
}

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

constexpr double MESH_SCALE = 0.20;

static void RenderMesh(const Mesh2D &mesh,
                       ImageRGBA *img) {
  const int w = img->Width();
  const int h = img->Height();
  // XXX compute this from the polyhedron's diameter
  const double scale = std::min(w, h) * MESH_SCALE;

  CHECK(mesh.faces->v.size() < COLORS.size()) << mesh.faces->v.size()
                                              << " but have "
                                              << COLORS.size();

  auto ToWorld = [w, h, scale](int sx, int sy) -> vec2 {
      // Center of screen should be 0,0.
      double cy = sy - h / 2.0;
      double cx = sx - w / 2.0;
      return vec2{.x = cx / scale, .y = cy / scale};
    };
  auto ToScreen = [w, h, scale](const vec2 &pt) -> std::pair<int, int> {
    double cx = pt.x * scale;
    double cy = pt.y * scale;
    return std::make_pair(cx + w / 2.0, cy + h / 2.0);
  };

  // Draw filled polygons first.
  for (int sy = 0; sy < h; sy++) {
    for (int sx = 0; sx < w; sx++) {
      vec2 pt = ToWorld(sx, sy);
      for (int i = 0; i < mesh.faces->v.size(); i++) {
        if (PointInPolygon(pt, mesh.vertices, mesh.faces->v[i])) {
          img->BlendPixel32(sx, sy, COLORS[i] & 0xFFFFFF22);
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
      img->BlendThickLine32(ax, ay, bx, by, 3.0, 0xFFFFFF99);
    }
  }
}

static void RenderHull(const Mesh2D &mesh,
                       const std::vector<int> &hull,
                       ImageRGBA *img) {
  const int w = img->Width();
  const int h = img->Height();
  // XXX compute this from the polyhedron's diameter
  const double scale = std::min(w, h) * MESH_SCALE;

  auto ToScreen = [w, h, scale](const vec2 &pt) -> std::pair<int, int> {
    double cx = pt.x * scale;
    double cy = pt.y * scale;
    return std::make_pair(cx + w / 2.0, cy + h / 2.0);
  };

  for (int i = 0; i < hull.size(); i++) {
    const vec2 &v0 = mesh.vertices[hull[i]];
    const vec2 &v1 = mesh.vertices[hull[(i + 1) % hull.size()]];

    const auto &[x0, y0] = ToScreen(v0);
    const auto &[x1, y1] = ToScreen(v1);

    img->BlendThickLine32(x0, y0, x1, y1, 2.0, 0x00FF00AA);
  }

  for (int i = 0; i < hull.size(); i++) {
    const vec2 &v0 = mesh.vertices[hull[i]];
    const auto &[x, y] = ToScreen(v0);
    img->BlendText32(x - 12, y - 12, 0xFFFF00FF,
                     StringPrintf("%d", i));
  }

}

[[maybe_unused]]
static void AnimateMesh() {
  ArcFour rc("animate");
  // const Polyhedron poly = Cube();
  // const Polyhedron poly = Dodecahedron();
  const Polyhedron poly = SnubCube();
  quat4 initial_rot = RandomQuaternion(&rc);

  constexpr int SIZE = 1080;
  constexpr int FRAMES = 10 * 60;
  // constexpr int FRAMES = 10;

  MovRecorder rec("animate.mov", SIZE, SIZE);

  StatusBar status(2);
  Periodically status_per(1.0);
  for (int i = 0; i < FRAMES; i++) {
    if (status_per.ShouldRun()) {
      status.Progressf(i, FRAMES, "rotate");
    }

    double t = i / (double)FRAMES;
    double angle = t * 2.0 * std::numbers::pi;

    // rotation quat actually returns vec4; isomorphic to quat4.
    quat4 frame_rot =
      QuatFromVec(yocto::rotation_quat<double>({0.0, 1.0, 0.0}, angle));

    quat4 final_rot = normalize(initial_rot * frame_rot);
    Polyhedron rpoly = Rotate(poly, yocto::rotation_frame(final_rot));

    ImageRGBA img(SIZE, SIZE);
    img.Clear32(0x000000FF);
    Mesh2D mesh = Shadow(rpoly);
    RenderMesh(mesh, &img);
    rec.AddFrame(std::move(img));
  }
}

[[maybe_unused]]
static void Visualize() {
  // ArcFour rc(StringPrintf("seed.%lld", time(nullptr)));
  ArcFour rc("fixed-seed");

  // const Polyhedron poly = Cube();
  // const Polyhedron poly = Dodecahedron();
  const Polyhedron poly = SnubCube();
  CHECK(PlanarityError(poly) < 1.0e-10);

  {
    ImageRGBA img(1920, 1080);
    img.Clear32(0x000000FF);
    for (int i = 0; i < 5; i++) {
      frame3 frame = yocto::rotation_frame(RandomQuaternion(&rc));
      Polyhedron rpoly = Rotate(poly, frame);

      CHECK(PlanarityError(rpoly) < 1.0e10);
      Render(rpoly, COLORS[i], &img);
    }

    img.Save("wireframe.png");
  }

  {
    ImageRGBA img(1920, 1080);
    img.Clear32(0x000000FF);

    quat4 q = RandomQuaternion(&rc);
    frame3 frame = yocto::rotation_frame(q);

    Polyhedron rpoly = Rotate(poly, frame);

    Mesh2D mesh = Shadow(rpoly);
    RenderMesh(mesh, &img);

    std::vector<int> hull = ConvexHull(mesh.vertices);
    RenderHull(mesh, hull, &img);

    img.Save("shadow.png");
    printf("Wrote shadow.png\n");
  }
}

static void Solve(const Polyhedron &polyhedron) {
  // ArcFour rc(StringPrintf("solve.%lld", time(nullptr)));

  static constexpr int HISTO_LINES = 32;

  std::mutex m;
  bool should_die = false;
  Timer run_timer;
  StatusBar status(3 + HISTO_LINES);
  Periodically status_per(1.0);
  Periodically image_per(10.0);
  double best_error = 1.0e42;
  AutoHisto error_histo(100000);
  constexpr int NUM_THREADS = 4;

  double prep_time = 0.0, opt_time = 0.0;

  ParallelFan(
      NUM_THREADS,
      [&](int thread_idx) {
        ArcFour rc(StringPrintf("solve.%d.%lld", thread_idx,
                                time(nullptr)));
        for (;;) {
          {
            MutexLock ml(&m);
            if (should_die) return;
          }

          Timer prep_timer;
          quat4 outer_rot = RandomQuaternion(&rc);
          const frame3 outer_frame = yocto::rotation_frame(outer_rot);
          Polyhedron outer = Rotate(polyhedron, outer_frame);
          Mesh2D souter = Shadow(outer);

          const std::vector<int> shadow_hull = ConvexHull(souter.vertices);

          // Starting orientation/position.
          const quat4 inner_rot = RandomQuaternion(&rc);

          static constexpr int D = 6;
          auto InnerFrame = [&inner_rot](const std::array<double, D> &args) {
              const auto &[di, dj, dk, dl, dx, dy] = args;
              quat4 tweaked_rot = normalize(quat4{
                  .x = inner_rot.x + di,
                  .y = inner_rot.y + dj,
                  .z = inner_rot.z + dk,
                  .w = inner_rot.w + dl,
                });
              frame3 rotate = yocto::rotation_frame(tweaked_rot);
              frame3 translate = yocto::translation_frame(
                  vec3{.x = dx, .y = dy, .z = 0.0});
              return rotate * translate;
            };

          auto WriteImage = [&](const std::string &filename,
                                const std::array<double, D> &args) {
              // Show:
              ImageRGBA img(3840, 2160);
              img.Clear32(0x000000FF);

              RenderMesh(souter, &img);
              // Darken background.
              for (int y = 0; y < img.Height(); y++) {
                for (int x = 0; x < img.Width(); x++) {
                  img.BlendPixel32(x, y, 0x550000AA);
                }
              }

              auto inner_frame = InnerFrame(args);
              Polyhedron inner = Rotate(polyhedron, inner_frame);
              Mesh2D sinner = Shadow(inner);
              RenderMesh(sinner, &img);

              img.Save(filename);

              status.Printf("Wrote " AGREEN("%s") "\n", filename.c_str());
            };

          auto Parameters = [&](const std::array<double, D> &args,
                                double error) {
              auto inner_frame = InnerFrame(args);
              std::string contents =
                StringPrintf("Error: %.17g\n", error);

              contents += "Outer frame:\n";
              contents += FrameString(outer_frame);
              contents += "\nInner frame:\n";
              contents += FrameString(inner_frame);
              StringAppendF(&contents,
                            "\nTook %lld iters, %lld attempts, %.3f seconds\n",
                            iters.Read(), attempts.Read(), run_timer.Seconds());
              return contents;
            };


          std::function<double(const std::array<double, D> &)> Loss =
            [&polyhedron, &souter, &shadow_hull, &InnerFrame](
                const std::array<double, D> &args) {
              attempts++;
              frame3 frame = InnerFrame(args);
              Polyhedron inner = Rotate(polyhedron, frame);
              Mesh2D sinner = Shadow(inner);

              // Does every vertex in inner fall inside the outer shadow?
              double error = 0.0;
              for (const vec2 &iv : sinner.vertices) {
                if (!InHull(souter, shadow_hull, iv)) {
                  // PERF we should get the distance to the convex hull,
                  // but distance from the origin should at least have
                  // the right slope.
                  // error += length(iv);
                  error += DistanceToHull(souter, shadow_hull, iv);
                }
              }

              return error;
            };

          const std::array<double, D> lb =
            {-0.15, -0.15, -0.15, -0.15, -0.25, -0.25};
          const std::array<double, D> ub =
            {+0.15, +0.15, +0.15, +0.15, +0.25, +0.25};
          const double prep_sec = prep_timer.Seconds();

          Timer opt_timer;
          const auto &[args, error] =
            Opt::Minimize<D>(Loss, lb, ub, 1000, 2);
          const double opt_sec = opt_timer.Seconds();

          if (error == 0.0) {
            MutexLock ml(&m);
            should_die = true;

            status.Printf("Solved! %lld iters, %lld attempts, in %s\n",
                          iters.Read(),
                          attempts.Read(),
                          ANSI::Time(run_timer.Seconds()).c_str());

            WriteImage("solved.png", args);

            std::string contents = Parameters(args, error);
            StringAppendF(&contents,
                          "\n%s\n",
                          error_histo.SimpleAsciiString(50).c_str());

            Util::WriteFile("solution.txt", contents);
            status.Printf("Wrote " AGREEN("solution.txt") "\n");

            return;
          }

          {
            MutexLock ml(&m);
            prep_time += prep_sec;
            opt_time += opt_sec;
            error_histo.Observe(log(error));
            if (error < best_error) {
              best_error = error;
              if (image_per.ShouldRun()) {
                std::string file_base =
                  StringPrintf("best.%lld", iters.Read());
                WriteImage(file_base + ".png", args);
                Util::WriteFile(file_base + ".txt", Parameters(args, error));
              }
            }

            status_per.RunIf([&]() {
                double total_time = prep_time + opt_time;

                int64_t it = iters.Read();
                double ips = it / total_time;

                status.Statusf(
                    "%s\n"
                    "%s " ABLUE("prep") " %s " APURPLE("opt")
                    " (" ABLUE("%.3f%%") " / " APURPLE("%.3f%%") ") "
                    "[" AWHITE("%.3f") "/s]\n"
                    "%s iters, %s attempts; best: %.11g",
                    error_histo.SimpleANSI(HISTO_LINES).c_str(),
                    ANSI::Time(prep_time).c_str(),
                    ANSI::Time(opt_time).c_str(),
                    (100.0 * prep_time) / total_time,
                    (100.0 * opt_time) / total_time,
                    ips,
                    FormatNum(it).c_str(),
                    FormatNum(attempts.Read()).c_str(),
                    best_error);
              });
          }

          iters++;
        }
      });
}


int main(int argc, char **argv) {
  ANSI::Init();
  printf("\n");

  // (void)SnubCube();
  Visualize();
  // AnimateMesh();

  printf("\n");
  // Solve(Cube());
  // Solve(Dodecahedron());
  Solve(SnubCube());

  printf("OK\n");
  return 0;
}
